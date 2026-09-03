#include <kernel/framebuffer.h>

#include <kernel/allocator.h>
#include <kernel/multiboot.h>
#include <kernel/process/address_space.h>
#include <kernel/process/process.h>
#include <kernel/vir_mem.h>
#include <string.h>

#define FRAMEBUFFER_CAPABILITY_LIMIT 4u

struct framebuffer_capability {
	bool used;
	uint16_t generation;
	process_pid_t owner;
	process_pid_t grantee;
	uintptr_t physical_address;
	size_t physical_size;
	size_t physical_offset;
	struct address_space *space;
	void *mapped_base;
	struct janos_framebuffer_info info;
};

static struct framebuffer_capability capabilities[FRAMEBUFFER_CAPABILITY_LIMIT];

static uint32_t capability_handle(size_t slot, uint16_t generation)
{
	return ((uint32_t)generation << 16) | (uint32_t)(slot + 1);
}

static struct framebuffer_capability *lookup_capability(uint32_t handle)
{
	uint32_t slot = (handle & 0xffffu) - 1u;
	uint16_t generation = (uint16_t)(handle >> 16);
	if (slot >= FRAMEBUFFER_CAPABILITY_LIMIT || generation == 0)
		return nullptr;
	struct framebuffer_capability *capability = &capabilities[slot];
	return capability->used && capability->generation == generation ? capability : nullptr;
}

static bool framebuffer_range(const struct multiboot_tag_framebuffer_common *framebuffer,
	                           uintptr_t *physical, size_t *size, size_t *offset)
{
	if (framebuffer == nullptr || physical == nullptr || size == nullptr || offset == nullptr ||
	    framebuffer->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB ||
	    (framebuffer->framebuffer_bpp != 24 && framebuffer->framebuffer_bpp != 32) ||
	    framebuffer->framebuffer_pitch == 0 || framebuffer->framebuffer_width == 0 ||
	    framebuffer->framebuffer_height == 0)
		return false;

	uint64_t address = framebuffer->framebuffer_addr;
	uint64_t bytes = (uint64_t)framebuffer->framebuffer_pitch * framebuffer->framebuffer_height;
	uint64_t end = address + bytes;
	if (address > UINT32_MAX || bytes == 0 || bytes > UINT32_MAX || end < address ||
	    end > UINT64_C(0x100000000))
		return false;
	*offset = (size_t)(address & (PAGE_SIZE - 1));
	uint64_t mapped_size = ((uint64_t)*offset + bytes + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
	if (mapped_size == 0 || mapped_size > UINT32_MAX)
		return false;
	*physical = (uintptr_t)(address - *offset);
	*size = (size_t)mapped_size;
	return true;
}

int32_t framebuffer_capability_create(
	const struct multiboot_tag_framebuffer_common *framebuffer,
	const struct process *owner)
{
	if (owner == nullptr)
		return -JANOS_ESRCH;
	uintptr_t physical;
	size_t size;
	size_t offset;
	if (!framebuffer_range(framebuffer, &physical, &size, &offset))
		return -JANOS_EINVAL;
	for (size_t i = 0; i < FRAMEBUFFER_CAPABILITY_LIMIT; ++i) {
		struct framebuffer_capability *capability = &capabilities[i];
		if (capability->used)
			continue;
		uint16_t generation = capability->generation + 1u;
		if (generation == 0)
			generation = 1;
		*capability = (struct framebuffer_capability){
			.used = true,
			.generation = generation,
			.owner = process_pid(owner),
			.physical_address = physical,
			.physical_size = size,
			.physical_offset = offset,
		};
		capability->info = (struct janos_framebuffer_info){
			.size = (uint32_t)((uint64_t)framebuffer->framebuffer_pitch *
				framebuffer->framebuffer_height),
			.pitch = framebuffer->framebuffer_pitch,
			.width = framebuffer->framebuffer_width,
			.height = framebuffer->framebuffer_height,
			.bpp = framebuffer->framebuffer_bpp,
			.type = framebuffer->framebuffer_type,
		};
		return (int32_t)capability_handle(i, generation);
	}
	return -JANOS_EINVAL;
}

static bool next_tag(const uint8_t *end, const struct multiboot_tag *tag,
	                 const struct multiboot_tag **next)
{
	uintptr_t address = (uintptr_t)tag;
	if (tag == nullptr || next == nullptr || address > (uintptr_t)end ||
	    (uintptr_t)end - address < MULTIBOOT_TAG_HEADER_SIZE ||
	    tag->size < MULTIBOOT_TAG_HEADER_SIZE || (uintptr_t)end - address < tag->size)
		return false;
	uint32_t aligned = (tag->size + MULTIBOOT_TAG_ALIGN - 1) &
		~(MULTIBOOT_TAG_ALIGN - 1);
	if (aligned < tag->size || (uintptr_t)end - address < aligned)
		return false;
	*next = (const struct multiboot_tag *)(address + aligned);
	return true;
}

int32_t framebuffer_capability_create_from_multiboot(
	const void *multiboot_info, size_t multiboot_info_size,
	const struct process *owner)
{
	if (multiboot_info == nullptr || multiboot_info_size < MULTIBOOT_INFO_HEADER_SIZE ||
	    owner == nullptr)
		return -JANOS_EINVAL;
	uint32_t announced_size;
	memcpy(&announced_size, multiboot_info, sizeof(announced_size));
	if (announced_size < MULTIBOOT_INFO_HEADER_SIZE || announced_size > multiboot_info_size)
		return -JANOS_EINVAL;
	const uint8_t *info = multiboot_info;
	const uint8_t *end = info + announced_size;
	const struct multiboot_tag *tag =
		(const struct multiboot_tag *)(info + MULTIBOOT_INFO_HEADER_SIZE);
	while ((uintptr_t)tag < (uintptr_t)end) {
		if (tag->type == MULTIBOOT_TAG_TYPE_END) {
			if (tag->size != MULTIBOOT_TAG_HEADER_SIZE)
				return -JANOS_EINVAL;
			return -JANOS_EINVAL;
		}
		if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER &&
		    tag->size >= sizeof(struct multiboot_tag_framebuffer_common))
			return framebuffer_capability_create(
				(const struct multiboot_tag_framebuffer_common *)tag, owner);
		const struct multiboot_tag *next;
		if (!next_tag(end, tag, &next))
			return -JANOS_EINVAL;
		tag = next;
	}
	return -JANOS_EINVAL;
}

bool framebuffer_capability_grant(uint32_t handle, struct process *process,
                              struct janos_framebuffer_info *info)
{
	if (process == nullptr || info == nullptr)
		return false;
	struct framebuffer_capability *capability = lookup_capability(handle);
	if (capability == nullptr || capability->owner != process_pid(process) ||
	    capability->grantee != 0)
		return false;
	void *mapped = nullptr;
	if (!address_space_map_borrowed(process_address_space(process),
		capability->physical_address, capability->physical_size,
		VMM_ENTRY_USER_SUPER_BIT | VMM_ENTRY_READ_WRITE_BIT |
		VMM_ENTRY_CACHE_DISABLE_BIT, &mapped)) {
		return false;
	}
	capability->grantee = process_pid(process);
	capability->space = process_address_space(process);
	capability->mapped_base = mapped;
	capability->info.address = (uint32_t)((uintptr_t)mapped + capability->physical_offset);
	*info = capability->info;
	return true;
}

void framebuffer_capability_revoke_process(const struct process *process)
{
	if (process == nullptr)
		return;
	process_pid_t pid = process_pid(process);
	for (size_t i = 0; i < FRAMEBUFFER_CAPABILITY_LIMIT; ++i) {
		struct framebuffer_capability *capability = &capabilities[i];
		if (!capability->used || (capability->owner != pid && capability->grantee != pid))
			continue;
		struct address_space *space = capability->space;
		void *mapped = capability->mapped_base;
		capability->used = false;
		capability->grantee = 0;
		capability->space = nullptr;
		capability->mapped_base = nullptr;
		if (space != nullptr && mapped != nullptr)
			(void)address_space_unmap(space, mapped);
	}
}
