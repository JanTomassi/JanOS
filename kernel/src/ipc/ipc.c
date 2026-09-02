#include <kernel/ipc.h>

#include <kernel/process/process.h>
#include <kernel/process/wait_queue.h>
#include <kernel/scheduler.h>
#include <kernel/spinlock.h>
#include <kernel/syscall.h>
#include <kernel/vir_mem.h>
#include <janos/syscall.h>
#include <string.h>

enum {
	IPC_EAGAIN = 11,
	IPC_EBUSY = 16,
	IPC_EINVAL = 22,
	IPC_ENOMEM = 12,
	IPC_ENOMSG = 42,
};

#define IPC_PENDING_LIMIT 32u

struct ipc_endpoint {
	bool used;
	uint16_t generation;
	process_pid_t owner;
	struct janos_ipc_message messages[JANOS_IPC_QUEUE_SIZE];
	size_t head;
	size_t count;
	uint32_t next_request;
	struct {
		bool used;
		uint32_t request_id;
		process_pid_t sender;
	} pending[IPC_PENDING_LIMIT];
	struct wait_queue receivers;
	struct wait_queue senders;
};

static struct ipc_endpoint endpoints[JANOS_IPC_ENDPOINT_LIMIT];
static spinlock_t ipc_lock;
static bool initialized;

static void ensure_initialized(void)
{
	if (initialized)
		return;
	spin_lock_init(&ipc_lock);
	for (size_t i = 0; i < JANOS_IPC_ENDPOINT_LIMIT; ++i)
		wait_queue_init(&endpoints[i].receivers), wait_queue_init(&endpoints[i].senders);
	initialized = true;
}

void ipc_init(void)
{
	ensure_initialized();
}

static uint32_t endpoint_handle(size_t slot, uint16_t generation)
{
	return ((uint32_t)generation << 16) | (uint32_t)(slot + 1);
}

static struct ipc_endpoint *lookup(uint32_t handle)
{
	uint32_t slot = (handle & 0xffffu) - 1u;
	uint16_t generation = (uint16_t)(handle >> 16);
	if (slot >= JANOS_IPC_ENDPOINT_LIMIT || generation == 0)
		return nullptr;
	struct ipc_endpoint *endpoint = &endpoints[slot];
	return endpoint->used && endpoint->generation == generation ? endpoint : nullptr;
}

static bool valid_message(const struct janos_ipc_message *message)
{
	return message != nullptr && message->header.length <= JANOS_IPC_PAYLOAD_SIZE &&
		(message->header.flags & ~(JANOS_IPC_REQUEST | JANOS_IPC_REPLY |
		JANOS_IPC_NOTIFICATION)) == 0 && message->header.flags != 0;
}

static int32_t endpoint_create(syscall_frame *frame, void *context)
{
	(void)context;
	struct process *current = process_current();
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	ensure_initialized();
	uint32_t flags = frame->ebx;
	if (flags != 0)
		return -IPC_EINVAL;
	uint32_t irq_flags = spin_lock_irqsave(&ipc_lock);
	for (size_t i = 0; i < JANOS_IPC_ENDPOINT_LIMIT; ++i) {
		struct ipc_endpoint *endpoint = &endpoints[i];
		if (endpoint->used)
			continue;
		uint16_t generation = endpoint->generation + 1u;
		if (generation == 0)
			generation = 1;
		*endpoint = (struct ipc_endpoint){
			.used = true, .generation = generation, .owner = process_pid(current),
			.next_request = 1,
		};
		wait_queue_init(&endpoint->receivers);
		wait_queue_init(&endpoint->senders);
		spin_unlock_irqrestore(&ipc_lock, irq_flags);
		return (int32_t)endpoint_handle(i, generation);
	}
	spin_unlock_irqrestore(&ipc_lock, irq_flags);
	return -IPC_ENOMEM;
}

static int32_t ipc_send(syscall_frame *frame, void *context)
{
	(void)context;
	struct process *current = process_current();
	struct janos_ipc_message message;
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	if (!copy_from_user(&message, (const void *)(uintptr_t)frame->ecx, sizeof(message)) ||
		!valid_message(&message))
		return -SYSCALL_EFAULT;
	if ((message.header.flags & JANOS_IPC_REQUEST) == 0)
		return -IPC_EINVAL;
	ensure_initialized();
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(frame->ebx);
	if (endpoint == nullptr) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	if (endpoint->count == JANOS_IPC_QUEUE_SIZE) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		if (frame->edx != 0)
			(void)scheduler_block_current(&endpoint->senders, frame);
		return -IPC_EAGAIN;
	}
	size_t pending_slot = IPC_PENDING_LIMIT;
	for (size_t i = 0; i < IPC_PENDING_LIMIT; ++i)
		if (!endpoint->pending[i].used) {
			pending_slot = i;
			break;
		}
	if (pending_slot == IPC_PENDING_LIMIT) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -IPC_EBUSY;
	}
	message.header.request_id = endpoint->next_request++;
	message.header.sender = process_pid(current);
	if (endpoint->next_request == 0)
		endpoint->next_request = 1;
	endpoint->messages[(endpoint->head + endpoint->count) % JANOS_IPC_QUEUE_SIZE] = message;
	endpoint->pending[pending_slot].used = true;
	endpoint->pending[pending_slot].request_id = message.header.request_id;
	endpoint->pending[pending_slot].sender = process_pid(current);
	++endpoint->count;
	spin_unlock_irqrestore(&ipc_lock, flags);
	(void)wait_queue_pop(&endpoint->receivers);
	return (int32_t)message.header.request_id;
}

static int32_t ipc_receive(syscall_frame *frame, void *context)
{
	(void)context;
	struct process *current = process_current();
	struct janos_ipc_message message;
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	if (!user_buffer(frame->ecx, sizeof(message), VMM_ENTRY_USER_SUPER_BIT | VMM_ENTRY_READ_WRITE_BIT))
		return -SYSCALL_EFAULT;
	ensure_initialized();
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(frame->ebx);
	if (endpoint == nullptr) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	if (endpoint->owner != process_pid(current)) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	if (endpoint->count == 0) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		if (frame->edx != 0)
			(void)scheduler_block_current(&endpoint->receivers, frame);
		return -IPC_ENOMSG;
	}
	message = endpoint->messages[endpoint->head];
	/* The request remains pending until the server explicitly replies. */
	endpoint->head = (endpoint->head + 1) % JANOS_IPC_QUEUE_SIZE;
	--endpoint->count;
	spin_unlock_irqrestore(&ipc_lock, flags);
	(void)wait_queue_pop(&endpoint->senders);
	if (!copy_to_user((void *)(uintptr_t)frame->ecx, &message, sizeof(message)))
		return -SYSCALL_EFAULT;
	return (int32_t)message.header.request_id;
}

static int32_t ipc_reply(syscall_frame *frame, void *context)
{
	(void)context;
	struct janos_ipc_message message;
	struct process *current = process_current();
	if (!copy_from_user(&message, (const void *)(uintptr_t)frame->edx, sizeof(message)) ||
		!valid_message(&message))
		return -SYSCALL_EFAULT;
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	if ((message.header.flags & JANOS_IPC_REPLY) == 0 || message.header.request_id != frame->ecx)
		return -IPC_EINVAL;
	ensure_initialized();
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(frame->ebx);
	if (endpoint == nullptr || endpoint->owner != process_pid(current)) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	for (size_t i = 0; i < IPC_PENDING_LIMIT; ++i) {
		if (endpoint->pending[i].used && endpoint->pending[i].request_id == frame->ecx) {
			endpoint->pending[i].used = false;
			spin_unlock_irqrestore(&ipc_lock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&ipc_lock, flags);
	return -SYSCALL_ESRCH;
}

static int32_t ipc_notify(syscall_frame *frame, void *context)
{
	(void)context;
	struct janos_ipc_message message = { .header = {
		.type = frame->ecx, .flags = JANOS_IPC_NOTIFICATION, .length = sizeof(uint32_t),
	}};
	memcpy(message.payload, &frame->edx, sizeof(uint32_t));
	struct process *current = process_current();
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	ensure_initialized();
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(frame->ebx);
	if (endpoint == nullptr) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	if (endpoint->count == JANOS_IPC_QUEUE_SIZE) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -IPC_EAGAIN;
	}
	message.header.request_id = endpoint->next_request++;
	message.header.sender = process_pid(current);
	endpoint->messages[(endpoint->head + endpoint->count) % JANOS_IPC_QUEUE_SIZE] = message;
	++endpoint->count;
	spin_unlock_irqrestore(&ipc_lock, flags);
	(void)wait_queue_pop(&endpoint->receivers);
	return (int32_t)message.header.request_id;
}

void ipc_process_cleanup(struct process *process)
{
	if (process == nullptr || !initialized)
		return;
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	for (size_t i = 0; i < JANOS_IPC_ENDPOINT_LIMIT; ++i)
		if (endpoints[i].used && endpoints[i].owner == process_pid(process))
			endpoints[i].used = false;
	spin_unlock_irqrestore(&ipc_lock, flags);
}

bool ipc_register_syscalls(void)
{
	ipc_init();
	return syscall_register(JANOS_SYS_IPC_ENDPOINT_CREATE, endpoint_create, nullptr) &&
		syscall_register(JANOS_SYS_IPC_SEND, ipc_send, nullptr) &&
		syscall_register(JANOS_SYS_IPC_RECEIVE, ipc_receive, nullptr) &&
		syscall_register(JANOS_SYS_IPC_REPLY, ipc_reply, nullptr) &&
		syscall_register(JANOS_SYS_IPC_NOTIFY, ipc_notify, nullptr);
}
