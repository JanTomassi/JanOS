#include "test.h"

#include <kernel/syscall.h>

struct handler_context {
	unsigned int calls;
	uint32_t argument;
	int32_t result;
};

static int32_t handler(syscall_frame *frame, void *context)
{
	struct handler_context *state = context;
	++state->calls;
	state->argument = frame->ebx;
	return state->result;
}

static int32_t blocked_handler(syscall_frame *frame, void *context)
{
	(void)context;
	frame->eax = 0xfeed;
	return -SYSCALL_EIPC_BLOCKED;
}

static void test_registration(void)
{
	struct handler_context state = { .result = 37 };
	syscall_init();
	TEST_ASSERT(!syscall_register(JANOS_SYS_MAX, handler, &state));
	TEST_ASSERT(!syscall_register(0, NULL, &state));
	TEST_ASSERT(syscall_register(0, handler, &state));
	TEST_ASSERT(!syscall_register(0, handler, &state));

	syscall_frame frame = { .eax = 0, .ebx = 0x1234 };
	TEST_ASSERT(syscall_dispatch(&frame) == 37);
	TEST_ASSERT(frame.eax == 37 && state.calls == 1 && state.argument == 0x1234);

	frame = (syscall_frame){ .eax = JANOS_SYS_MAX };
	TEST_ASSERT(syscall_dispatch(&frame) == -SYSCALL_ENOSYS);
	TEST_ASSERT(frame.eax == (uint32_t)-SYSCALL_ENOSYS);
	TEST_ASSERT(syscall_dispatch(NULL) == -SYSCALL_ENOSYS);

	syscall_init();
	frame = (syscall_frame){ .eax = 0, .ebx = 9 };
	TEST_ASSERT(syscall_dispatch(&frame) == -SYSCALL_ENOSYS);
}

static void test_result_and_blocked_dispatch(void)
{
	struct handler_context state = { .result = -19 };
	syscall_init();
	TEST_ASSERT(syscall_register(1, handler, &state));
	syscall_frame frame = { .eax = 1 };
	TEST_ASSERT(syscall_dispatch(&frame) == -19);
	TEST_ASSERT(frame.eax == (uint32_t)-19);

	syscall_init();
	TEST_ASSERT(syscall_register(2, blocked_handler, NULL));
	frame = (syscall_frame){ .eax = 2 };
	TEST_ASSERT(syscall_dispatch(&frame) == -SYSCALL_EIPC_BLOCKED);
	TEST_ASSERT(frame.eax == 0xfeed);
}

int main(void)
{
	test_registration();
	test_result_and_blocked_dispatch();
	return 0;
}
