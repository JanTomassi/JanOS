#include <kernel/process/process_service.h>

#include <kernel/block_device.h>
#include <kernel/process/process.h>
#include <kernel/scheduler.h>
#include <kernel/syscall.h>
#include <kernel/vir_mem.h>
#include <kernel/ipc.h>

#include <arch/i386/smp.h>
#include <string.h>

#include "../exec/multiboot_exec.h"

static struct process *service_process = nullptr;
static struct block_device application_device = {0};
static bool configured = false;

static size_t u32_to_decimal(uint32_t value, char *buffer, size_t size)
{
	char reversed[11];
	size_t length = 0;
	if (buffer == nullptr || size < 2)
		return 0;
	if (value == 0)
		reversed[length++] = '0';
	while (value != 0 && length < sizeof(reversed)) {
		reversed[length++] = (char)('0' + value % 10);
		value /= 10;
	}
	if (length + 1 > size)
		return 0;
	for (size_t i = 0; i < length; ++i)
		buffer[i] = reversed[length - i - 1];
	buffer[length] = '\0';
	return length;
}

static bool authorized(void)
{
	return configured && process_current() == service_process;
}

void process_service_configure(struct process *service,
	                             const struct block_device *device)
{
	service_process = service;
	if (device != nullptr)
		application_device = *device;
	configured = service != nullptr && device != nullptr;
}

void process_service_process_exiting(struct process *process)
{
	if (process == service_process) {
		service_process = nullptr;
		configured = false;
	}
}

static int32_t snapshot_handler(syscall_frame *frame, void *context)
{
	(void)context;
	struct janos_process_info info;
	if (!authorized())
		return -SYSCALL_EBADF;
	if (!user_buffer(frame->ecx, sizeof(info),
		VMM_ENTRY_USER_SUPER_BIT | VMM_ENTRY_READ_WRITE_BIT))
		return -SYSCALL_EFAULT;
	int32_t result = process_snapshot(frame->ebx, &info);
	if (result < 0)
		return result;
	return copy_to_user((void *)(uintptr_t)frame->ecx, &info, sizeof(info))
		? 0 : -SYSCALL_EFAULT;
}

static bool bounded_string(const char *text, size_t size)
{
	if (text == nullptr || text[0] == '\0')
		return false;
	for (size_t i = 0; i < size; ++i)
		if (text[i] == '\0')
			return true;
	return false;
}

static bool activate_kernel_space(uint32_t *old_page_directory)
{
	if (old_page_directory == nullptr)
		return false;
	__asm__ volatile("mov %%cr3, %0" : "=r"(*old_page_directory) : : "memory");
	const fatptr_t *kernel_page_directory = vmm_kernel_page_directory();
	return kernel_page_directory != nullptr &&
		vmm_page_directory_activate(kernel_page_directory);
}

static void restore_page_directory(uint32_t page_directory)
{
	__asm__ volatile("mov %0, %%cr3" : : "r"(page_directory) : "memory");
}

static int32_t spawn_handler(syscall_frame *frame, void *context)
{
	(void)context;
	struct janos_process_exec_request request;
	struct janos_process_info info;
	struct janos_process_spawn_result result_info;
	char endpoint_text[12];
	if (!authorized())
		return -SYSCALL_EBADF;
	if (!user_buffer(frame->ebx, sizeof(request), VMM_ENTRY_USER_SUPER_BIT) ||
		!user_buffer(frame->ecx, sizeof(result_info),
		VMM_ENTRY_USER_SUPER_BIT | VMM_ENTRY_READ_WRITE_BIT))
		return -SYSCALL_EFAULT;
	if (!copy_from_user(&request, (const void *)(uintptr_t)frame->ebx,
		sizeof(request)))
		return -SYSCALL_EFAULT;
	if (!bounded_string(request.name, sizeof(request.name)) ||
		request.argument_length >= sizeof(request.argument))
		return -JANOS_EINVAL;
	if (request.cpu_affinity != JANOS_PROCESS_CPU_ANY) {
		size_t cpu_count = 0;
		(void)smp_get_cpus(&cpu_count);
		if (request.cpu_affinity >= cpu_count || request.cpu_affinity >= 16)
			return -JANOS_EINVAL;
	}

	if (!process_exists(request.parent_pid))
		return -JANOS_ESRCH;
	char name[JANOS_PROCESS_NAME_SIZE];
	char argument[JANOS_PROCESS_ARGUMENT_SIZE];
	memcpy(name, request.name, sizeof(name));
	memcpy(argument, request.argument, request.argument_length);
	argument[request.argument_length] = '\0';
	const char *argv[3] = { name, argument, nullptr };
	int argc = 3;

	struct process_exec_result loaded;
	uint32_t old_page_directory;
	if (!activate_kernel_space(&old_page_directory))
		return -SYSCALL_EFAULT;
	if (!process_load_block_device_app_for_parent_pid(&application_device, name,
		request.parent_pid, &loaded)) {
		restore_page_directory(old_page_directory);
		return -JANOS_EINVAL;
	}
	int32_t endpoint = ipc_endpoint_create_for(loaded.process);
	if (endpoint < 0 ||
		u32_to_decimal((uint32_t)endpoint, endpoint_text,
			sizeof(endpoint_text)) == 0 ||
		!ipc_grant_process((uint32_t)endpoint, request.parent_pid,
			JANOS_IPC_RIGHT_SEND | JANOS_IPC_RIGHT_NOTIFY)) {
		process_destroy(loaded.process);
		restore_page_directory(old_page_directory);
		return -JANOS_ENOMEM;
	}
	argv[1] = argument;
	argv[2] = endpoint_text;
	argc = 3;
	if (!process_start(loaded.process, loaded.entry, argc, argv)) {
		process_destroy(loaded.process);
		restore_page_directory(old_page_directory);
		return -JANOS_EINVAL;
	}
	if (request.cpu_affinity != JANOS_PROCESS_CPU_ANY &&
		!scheduler_set_affinity(loaded.process, (uint8_t)request.cpu_affinity)) {
		restore_page_directory(old_page_directory);
		return -JANOS_EINVAL;
	}
	int32_t result = process_snapshot_pid(process_pid(loaded.process), &info);
	restore_page_directory(old_page_directory);
	if (result < 0)
		return result;
	result_info.process = info;
	result_info.input_endpoint = (uint32_t)endpoint;
	return copy_to_user((void *)(uintptr_t)frame->ecx, &result_info,
		sizeof(result_info))
		? 0 : -SYSCALL_EFAULT;
}

static int32_t wait_handler(syscall_frame *frame, void *context)
{
	(void)context;
	struct process *current = process_current();
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	if (frame->ecx != 0 && !user_buffer(frame->ecx, sizeof(int32_t),
		VMM_ENTRY_USER_SUPER_BIT | VMM_ENTRY_READ_WRITE_BIT))
		return -SYSCALL_EFAULT;
	return process_wait_child(current, frame->ebx, frame->ecx, frame->edx);
}

bool process_service_register_syscalls(void)
{
	return syscall_register(JANOS_SYS_PROCESS_SNAPSHOT, snapshot_handler, nullptr) &&
		syscall_register(JANOS_SYS_PROCESS_SPAWN, spawn_handler, nullptr) &&
		syscall_register(JANOS_SYS_PROCESS_WAIT, wait_handler, nullptr);
}
