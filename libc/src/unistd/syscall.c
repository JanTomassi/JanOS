#include <janos/syscall.h>
#include <janos/process.h>
#include <stdint.h>
#include <unistd.h>

static int32_t syscall3(uint32_t number, uint32_t arg1, uint32_t arg2,
		uint32_t arg3)
{
	int32_t result;
	__asm__ volatile("int $0x80" : "=a"(result)
		: "a"(number), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
	return result;
}

static int32_t syscall4(uint32_t number, uint32_t arg1, uint32_t arg2,
		uint32_t arg3, uint32_t arg4)
{
	int32_t result;
	__asm__ volatile("int $0x80" : "=a"(result)
		: "a"(number), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4) : "memory");
	return result;
}

ssize_t read(int fd, void *buffer, size_t length)
{
	return syscall3(JANOS_SYS_READ, (uint32_t)fd,
		(uint32_t)(uintptr_t)buffer, (uint32_t)length);
}

ssize_t write(int fd, const void *buffer, size_t length)
{
	return syscall3(JANOS_SYS_WRITE, (uint32_t)fd,
		(uint32_t)(uintptr_t)buffer, (uint32_t)length);
}

int sched_yield(void)
{
	return syscall3(JANOS_SYS_YIELD, 0, 0, 0);
}

ssize_t janos_framebuffer_read(void *buffer, size_t length)
{
	return syscall3(JANOS_SYS_FRAMEBUFFER_READ, 0,
		(uint32_t)(uintptr_t)buffer, (uint32_t)length);
}

int32_t janos_cpu_get(void)
{
	return syscall3(JANOS_SYS_CPU_GET, 0, 0, 0);
}

int32_t janos_process_snapshot(uint32_t index, struct janos_process_info *info)
{
	return syscall3(JANOS_SYS_PROCESS_SNAPSHOT, index,
		(uint32_t)(uintptr_t)info, 0);
}

int32_t janos_process_spawn(const struct janos_process_exec_request *request,
	                           struct janos_process_info *info)
{
	return syscall4(JANOS_SYS_PROCESS_SPAWN,
		(uint32_t)(uintptr_t)request, (uint32_t)(uintptr_t)info, 0, 0);
}

int32_t janos_ipc_endpoint_create(uint32_t flags)
{
	return syscall3(JANOS_SYS_IPC_ENDPOINT_CREATE, flags, 0, 0);
}

int32_t janos_ipc_send(uint32_t endpoint, const struct janos_ipc_message *message,
                       uint32_t timeout)
{
	return syscall3(JANOS_SYS_IPC_SEND, endpoint, (uint32_t)(uintptr_t)message, timeout);
}

int32_t janos_ipc_call(uint32_t endpoint, const struct janos_ipc_message *message,
                       struct janos_ipc_message *reply, uint32_t timeout)
{
	return syscall4(JANOS_SYS_IPC_CALL, endpoint, (uint32_t)(uintptr_t)message,
		(uint32_t)(uintptr_t)reply, timeout);
}

int32_t janos_ipc_receive(uint32_t endpoint, struct janos_ipc_message *message,
                          uint32_t timeout)
{
	return syscall3(JANOS_SYS_IPC_RECEIVE, endpoint, (uint32_t)(uintptr_t)message, timeout);
}

int32_t janos_ipc_reply(uint32_t endpoint, uint32_t request_id,
                        const struct janos_ipc_message *message)
{
	return syscall3(JANOS_SYS_IPC_REPLY, endpoint, request_id, (uint32_t)(uintptr_t)message);
}

int32_t janos_ipc_notify(uint32_t endpoint, uint32_t type, uint32_t value)
{
	return syscall3(JANOS_SYS_IPC_NOTIFY, endpoint, type, value);
}

int32_t janos_ipc_grant(uint32_t endpoint, uint32_t pid, uint32_t rights)
{
	return syscall3(JANOS_SYS_IPC_GRANT, endpoint, pid, rights);
}

int32_t janos_ipc_cancel(uint32_t endpoint, uint32_t request_id)
{
	return syscall3(JANOS_SYS_IPC_CANCEL, endpoint, request_id, 0);
}

int32_t janos_ipc_close(uint32_t endpoint)
{
	return syscall3(JANOS_SYS_IPC_CLOSE, endpoint, 0, 0);
}

_Noreturn void _Exit(int status)
{
	(void)syscall3(JANOS_SYS_EXIT, (uint32_t)status, 0, 0);
	__builtin_unreachable();
}
