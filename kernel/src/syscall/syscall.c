#include <kernel/syscall.h>

#include <kernel/process/process.h>
#include <kernel/process/address_space.h>
#include <kernel/tty.h>
#include <kernel/vir_mem.h>
#include <kernel/scheduler.h>
#include <kernel/ipc.h>
#include <kernel/framebuffer_boot.h>
#include <kernel/process/process_service.h>
#include <arch/i386/smp.h>

struct syscall_slot {
	syscall_handler_t handler;
	void *context;
};

static struct syscall_slot slots[JANOS_SYS_MAX];

bool copy_from_user(void *destination, const void *source, size_t length)
{
	struct process *process = process_current();
	return process != nullptr && (destination != nullptr || length == 0) &&
		address_space_validate(process_address_space(process), (uintptr_t)source,
			length, VMM_ENTRY_USER_SUPER_BIT) &&
		address_space_copy_from(process_address_space(process), destination,
			(uintptr_t)source, length);
}

bool copy_to_user(void *destination, const void *source, size_t length)
{
	struct process *process = process_current();
	return process != nullptr && (source != nullptr || length == 0) &&
		address_space_validate(process_address_space(process), (uintptr_t)destination,
			length, VMM_ENTRY_USER_SUPER_BIT | VMM_ENTRY_READ_WRITE_BIT) &&
		address_space_copy_to(process_address_space(process), (uintptr_t)destination,
			source, length);
}

bool user_buffer(uintptr_t address, size_t length, uint16_t flags)
{
	struct process *process = process_current();
	return process != nullptr && (length == 0 || address_space_validate(
		process_address_space(process), address, length, flags));
}

static int32_t syscall_read_handler(syscall_frame *frame, void *context)
{
	(void)context;
	if (frame->ebx != 0)
		return -SYSCALL_EBADF;
	if (frame->edx > INT32_MAX || !user_buffer(frame->ecx, frame->edx,
		VMM_ENTRY_USER_SUPER_BIT | VMM_ENTRY_READ_WRITE_BIT))
		return -SYSCALL_EFAULT;
	char buffer[256];
	size_t count = 0;
	while (count < frame->edx) {
		size_t chunk = frame->edx - count;
		if (chunk > sizeof(buffer))
			chunk = sizeof(buffer);
		int32_t got = console_read(buffer, chunk, frame);
		if (got == -SYSCALL_EIPC_BLOCKED)
			return got;
		if (got == 0)
			break;
		if (!copy_to_user((void *)(uintptr_t)frame->ecx + count, buffer, got))
			return -SYSCALL_EFAULT;
		count += got;
		break;
	}
	return (int32_t)count;
}

static int32_t syscall_write_handler(syscall_frame *frame, void *context)
{
	(void)context;
	if (frame->ebx != 1 && frame->ebx != 2)
		return -SYSCALL_EBADF;
	if (frame->edx > INT32_MAX || !user_buffer(frame->ecx, frame->edx,
		VMM_ENTRY_USER_SUPER_BIT))
		return -SYSCALL_EFAULT;
	char buffer[256];
	size_t count = 0;
	while (count < frame->edx) {
		size_t chunk = frame->edx - count;
		if (chunk > sizeof(buffer))
			chunk = sizeof(buffer);
		if (!copy_from_user(buffer, (const void *)(uintptr_t)frame->ecx + count, chunk))
			return -SYSCALL_EFAULT;
		console_write(buffer, chunk);
		count += chunk;
	}
	return (int32_t)count;
}

static int32_t syscall_exit_handler(syscall_frame *frame, void *context)
{
	(void)context;
	if (process_current() == nullptr)
		return -SYSCALL_ESRCH;
	process_exit_current((int32_t)frame->ebx);
	return 0;
}

static int32_t syscall_yield_handler(syscall_frame *frame, void *context)
{
	(void)context;
	scheduler_yield(frame);
	return 0;
}

static int32_t syscall_framebuffer_read(syscall_frame *frame, void *context)
{
	(void)context;
	if (frame->edx > JANOS_FRAMEBUFFER_OUTPUT_CHUNK ||
		!user_buffer(frame->ecx, frame->edx, VMM_ENTRY_USER_SUPER_BIT |
			VMM_ENTRY_READ_WRITE_BIT))
		return -SYSCALL_EFAULT;
	char buffer[JANOS_FRAMEBUFFER_OUTPUT_CHUNK];
	size_t count = framebuffer_console_read(buffer, frame->edx);
	if (count != 0 && !copy_to_user((void *)(uintptr_t)frame->ecx, buffer, count))
		return -SYSCALL_EFAULT;
	return (int32_t)count;
}

static int32_t syscall_cpu_get(syscall_frame *frame, void *context)
{
	(void)frame;
	(void)context;
	return (int32_t)smp_current_cpu_index();
}

void syscall_init(void)
{
	for (size_t i = 0; i < JANOS_SYS_MAX; ++i)
		slots[i] = (struct syscall_slot){ 0 };
}

bool syscall_register(uint32_t number, syscall_handler_t handler, void *context)
{
	if (number >= JANOS_SYS_MAX || handler == nullptr || slots[number].handler != nullptr)
		return false;
	slots[number] = (struct syscall_slot){ .handler = handler, .context = context };
	return true;
}

int32_t syscall_dispatch(syscall_frame *frame)
{
	if (frame == nullptr)
		return -SYSCALL_ENOSYS;
	int32_t result = -SYSCALL_ENOSYS;
	if (frame->eax < JANOS_SYS_MAX && slots[frame->eax].handler != nullptr)
		result = slots[frame->eax].handler(frame, slots[frame->eax].context);
	/* A blocked IPC syscall has already saved its user continuation. */
	if (result != -SYSCALL_EIPC_BLOCKED)
		frame->eax = (uint32_t)result;
	return result;
}

bool syscall_register_console_handlers(void)
{
	if (slots[JANOS_SYS_READ].handler != nullptr ||
	    slots[JANOS_SYS_WRITE].handler != nullptr ||
	    slots[JANOS_SYS_EXIT].handler != nullptr)
		return false;

        bool res = true;
	res &= syscall_register(JANOS_SYS_READ, syscall_read_handler, nullptr);
        res &= syscall_register(JANOS_SYS_YIELD, syscall_yield_handler, nullptr);
        res &= syscall_register(JANOS_SYS_WRITE, syscall_write_handler, nullptr);
        res &= syscall_register(JANOS_SYS_EXIT, syscall_exit_handler, nullptr);
        res &= syscall_register(JANOS_SYS_FRAMEBUFFER_READ, syscall_framebuffer_read, nullptr);
        res &= syscall_register(JANOS_SYS_CPU_GET, syscall_cpu_get, nullptr);
        res &= ipc_register_syscalls();
        res &= process_service_register_syscalls();

        return res;
}
