#include <kernel/test.h>

#include <kernel/display.h>
#include <kernel/framebuffer.h>
#include <kernel/block_device.h>
#include <kernel/fat16.h>
#include <kernel/ipc.h>
#include <kernel/process/address_space.h>
#include <kernel/process/process.h>
#include <kernel/scheduler.h>
#include <kernel/syscall.h>
#include <kernel/vir_mem.h>
#include <arch/i386/context.h>
#include <arch/i386/smp.h>

#include "../../kernel/src/exec/multiboot_exec.h"

#include <stdint.h>
#include <string.h>

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

static bool find_application_disk(const char *required_file,
                                  struct block_device *out_device)
{
	if (required_file == nullptr || out_device == nullptr)
		return false;
	for (size_t i = 0; i < block_device_count(); ++i) {
		struct block_device candidate;
		fat_dir_entry_t entry;
		if (!block_device_get(i, &candidate) ||
		    !fat16_find_entry_by_name(&candidate, required_file, &entry))
			continue;
		*out_device = candidate;
		return true;
	}
	return false;
}

static bool multiboot_cmdline_is(const void *multiboot_info,
                                 size_t multiboot_info_size,
                                 const char *expected)
{
	if (multiboot_info == nullptr || expected == nullptr ||
	    multiboot_info_size < MULTIBOOT_INFO_HEADER_SIZE)
		return false;
	uint32_t announced_size;
	memcpy(&announced_size, multiboot_info, sizeof(announced_size));
	if (announced_size < MULTIBOOT_INFO_HEADER_SIZE || announced_size > multiboot_info_size)
		return false;
	const uint8_t *info = multiboot_info;
	const uint8_t *end = info + announced_size;
	const struct multiboot_tag *tag =
		(const struct multiboot_tag *)(info + MULTIBOOT_INFO_HEADER_SIZE);
	while ((uintptr_t)tag < (uintptr_t)end) {
		if (tag->type == MULTIBOOT_TAG_TYPE_END)
			return false;
		if (tag->type == MULTIBOOT_TAG_TYPE_CMDLINE &&
		    tag->size >= sizeof(struct multiboot_tag_string)) {
			const struct multiboot_tag_string *cmdline =
				(const struct multiboot_tag_string *)tag;
			size_t available = tag->size - sizeof(*cmdline);
			size_t expected_size = strlen(expected) + 1;
			if (available == expected_size &&
			    memcmp(cmdline->string, expected, expected_size) == 0)
				return true;
		}
		uint32_t aligned_size = (tag->size + MULTIBOOT_TAG_ALIGN - 1) &
			~(MULTIBOOT_TAG_ALIGN - 1);
		if (aligned_size < tag->size || (uintptr_t)end - (uintptr_t)tag < aligned_size)
			return false;
		tag = (const struct multiboot_tag *)((const uint8_t *)tag + aligned_size);
	}
	return false;
}

static void run_pingpong_test(void)
{
	struct block_device device;
	if (!find_application_disk("pingpong", &device))
		return;
	struct process_exec_result server;
	if (!process_load_block_device_app(&device, "pingpong", &server))
		return;

	int32_t endpoint = ipc_endpoint_create_for(server.process);
	char endpoint_text[12];
	if (endpoint < 0 || u32_to_decimal((uint32_t)endpoint, endpoint_text,
	                                   sizeof(endpoint_text)) == 0)
		panic("Failed to create pingpong endpoint\n");

	const char *server_argv[] = { "pingpong", "server", endpoint_text, nullptr };
	if (!process_start(server.process, server.entry, 3, server_argv) ||
	    !process_initial_context(server.process, &server.context))
		panic("Failed to start pingpong server\n");

	const char *client0_argv[] = {
		"pingpong", "client", endpoint_text, "20", "0", nullptr
	};
	const char *client1_argv[] = {
		"pingpong", "client", endpoint_text, "20", "1", nullptr
	};
	struct process_exec_result client0;
	struct process_exec_result client1;
	if (!process_exec_block_device_app(&device, "pingpong", 5, client0_argv, &client0) ||
	    !process_exec_block_device_app(&device, "pingpong", 5, client1_argv, &client1) ||
	    !ipc_grant_process((uint32_t)endpoint, process_pid(client0.process),
	                       JANOS_IPC_RIGHT_SEND) ||
	    !ipc_grant_process((uint32_t)endpoint, process_pid(client1.process),
	                       JANOS_IPC_RIGHT_SEND))
		panic("Failed to start pingpong clients\n");

	size_t cpu_count = 0;
	(void)smp_get_cpus(&cpu_count);
	uint8_t client1_cpu = cpu_count > 3 ? 3 : (cpu_count > 1 ? 1 : 0);
	if (!scheduler_set_affinity(server.process, 0) ||
	    !scheduler_set_affinity(client0.process, 0) ||
	    !scheduler_set_affinity(client1.process, client1_cpu))
		panic("Failed to assign pingpong CPU affinity\n");

	kprintf("PINGPONG_START endpoint=%u server=%u client0=%u client1=%u\n",
	         (unsigned)endpoint, (unsigned)process_pid(server.process),
	         (unsigned)process_pid(client0.process),
	         (unsigned)process_pid(client1.process));
	kprintf("PINGPONG_AFFINITY server=0 client0=0 client1=%u\n",
	         (unsigned)client1_cpu);

	__asm__ volatile("sti");
	i386_context_enter_user(&server.context);
}

static void run_framebuffer_test(const void *multiboot_info, size_t multiboot_info_size)
{
	struct block_device device;
	if (!find_application_disk("fbserver", &device))
		return;

	struct process_exec_result server;
	if (!process_load_block_device_app(&device, "fbserver", &server))
		panic("Failed to load framebuffer server\n");
	int32_t capability = framebuffer_capability_create_from_multiboot(
		multiboot_info, multiboot_info_size, server.process);
	if (capability < 0)
		panic("Failed to create framebuffer capability\n");
	struct janos_framebuffer_info framebuffer;
	if (!framebuffer_capability_grant((uint32_t)capability, server.process,
		&framebuffer))
		panic("Failed to grant framebuffer capability\n");
	void *font_address = nullptr;
	size_t font_size;
	if (!process_map_block_device_file(&device, "janos.psf", server.process,
		VMM_ENTRY_USER_SUPER_BIT, &font_address, &font_size))
		panic("Failed to map PSF2 file\n");

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
		u32_to_decimal((uint32_t)endpoint_result, endpoint_text,
		sizeof(endpoint_text)) == 0 ||
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
		panic("Failed to format framebuffer arguments\n");
	uint32_t endpoint = (uint32_t)endpoint_result;
	const char *server_argv[] = {
		"fbserver", "server", endpoint_text, framebuffer_address_text,
		framebuffer_size_text, pitch_text, width_text, height_text, bpp_text,
		type_text, font_address_text, font_size_text, nullptr,
	};
	if (!process_start(server.process, server.entry, 12, server_argv) ||
		!process_initial_context(server.process, &server.context))
		panic("Failed to start framebuffer server\n");

	const char *unavailable_argv[] = { "fbclient", "unavailable", nullptr };
	struct process_exec_result unavailable;
	if (!process_exec_block_device_app(&device, "fbclient", 2, unavailable_argv,
		&unavailable))
		panic("Failed to start unavailable framebuffer client\n");
	const char *client_argv[] = { "fbclient", "client", endpoint_text, "test", nullptr };
	struct process_exec_result client;
	if (!process_exec_block_device_app(&device, "fbclient", 4, client_argv, &client) ||
		!ipc_grant_process(endpoint, process_pid(client.process), JANOS_IPC_RIGHT_SEND))
		panic("Failed to start framebuffer client\n");

	size_t cpu_count = 0;
	(void)smp_get_cpus(&cpu_count);
	uint8_t client_cpu = cpu_count > 3 ? 3 : (cpu_count > 1 ? 1 : 0);
	uint8_t unavailable_cpu = cpu_count > 2 ? 2 : (cpu_count > 1 ? 1 : 0);
	if (!scheduler_set_affinity(server.process, 0) ||
		!scheduler_set_affinity(client.process, client_cpu) ||
		!scheduler_set_affinity(unavailable.process, unavailable_cpu))
		panic("Failed to assign framebuffer CPU affinity\n");
	const fatptr_t *server_pd = process_page_directory(server.process);
	const fatptr_t *client_pd = process_page_directory(client.process);
	bool distinct = server_pd != nullptr && client_pd != nullptr &&
		server_pd->ptr != client_pd->ptr;
	kprintf("FBTEST_START cap=%u endpoint=%u server=%u client=%u unavailable=%u\n",
		(unsigned)capability, (unsigned)endpoint, (unsigned)process_pid(server.process),
		(unsigned)process_pid(client.process), (unsigned)process_pid(unavailable.process));
	kprintf("FBTEST_SPACE server_pd=%x client_pd=%x distinct=%u\n",
		server_pd == nullptr ? 0u : (unsigned)(uintptr_t)server_pd->ptr,
		client_pd == nullptr ? 0u : (unsigned)(uintptr_t)client_pd->ptr,
		(unsigned)distinct);
	if (!distinct)
		panic("Framebuffer server and client share a page directory\n");
	__asm__ volatile("sti");
	i386_context_enter_user(&server.context);
}

void kernel_test_boot(const void *multiboot_info, size_t multiboot_info_size)
{
	if (multiboot_cmdline_is(multiboot_info, multiboot_info_size, "pingpong")) {
		run_pingpong_test();
		return;
	}
	if (multiboot_cmdline_is(multiboot_info, multiboot_info_size, "framebuffer"))
		run_framebuffer_test(multiboot_info, multiboot_info_size);
}
