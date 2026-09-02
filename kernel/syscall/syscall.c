#include <kernel/syscall.h>

#include <kernel/process/process.h>
#include <kernel/tty.h>
#include <kernel/vir_mem.h>

#define USER_ADDRESS_LIMIT 0xc0000000u

struct syscall_slot {
	syscall_handler_t handler;
	void *context;
};

static struct syscall_slot slots[SYSCALL_MAX];

/*
 * There is currently no public page-table range query.  Keep the range below
 * the kernel split and reject arithmetic wraparound; mapped-page validation
 * must be added before arbitrary user processes are supported.
 */
static bool user_buffer(uint32_t address, uint32_t length)
{
	if (length == 0)
		return true;
	if (process_current() == nullptr || address < PAGE_SIZE ||
	    address >= USER_ADDRESS_LIMIT)
		return false;
	return (uint64_t)address + length <= USER_ADDRESS_LIMIT;
}

static int32_t syscall_read_handler(struct syscall_frame *frame, void *context)
{
	(void)context;
	if (frame->ebx != 0)
		return -SYSCALL_EBADF;
	if (frame->edx > INT32_MAX || !user_buffer(frame->ecx, frame->edx))
		return -SYSCALL_EFAULT;
	return (int32_t)console_read((char *)(uintptr_t)frame->ecx, frame->edx);
}

static int32_t syscall_write_handler(struct syscall_frame *frame, void *context)
{
	(void)context;
	if (frame->ebx != 1 && frame->ebx != 2)
		return -SYSCALL_EBADF;
	if (frame->edx > INT32_MAX || !user_buffer(frame->ecx, frame->edx))
		return -SYSCALL_EFAULT;
	return (int32_t)console_write((const char *)(uintptr_t)frame->ecx, frame->edx);
}

static int32_t syscall_exit_handler(struct syscall_frame *frame, void *context)
{
	(void)context;
	if (process_current() == nullptr)
		return -SYSCALL_ESRCH;
	process_exit_current((int32_t)frame->ebx);
	return 0;
}

void syscall_init(void)
{
	for (size_t i = 0; i < SYSCALL_MAX; ++i)
		slots[i] = (struct syscall_slot){ 0 };
}

bool syscall_register(uint32_t number, syscall_handler_t handler, void *context)
{
	if (number >= SYSCALL_MAX || handler == nullptr || slots[number].handler != nullptr)
		return false;
	slots[number] = (struct syscall_slot){ .handler = handler, .context = context };
	return true;
}

int32_t syscall_dispatch(struct syscall_frame *frame)
{
	if (frame == nullptr || frame->eax >= SYSCALL_MAX || slots[frame->eax].handler == nullptr)
		return -SYSCALL_ENOSYS;
	int32_t result = slots[frame->eax].handler(frame, slots[frame->eax].context);
	if (frame != nullptr)
		frame->eax = (uint32_t)result;
	return result;
}

bool syscall_register_console_handlers(void)
{
	if (slots[SYSCALL_READ].handler != nullptr ||
	    slots[SYSCALL_WRITE].handler != nullptr ||
	    slots[SYSCALL_EXIT].handler != nullptr)
		return false;
	return syscall_register(SYSCALL_READ, syscall_read_handler, nullptr) &&
		syscall_register(SYSCALL_WRITE, syscall_write_handler, nullptr) &&
		syscall_register(SYSCALL_EXIT, syscall_exit_handler, nullptr);
}
