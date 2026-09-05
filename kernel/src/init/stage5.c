#include <kernel/stage5.h>

#include <kernel/block_device.h>
#include <kernel/display.h>
#include <kernel/framebuffer_boot.h>
#include <kernel/ipc.h>
#include <kernel/process/process.h>
#include <kernel/process/process_service.h>
#include <arch/i386/port.h>
#include <arch/i386/ps2.h>
#include "../exec/multiboot_exec.h"

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

bool stage5_boot_services(const struct block_device *device,
                          struct i386_context *initial_context,
                          bool *initial_context_ready)
{
	if (device == nullptr || initial_context == nullptr ||
		initial_context_ready == nullptr)
		return false;
	struct process_exec_result procserv;
	struct process_exec_result input;
	struct process_exec_result shell;
	if (!process_load_block_device_app(device, "procserv", &procserv) ||
		!process_load_block_device_app(device, "input", &input) ||
		!process_load_block_device_app(device, "shell", &shell))
		return false;
	int32_t procserv_endpoint_result = ipc_endpoint_create_for(procserv.process);
	int32_t input_endpoint_result = ipc_endpoint_create_for(input.process);
	int32_t shell_endpoint_result = ipc_endpoint_create_for(shell.process);
	if (procserv_endpoint_result < 0 || input_endpoint_result < 0 ||
		shell_endpoint_result < 0)
		return false;
	uint32_t procserv_endpoint = (uint32_t)procserv_endpoint_result;
	uint32_t input_endpoint = (uint32_t)input_endpoint_result;
	uint32_t shell_endpoint = (uint32_t)shell_endpoint_result;
	uint32_t framebuffer_endpoint = framebuffer_boot_endpoint();
	char procserv_endpoint_text[12];
	char input_endpoint_text[12];
	char shell_endpoint_text[12];
	char framebuffer_endpoint_text[12];
	if (u32_to_decimal(procserv_endpoint, procserv_endpoint_text,
			sizeof(procserv_endpoint_text)) == 0 ||
		u32_to_decimal(input_endpoint, input_endpoint_text,
			sizeof(input_endpoint_text)) == 0 ||
		u32_to_decimal(shell_endpoint, shell_endpoint_text,
			sizeof(shell_endpoint_text)) == 0 ||
		u32_to_decimal(framebuffer_endpoint, framebuffer_endpoint_text,
			sizeof(framebuffer_endpoint_text)) == 0)
		return false;
	if (!ipc_grant_process(shell_endpoint, process_pid(input.process),
		JANOS_IPC_RIGHT_NOTIFY) ||
		!ipc_grant_process(procserv_endpoint, process_pid(shell.process),
		JANOS_IPC_RIGHT_SEND))
		return false;
	if (framebuffer_endpoint != 0 &&
		!ipc_grant_process(framebuffer_endpoint, process_pid(shell.process),
		JANOS_IPC_RIGHT_SEND))
		return false;
	process_service_configure(procserv.process, device);
	if (framebuffer_endpoint != 0) {
		if (!framebuffer_console_handoff(shell.process))
			return false;
		kprintf("FBHANDOFF_READY pid=%u\n", (unsigned)process_pid(shell.process));
	}

	const char *procserv_argv[] = {
		"procserv", "server", procserv_endpoint_text, nullptr,
	};
	const char *input_argv[] = {
		"input", "server", input_endpoint_text, shell_endpoint_text, nullptr,
	};
	const char *shell_argv[] = {
		"shell", "server", shell_endpoint_text, framebuffer_endpoint_text,
		procserv_endpoint_text, input_endpoint_text, nullptr,
	};
	if (!process_start(procserv.process, procserv.entry, 3, procserv_argv))
		return false;
	if (!*initial_context_ready) {
		if (!process_initial_context(procserv.process, initial_context))
			return false;
		*initial_context_ready = true;
	}
	ps2_set_input_endpoint(input_endpoint);
	if (!process_start(input.process, input.entry, 4, input_argv) ||
		!process_start(shell.process, shell.entry, 6, shell_argv))
		return false;
	return true;
}
