#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <janos/framebuffer.h>

struct multiboot_tag_framebuffer_common;
struct process;

/* A capability owns the right to map one bootloader framebuffer. */
int32_t framebuffer_capability_create(
	const struct multiboot_tag_framebuffer_common *framebuffer,
	const struct process *owner);
int32_t framebuffer_capability_create_from_multiboot(
	const void *multiboot_info, size_t multiboot_info_size,
	const struct process *owner);

/* Grant the capability to its owner and map it in that process only. */
bool framebuffer_capability_grant(uint32_t capability,
	struct process *process, struct janos_framebuffer_info *info);
void framebuffer_capability_revoke_process(const struct process *process);
