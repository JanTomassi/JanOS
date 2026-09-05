#include <kernel/framebuffer_boot.h>

#include <kernel/boot_log.h>
#include <kernel/display_session.h>
#include <kernel/display.h>
#include <kernel/framebuffer.h>
#include <kernel/ipc.h>
#include <kernel/process/address_space.h>
#include <kernel/process/process.h>
#include <kernel/spinlock.h>
#include <kernel/vir_mem.h>
#include <string.h>

#include "../exec/multiboot_exec.h"

static uint32_t framebuffer_endpoint;
static struct boot_log boot_log;
static struct display_session console_session = {
	.mode = DISPLAY_SESSION_BOOT,
};
static spinlock_t console_lock = { 0 };
static uint32_t next_session_token = 1;

static bool foreground_process_allowed(const struct process *process)
{
	while (process != nullptr) {
		if (display_session_allows(&console_session, process_pid(process)))
			return true;
		process = process_parent(process);
	}
	return false;
}

static size_t framebuffer_console_write_internal(const char *buffer, size_t length,
	bool user_output)
{
	if (buffer == nullptr)
		return 0;
	uint32_t flags = spin_lock_irqsave(&console_lock);
	bool allowed = display_session_allows(&console_session, 0);
	if (!allowed && user_output)
		allowed = foreground_process_allowed(process_current());
	size_t written = allowed ? boot_log_write(&boot_log, buffer, length) : 0;
	spin_unlock_irqrestore(&console_lock, flags);
	if (framebuffer_endpoint != 0 && written != 0)
		ipc_wake_receiver(framebuffer_endpoint);
	return length;
}

size_t framebuffer_console_write(const char *buffer, size_t length)
{
	return framebuffer_console_write_internal(buffer, length, false);
}

size_t framebuffer_console_write_user(const char *buffer, size_t length)
{
	return framebuffer_console_write_internal(buffer, length, true);
}

size_t framebuffer_console_read(char *buffer, size_t length)
{
	if (buffer == nullptr || length == 0)
		return 0;
	uint32_t flags = spin_lock_irqsave(&console_lock);
	size_t count = boot_log_read(&boot_log, buffer, length);
	spin_unlock_irqrestore(&console_lock, flags);
	return count;
}

bool framebuffer_boot_clear(void)
{
	struct janos_ipc_message request = { .header = {
		.type = JANOS_FB_MSG_CLEAR,
		.flags = JANOS_IPC_REQUEST,
	} };
	return framebuffer_endpoint != 0 && ipc_kernel_send(framebuffer_endpoint,
		&request);
}

uint32_t framebuffer_boot_endpoint(void)
{
	return framebuffer_endpoint;
}

void framebuffer_console_flush(void)
{
	/* Output is drained by fbserver through the framebuffer read syscall. */
}

static bool queue_session_update_locked(uint32_t mode, uint32_t owner,
	uint32_t token)
{
	if (framebuffer_endpoint == 0)
		return false;
	struct janos_ipc_message request = { .header = {
		.type = JANOS_FB_MSG_SESSION,
		.flags = JANOS_IPC_REQUEST,
		.length = sizeof(struct janos_fb_session),
	} };
	struct janos_fb_session session = {
		.mode = mode,
		.owner = owner,
		.token = token,
	};
	memcpy(request.payload, &session, sizeof(session));
	return ipc_kernel_send(framebuffer_endpoint, &request);
}

bool framebuffer_console_handoff(struct process *process)
{
	if (process == nullptr)
		return false;
	process_pid_t owner = process_pid(process);
	uint32_t flags = spin_lock_irqsave(&console_lock);
	if (console_session.mode != DISPLAY_SESSION_BOOT) {
		spin_unlock_irqrestore(&console_lock, flags);
		return false;
	}
	uint32_t token = next_session_token++;
	if (token == 0)
		token = next_session_token++;
	bool claimed = display_session_claim(&console_session, owner, token);
	if (claimed && !queue_session_update_locked(
		JANOS_FB_SESSION_FOREGROUND, owner, token)) {
		(void)display_session_release(&console_session, owner, token);
		claimed = false;
	}
	spin_unlock_irqrestore(&console_lock, flags);
	return claimed;
}

bool framebuffer_console_release(struct process *process)
{
	if (process == nullptr)
		return false;
	process_pid_t owner = process_pid(process);
	uint32_t flags = spin_lock_irqsave(&console_lock);
	bool owns_session = console_session.mode == DISPLAY_SESSION_FOREGROUND &&
		console_session.owner == owner;
	uint32_t token = console_session.token;
	if (!owns_session || !queue_session_update_locked(
		JANOS_FB_SESSION_BOOT, owner, token)) {
		spin_unlock_irqrestore(&console_lock, flags);
		return false;
	}
	bool released = display_session_release(&console_session, owner, token);
	spin_unlock_irqrestore(&console_lock, flags);
	return released;
}

void framebuffer_console_enable(void)
{
	/* Boot-log capture is active before the framebuffer server starts. */
}

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

bool framebuffer_boot_services(const void *multiboot_info,
                               size_t multiboot_info_size,
                               const struct block_device *device,
                               struct i386_context *initial_context)
{
	if (device == nullptr || initial_context == nullptr)
		return false;

	struct process_exec_result server;
	if (!process_load_block_device_app(device, "fbserver", &server))
		return false;
	int32_t capability = framebuffer_capability_create_from_multiboot(
		multiboot_info, multiboot_info_size, server.process);
	if (capability < 0)
		return false;
	struct janos_framebuffer_info framebuffer;
	if (!framebuffer_capability_grant((uint32_t)capability, server.process,
		&framebuffer))
		return false;
	void *font_address = nullptr;
	size_t font_size;
	if (!process_map_block_device_file(device, "janos.psf", server.process,
		VMM_ENTRY_USER_SUPER_BIT, &font_address, &font_size))
		return false;

	char endpoint_text[12];
	char framebuffer_address_text[12];
	char framebuffer_size_text[12];
	char pitch_text[12];
	char width_text[12];
	char height_text[12];
	char bpp_text[12];
	char type_text[12];
	char font_address_text[12];
	char font_size_text[12];
	int32_t endpoint_result = ipc_endpoint_create_for(server.process);
	if (endpoint_result < 0 ||
		u32_to_decimal((uint32_t)endpoint_result, endpoint_text, sizeof(endpoint_text)) == 0 ||
		u32_to_decimal(framebuffer.address, framebuffer_address_text,
			sizeof(framebuffer_address_text)) == 0 ||
		u32_to_decimal(framebuffer.size, framebuffer_size_text,
			sizeof(framebuffer_size_text)) == 0 ||
		u32_to_decimal(framebuffer.pitch, pitch_text, sizeof(pitch_text)) == 0 ||
		u32_to_decimal(framebuffer.width, width_text, sizeof(width_text)) == 0 ||
		u32_to_decimal(framebuffer.height, height_text, sizeof(height_text)) == 0 ||
		u32_to_decimal(framebuffer.bpp, bpp_text, sizeof(bpp_text)) == 0 ||
		u32_to_decimal(framebuffer.type, type_text, sizeof(type_text)) == 0 ||
		u32_to_decimal((uint32_t)(uintptr_t)font_address, font_address_text,
			sizeof(font_address_text)) == 0 ||
		u32_to_decimal((uint32_t)font_size, font_size_text,
			sizeof(font_size_text)) == 0)
		return false;
	framebuffer_endpoint = (uint32_t)endpoint_result;

	const char *server_argv[] = {
		"fbserver", "server", endpoint_text, framebuffer_address_text,
		framebuffer_size_text, pitch_text, width_text, height_text, bpp_text,
		type_text, font_address_text, font_size_text, nullptr,
	};
	if (!process_start(server.process, server.entry, 12, server_argv))
		return false;
	if (!process_initial_context(server.process, initial_context))
		return false;

	return true;
}
