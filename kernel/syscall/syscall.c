#include <kernel/syscall.h>

#define SYSCALL_ENOSYS 38

struct syscall_slot {
	syscall_handler_t handler;
	void *context;
};

static struct syscall_slot slots[SYSCALL_MAX];

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
