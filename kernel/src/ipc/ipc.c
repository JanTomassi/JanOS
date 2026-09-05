#include <kernel/ipc.h>

#include <kernel/process/process.h>
#include <kernel/process/wait_queue.h>
#include <kernel/scheduler.h>
#include <kernel/spinlock.h>
#include <kernel/syscall.h>
#include <kernel/vir_mem.h>
#include <kernel/timer.h>
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
#define IPC_CAPABILITY_LIMIT 32u

struct ipc_capability {
	bool used;
	process_pid_t process;
	uint32_t rights;
};

struct ipc_pending {
	bool used;
	uint32_t request_id;
	process_pid_t sender;
	struct process *process;
};

struct ipc_endpoint {
	bool used;
	uint16_t generation;
	process_pid_t owner;
	struct ipc_capability capabilities[IPC_CAPABILITY_LIMIT];
	struct janos_ipc_message messages[JANOS_IPC_QUEUE_SIZE];
	size_t head;
	size_t count;
	bool wake_pending;
	uint32_t next_request;
	struct ipc_pending pending[IPC_PENDING_LIMIT];
	struct wait_queue receivers;
	struct wait_queue senders;
	struct wait_queue replies;
};

static struct ipc_endpoint endpoints[JANOS_IPC_ENDPOINT_LIMIT];
static spinlock_t ipc_lock;
static bool initialized;

static bool deadline_expired(uint32_t deadline);

static int32_t completed_wait_result(struct process *process)
{
	struct i386_context context;
	if (process == nullptr || process_ipc_wait_active(process) ||
		!process_load_context(process, &context))
		return -IPC_EBUSY;
	return (int32_t)context.eax;
}

static void close_endpoint(struct ipc_endpoint *endpoint)
{
	endpoint->used = false;
	for (size_t c = 0; c < IPC_CAPABILITY_LIMIT; ++c)
		endpoint->capabilities[c].used = false;
	while (wait_queue_count(&endpoint->receivers) != 0) {
		struct process *waiter = wait_queue_pop(&endpoint->receivers);
		if (waiter == nullptr)
			break;
		(void)process_ipc_wait_complete(waiter, -SYSCALL_ESRCH, nullptr);
	}
	while (wait_queue_count(&endpoint->senders) != 0) {
		struct process *waiter = wait_queue_pop(&endpoint->senders);
		if (waiter == nullptr)
			break;
		(void)process_ipc_wait_complete(waiter, -SYSCALL_ESRCH, nullptr);
	}
	while (wait_queue_count(&endpoint->replies) != 0) {
		struct process *waiter = wait_queue_pop(&endpoint->replies);
		if (waiter == nullptr)
			break;
		(void)process_ipc_wait_complete(waiter, -SYSCALL_ESRCH, nullptr);
	}
	for (size_t p = 0; p < IPC_PENDING_LIMIT; ++p) {
		if (!endpoint->pending[p].used)
			continue;
		struct process *sender = endpoint->pending[p].process;
		endpoint->pending[p].used = false;
		if (sender != nullptr) {
			(void)process_ipc_wait_complete(sender, -SYSCALL_ESRCH, nullptr);
			(void)scheduler_wake(sender);
		}
	}
}

static void ensure_initialized(void)
{
	if (initialized)
		return;
	spin_lock_init(&ipc_lock);
	for (size_t i = 0; i < JANOS_IPC_ENDPOINT_LIMIT; ++i) {
		wait_queue_init(&endpoints[i].receivers);
		wait_queue_init(&endpoints[i].senders);
		wait_queue_init(&endpoints[i].replies);
	}
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

static bool has_right(const struct ipc_endpoint *endpoint, process_pid_t process,
	                  uint32_t right)
{
	if (endpoint == nullptr)
		return false;
	for (size_t i = 0; i < IPC_CAPABILITY_LIMIT; ++i)
		if (endpoint->capabilities[i].used &&
		    endpoint->capabilities[i].process == process)
			return (endpoint->capabilities[i].rights & right) != 0;
	return false;
}

static bool valid_message(const struct janos_ipc_message *message)
{
	uint32_t kinds;
	if (message == nullptr)
		return false;
	kinds = message->header.flags & (JANOS_IPC_REQUEST | JANOS_IPC_REPLY |
		JANOS_IPC_NOTIFICATION);
	return message != nullptr && message->header.length <= JANOS_IPC_PAYLOAD_SIZE &&
		(message->header.flags & ~(JANOS_IPC_REQUEST | JANOS_IPC_REPLY |
		JANOS_IPC_NOTIFICATION)) == 0 && kinds != 0 && (kinds & (kinds - 1)) == 0;
}

static void deliver_waiting_receiver(struct ipc_endpoint *endpoint)
{
	struct process *receiver = nullptr;
	bool completed = false;
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	if (endpoint->receivers.waiters.next == &endpoint->receivers.waiters)
		goto out;
	if (endpoint->count == 0)
		goto out;
	receiver = process_from_wait_link(endpoint->receivers.waiters.next);
	if (receiver == nullptr)
		goto out;
	struct janos_ipc_message message = endpoint->messages[endpoint->head];
	if (!process_wait_detach(receiver, &endpoint->receivers)) {
		receiver = nullptr;
		goto out;
	}
	endpoint->head = (endpoint->head + 1) % JANOS_IPC_QUEUE_SIZE;
	--endpoint->count;
	completed = process_ipc_wait_complete(receiver,
	                                      (int32_t)message.header.request_id,
	                                      &message);
out:
	spin_unlock_irqrestore(&ipc_lock, flags);
	if (receiver != nullptr && completed)
		if (process_set_state(receiver, PROCESS_READY))
			scheduler_process_ready(receiver);
	return;
}

void ipc_wake_receiver(uint32_t handle)
{
	struct process *receiver = nullptr;
	bool completed = false;
	if (!initialized)
		return;
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(handle);
	if (endpoint == nullptr)
		goto out;
	if (endpoint->receivers.waiters.next == &endpoint->receivers.waiters) {
		/* Preserve the wake if the receiver has not blocked yet. */
		if (endpoint->count == 0)
			endpoint->wake_pending = true;
		goto out;
	}
	receiver = process_from_wait_link(endpoint->receivers.waiters.next);
	if (receiver == nullptr || !process_wait_detach(receiver, &endpoint->receivers)) {
		receiver = nullptr;
		goto out;
	}
	completed = process_ipc_wait_complete(receiver, -IPC_EAGAIN, nullptr);
out:
	spin_unlock_irqrestore(&ipc_lock, flags);
	if (receiver != nullptr && completed && process_set_state(receiver, PROCESS_READY))
		scheduler_process_ready(receiver);
}

static bool remove_queued_message(struct ipc_endpoint *endpoint, uint32_t request_id)
{
	for (size_t position = 0; position < endpoint->count; ++position) {
		size_t index = (endpoint->head + position) % JANOS_IPC_QUEUE_SIZE;
		if (endpoint->messages[index].header.request_id != request_id)
			continue;
		for (size_t next = position; next + 1 < endpoint->count; ++next) {
			size_t from = (endpoint->head + next + 1) % JANOS_IPC_QUEUE_SIZE;
			size_t to = (endpoint->head + next) % JANOS_IPC_QUEUE_SIZE;
			endpoint->messages[to] = endpoint->messages[from];
		}
		--endpoint->count;
		return true;
	}
	return false;
}

static int32_t endpoint_create(syscall_frame *frame, void *context)
{
	(void)context;
	struct process *current = process_current();
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	uint32_t flags = frame->ebx;
	if (flags != 0)
		return -IPC_EINVAL;
	return ipc_endpoint_create_for(current);
}

int32_t ipc_endpoint_create_for(struct process *owner)
{
	if (owner == nullptr)
		return -SYSCALL_ESRCH;
	ensure_initialized();
	uint32_t irq_flags = spin_lock_irqsave(&ipc_lock);
	for (size_t i = 0; i < JANOS_IPC_ENDPOINT_LIMIT; ++i) {
		struct ipc_endpoint *endpoint = &endpoints[i];
		if (endpoint->used)
			continue;
		uint16_t generation = endpoint->generation + 1u;
		if (generation == 0)
			generation = 1;
		*endpoint = (struct ipc_endpoint){
			.used = true, .generation = generation, .owner = process_pid(owner),
			.next_request = 1,
		};
		endpoint->capabilities[0] = (struct ipc_capability){
			.used = true, .process = process_pid(owner),
			.rights = JANOS_IPC_RIGHT_SEND | JANOS_IPC_RIGHT_RECEIVE |
				JANOS_IPC_RIGHT_REPLY | JANOS_IPC_RIGHT_NOTIFY,
		};
		wait_queue_init(&endpoint->receivers);
		wait_queue_init(&endpoint->senders);
		wait_queue_init(&endpoint->replies);
		spin_unlock_irqrestore(&ipc_lock, irq_flags);
		return (int32_t)endpoint_handle(i, generation);
	}
	spin_unlock_irqrestore(&ipc_lock, irq_flags);
	return -IPC_ENOMEM;
}

bool ipc_kernel_send(uint32_t handle, const struct janos_ipc_message *message)
{
	if (message == nullptr || !valid_message(message) ||
	    (message->header.flags & JANOS_IPC_REQUEST) == 0)
		return false;
	ensure_initialized();
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(handle);
	if (endpoint == nullptr || endpoint->count == JANOS_IPC_QUEUE_SIZE) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return false;
	}
	size_t pending_slot = IPC_PENDING_LIMIT;
	for (size_t i = 0; i < IPC_PENDING_LIMIT; ++i)
		if (!endpoint->pending[i].used) {
			pending_slot = i;
			break;
		}
	if (pending_slot == IPC_PENDING_LIMIT) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return false;
	}
	struct janos_ipc_message queued = *message;
	queued.header.request_id = endpoint->next_request++;
	queued.header.sender = 0;
	if (endpoint->next_request == 0)
		endpoint->next_request = 1;
	endpoint->messages[(endpoint->head + endpoint->count) % JANOS_IPC_QUEUE_SIZE] = queued;
	endpoint->pending[pending_slot] = (struct ipc_pending){
		.used = true, .request_id = queued.header.request_id,
		.sender = 0, .process = nullptr,
	};
	++endpoint->count;
	spin_unlock_irqrestore(&ipc_lock, flags);
	deliver_waiting_receiver(endpoint);
	return true;
}

bool ipc_kernel_notify(uint32_t handle, uint32_t type, uint32_t value)
{
	struct janos_ipc_message message = { .header = {
		.type = type,
		.flags = JANOS_IPC_NOTIFICATION,
		.length = sizeof(value),
	} };
	memcpy(message.payload, &value, sizeof(value));
	ensure_initialized();
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(handle);
	if (endpoint == nullptr || endpoint->count == JANOS_IPC_QUEUE_SIZE) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return false;
	}
	message.header.request_id = endpoint->next_request++;
	message.header.sender = 0;
	if (endpoint->next_request == 0)
		endpoint->next_request = 1;
	endpoint->messages[(endpoint->head + endpoint->count) % JANOS_IPC_QUEUE_SIZE] = message;
	++endpoint->count;
	spin_unlock_irqrestore(&ipc_lock, flags);
	/* Wake without copying into a user address from interrupt context. */
	ipc_wake_receiver(handle);
	return true;
}

static int32_t ipc_send(syscall_frame *frame, void *context)
{
	(void)context;
	struct process *current = process_current();
	struct janos_ipc_message message;
	bool call = frame->eax == JANOS_SYS_IPC_CALL;
	uint32_t timeout = call ? frame->esi : frame->edx;
	uintptr_t reply_buffer = call ? frame->edx : 0;
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	if (!copy_from_user(&message, (const void *)(uintptr_t)frame->ecx, sizeof(message)) ||
		!valid_message(&message))
		return -SYSCALL_EFAULT;
	if ((message.header.flags & JANOS_IPC_REQUEST) == 0)
		return -IPC_EINVAL;
	if (call && (timeout == 0 || !user_buffer(reply_buffer, sizeof(message),
		VMM_ENTRY_USER_SUPER_BIT | VMM_ENTRY_READ_WRITE_BIT)))
		return timeout == 0 ? -IPC_EINVAL : -SYSCALL_EFAULT;
	ensure_initialized();
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(frame->ebx);
	if (endpoint == nullptr || !has_right(endpoint, process_pid(current),
		JANOS_IPC_RIGHT_SEND)) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	if (endpoint->count == JANOS_IPC_QUEUE_SIZE) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		/* A nonblocking call must never alter scheduler state. */
		if (timeout != 0) {
			uint32_t deadline = timeout == JANOS_IPC_TIMEOUT_INFINITE ?
				JANOS_IPC_TIMEOUT_INFINITE : (uint32_t)GLOBAL_TICK + timeout;
			if (!process_ipc_wait_begin(current, call ? JANOS_SYS_IPC_CALL : JANOS_SYS_IPC_SEND,
				reply_buffer, deadline) ||
			    !process_ipc_wait_set_message(current, frame->ebx, &message))
				return -IPC_EBUSY;
			if (scheduler_block_current(&endpoint->senders, frame))
				return -SYSCALL_EIPC_BLOCKED;
			if (!process_ipc_wait_active(current))
				return completed_wait_result(current);
		}
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
	if (timeout != 0) {
		uint32_t deadline = timeout == JANOS_IPC_TIMEOUT_INFINITE ?
			JANOS_IPC_TIMEOUT_INFINITE : (uint32_t)GLOBAL_TICK + timeout;
		if (!process_ipc_wait_begin(current, call ? JANOS_SYS_IPC_CALL : JANOS_SYS_IPC_SEND,
			reply_buffer, deadline) ||
		    !process_block(current, &endpoint->replies)) {
			(void)process_ipc_wait_cancel(current);
			spin_unlock_irqrestore(&ipc_lock, flags);
			return -IPC_EBUSY;
		}
	}
	message.header.request_id = endpoint->next_request++;
	message.header.sender = process_pid(current);
	if (endpoint->next_request == 0)
		endpoint->next_request = 1;
	endpoint->messages[(endpoint->head + endpoint->count) % JANOS_IPC_QUEUE_SIZE] = message;
	endpoint->pending[pending_slot].used = true;
	endpoint->pending[pending_slot].request_id = message.header.request_id;
	endpoint->pending[pending_slot].sender = process_pid(current);
	endpoint->pending[pending_slot].process = current;
	++endpoint->count;
	spin_unlock_irqrestore(&ipc_lock, flags);
	deliver_waiting_receiver(endpoint);
	if (timeout != 0) {
		if (scheduler_block_current(&endpoint->replies, frame))
			return -SYSCALL_EIPC_BLOCKED;
		if (!process_ipc_wait_active(current))
			return completed_wait_result(current);
		return -IPC_EAGAIN;
	}
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
	if (endpoint->owner != process_pid(current) || !has_right(endpoint,
		process_pid(current), JANOS_IPC_RIGHT_RECEIVE)) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	if (endpoint->count == 0) {
		if (endpoint->wake_pending) {
			endpoint->wake_pending = false;
			spin_unlock_irqrestore(&ipc_lock, flags);
			return -IPC_EAGAIN;
		}
		if (frame->edx != 0) {
			uint32_t deadline = frame->edx == JANOS_IPC_TIMEOUT_INFINITE ?
				JANOS_IPC_TIMEOUT_INFINITE : (uint32_t)GLOBAL_TICK + frame->edx;
			if (!process_ipc_wait_begin(current, JANOS_SYS_IPC_RECEIVE, frame->ecx,
				deadline) || !process_block(current, &endpoint->receivers)) {
				(void)process_ipc_wait_cancel(current);
				spin_unlock_irqrestore(&ipc_lock, flags);
				return -IPC_EBUSY;
			}
			spin_unlock_irqrestore(&ipc_lock, flags);
			if (scheduler_block_current(&endpoint->receivers, frame))
				return -SYSCALL_EIPC_BLOCKED;
			if (!process_ipc_wait_active(current))
				return completed_wait_result(current);
			return -IPC_ENOMSG;
		}
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -IPC_ENOMSG;
	}
	message = endpoint->messages[endpoint->head];
	/* The request remains pending until the server explicitly replies. */
	endpoint->head = (endpoint->head + 1) % JANOS_IPC_QUEUE_SIZE;
	--endpoint->count;
	spin_unlock_irqrestore(&ipc_lock, flags);
	struct process *sender = endpoint->senders.waiters.next == &endpoint->senders.waiters ?
		nullptr : process_from_wait_link(endpoint->senders.waiters.next);
	if (sender != nullptr && process_ipc_wait_active(sender)) {
		struct janos_ipc_message queued;
		uint32_t sender_endpoint;
		if (process_ipc_wait_get_message(sender, &sender_endpoint, &queued) &&
		    sender_endpoint == frame->ebx) {
			if (deadline_expired(process_ipc_wait_deadline(sender))) {
				bool detached = process_wait_detach(sender, &endpoint->senders);
				bool completed = detached &&
					process_ipc_wait_complete(sender, -IPC_EAGAIN, nullptr);
				if (completed && process_set_state(sender, PROCESS_READY))
					scheduler_process_ready(sender);
				return (int32_t)message.header.request_id;
			}
			uint32_t enqueue_flags = spin_lock_irqsave(&ipc_lock);
			size_t sender_pending_slot = IPC_PENDING_LIMIT;
			for (size_t p = 0; p < IPC_PENDING_LIMIT; ++p)
				if (!endpoint->pending[p].used) {
					sender_pending_slot = p;
					break;
				}
			if (sender_pending_slot != IPC_PENDING_LIMIT && endpoint->count < JANOS_IPC_QUEUE_SIZE) {
				queued.header.request_id = endpoint->next_request++;
				queued.header.sender = process_pid(sender);
				endpoint->messages[(endpoint->head + endpoint->count) % JANOS_IPC_QUEUE_SIZE] = queued;
				++endpoint->count;
				endpoint->pending[sender_pending_slot] = (struct ipc_pending){
					.used = true, .request_id = queued.header.request_id,
					.sender = process_pid(sender), .process = sender,
				};
				spin_unlock_irqrestore(&ipc_lock, enqueue_flags);
				(void)process_wait_requeue(sender, &endpoint->senders,
				                          &endpoint->replies);
			} else {
				spin_unlock_irqrestore(&ipc_lock, enqueue_flags);
				(void)process_ipc_wait_complete(sender, -IPC_EBUSY, nullptr);
				(void)process_wake(sender, &endpoint->senders);
			}
		}
	}
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
	if (endpoint == nullptr || endpoint->owner != process_pid(current) ||
		!has_right(endpoint, process_pid(current), JANOS_IPC_RIGHT_REPLY)) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	for (size_t i = 0; i < IPC_PENDING_LIMIT; ++i) {
		if (endpoint->pending[i].used && endpoint->pending[i].request_id == frame->ecx) {
			struct process *sender = endpoint->pending[i].process;
			endpoint->pending[i].used = false;
			(void)remove_queued_message(endpoint, frame->ecx);
			spin_unlock_irqrestore(&ipc_lock, flags);
			const struct janos_ipc_message *reply =
				process_ipc_wait_reply_buffer(sender) != 0 ? &message : nullptr;
			if (sender != nullptr && process_ipc_wait_complete(sender, 0, reply))
				(void)scheduler_wake(sender);
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
	if (endpoint == nullptr || !has_right(endpoint, process_pid(current),
		JANOS_IPC_RIGHT_NOTIFY)) {
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
	deliver_waiting_receiver(endpoint);
	return (int32_t)message.header.request_id;
}

static int32_t ipc_grant(syscall_frame *frame, void *context)
{
	(void)context;
	struct process *current = process_current();
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	if (frame->ecx == 0 || !process_exists(frame->ecx) || frame->edx == 0 ||
	    (frame->edx & ~(JANOS_IPC_RIGHT_SEND |
		JANOS_IPC_RIGHT_RECEIVE | JANOS_IPC_RIGHT_REPLY | JANOS_IPC_RIGHT_NOTIFY)) != 0)
		return -IPC_EINVAL;
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(frame->ebx);
	bool owner = endpoint != nullptr && endpoint->owner == process_pid(current);
	spin_unlock_irqrestore(&ipc_lock, flags);
	if (!owner || !ipc_grant_process(frame->ebx, frame->ecx, frame->edx))
		return -SYSCALL_EBADF;
	return 0;
}

bool ipc_grant_process(uint32_t handle, uint32_t process, uint32_t rights)
{
	if (process == 0 || !process_exists(process) || rights == 0 ||
	    (rights & ~(JANOS_IPC_RIGHT_SEND | JANOS_IPC_RIGHT_RECEIVE |
	    JANOS_IPC_RIGHT_REPLY | JANOS_IPC_RIGHT_NOTIFY)) != 0)
		return false;
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(handle);
	if (endpoint == nullptr) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return false;
	}
	/* The syscall wrapper checks ownership; this helper is for the trusted
	 * process manager and therefore only grants an existing target process. */
	for (size_t i = 0; i < IPC_CAPABILITY_LIMIT; ++i) {
		if (endpoint->capabilities[i].used && endpoint->capabilities[i].process == process) {
			endpoint->capabilities[i].rights = rights;
			spin_unlock_irqrestore(&ipc_lock, flags);
			return true;
		}
	}
	for (size_t i = 0; i < IPC_CAPABILITY_LIMIT; ++i) {
		if (!endpoint->capabilities[i].used) {
			endpoint->capabilities[i] = (struct ipc_capability){
				.used = true, .process = process, .rights = rights,
			};
			spin_unlock_irqrestore(&ipc_lock, flags);
			return true;
		}
	}
	spin_unlock_irqrestore(&ipc_lock, flags);
	return false;
}

static int32_t ipc_cancel(syscall_frame *frame, void *context)
{
	(void)context;
	struct process *current = process_current();
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(frame->ebx);
	if (endpoint == nullptr || (!has_right(endpoint, process_pid(current),
		JANOS_IPC_RIGHT_SEND) && endpoint->owner != process_pid(current))) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	for (size_t i = 0; i < IPC_PENDING_LIMIT; ++i) {
		if (endpoint->pending[i].used && endpoint->pending[i].request_id == frame->ecx &&
		    (endpoint->pending[i].process == current ||
		     endpoint->owner == process_pid(current))) {
			struct process *sender = endpoint->pending[i].process;
			endpoint->pending[i].used = false;
			spin_unlock_irqrestore(&ipc_lock, flags);
			if (sender != nullptr && sender != current &&
			    process_ipc_wait_complete(sender, -IPC_EAGAIN, nullptr))
				(void)scheduler_wake(sender);
			return 0;
		}
	}
	spin_unlock_irqrestore(&ipc_lock, flags);
	return -SYSCALL_ESRCH;
}

static int32_t ipc_close(syscall_frame *frame, void *context)
{
	(void)context;
	struct process *current = process_current();
	if (current == nullptr)
		return -SYSCALL_ESRCH;
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	struct ipc_endpoint *endpoint = lookup(frame->ebx);
	if (endpoint == nullptr || endpoint->owner != process_pid(current)) {
		spin_unlock_irqrestore(&ipc_lock, flags);
		return -SYSCALL_EBADF;
	}
	close_endpoint(endpoint);
	spin_unlock_irqrestore(&ipc_lock, flags);
	return 0;
}

void ipc_process_cleanup(struct process *process)
{
	if (process == nullptr || !initialized)
		return;
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	for (size_t i = 0; i < JANOS_IPC_ENDPOINT_LIMIT; ++i) {
		struct ipc_endpoint *endpoint = &endpoints[i];
		if (!endpoint->used)
			continue;
		if (endpoint->owner == process_pid(process)) {
			close_endpoint(endpoint);
			continue;
		}
		for (size_t c = 0; c < IPC_CAPABILITY_LIMIT; ++c)
			if (endpoint->capabilities[c].used &&
			    endpoint->capabilities[c].process == process_pid(process))
				endpoint->capabilities[c].used = false;
		for (size_t p = 0; p < IPC_PENDING_LIMIT; ++p) {
			if (!endpoint->pending[p].used)
				continue;
			if (endpoint->pending[p].process == process) {
				endpoint->pending[p].used = false;
				continue;
			}
			if (endpoint->owner == process_pid(process)) {
				struct process *sender = endpoint->pending[p].process;
				endpoint->pending[p].used = false;
				if (sender != nullptr) {
					(void)process_ipc_wait_complete(sender, -SYSCALL_ESRCH, nullptr);
					(void)scheduler_wake(sender);
				}
			}
		}
	}
	spin_unlock_irqrestore(&ipc_lock, flags);
}

static bool deadline_expired(uint32_t deadline)
{
	return deadline != JANOS_IPC_TIMEOUT_INFINITE &&
		(int32_t)((uint32_t)GLOBAL_TICK - deadline) >= 0;
}

void ipc_tick(void)
{
	if (!initialized)
		return;
	uint32_t flags = spin_lock_irqsave(&ipc_lock);
	for (size_t i = 0; i < JANOS_IPC_ENDPOINT_LIMIT; ++i) {
		struct ipc_endpoint *endpoint = &endpoints[i];
		if (!endpoint->used)
			continue;
		for (;;) {
			struct process *expired = nullptr;
			list_for_each(&endpoint->receivers.waiters) {
				struct process *candidate = process_from_wait_link(it);
				if (deadline_expired(process_ipc_wait_deadline(candidate))) {
					expired = candidate;
					break;
				}
			}
			if (expired == nullptr)
				break;
			if (!process_wait_detach(expired, &endpoint->receivers))
				continue;
			if (process_ipc_wait_complete(expired, -IPC_EAGAIN, nullptr) &&
				process_set_state(expired, PROCESS_READY))
				scheduler_process_ready(expired);
		}
		for (;;) {
			struct process *expired = nullptr;
			list_for_each(&endpoint->replies.waiters) {
				struct process *candidate = process_from_wait_link(it);
				if (deadline_expired(process_ipc_wait_deadline(candidate))) {
					expired = candidate;
					break;
				}
			}
			if (expired == nullptr)
				break;
			for (size_t p = 0; p < IPC_PENDING_LIMIT; ++p)
				if (endpoint->pending[p].used && endpoint->pending[p].process == expired)
					endpoint->pending[p].used = false;
			if (!process_wait_detach(expired, &endpoint->replies))
				continue;
			if (process_ipc_wait_complete(expired, -IPC_EAGAIN, nullptr) &&
				process_set_state(expired, PROCESS_READY))
				scheduler_process_ready(expired);
		}
		for (;;) {
			struct process *expired = nullptr;
			list_for_each(&endpoint->senders.waiters) {
				struct process *candidate = process_from_wait_link(it);
				if (deadline_expired(process_ipc_wait_deadline(candidate))) {
					expired = candidate;
					break;
				}
			}
			if (expired == nullptr)
				break;
			if (!process_wait_detach(expired, &endpoint->senders))
				continue;
			if (process_ipc_wait_complete(expired, -IPC_EAGAIN, nullptr) &&
				process_set_state(expired, PROCESS_READY))
				scheduler_process_ready(expired);
		}
	}
	spin_unlock_irqrestore(&ipc_lock, flags);
}

bool ipc_register_syscalls(void)
{
    bool res = true;
    ipc_init();
    res &= syscall_register(JANOS_SYS_IPC_ENDPOINT_CREATE, endpoint_create, nullptr);
    res &= syscall_register(JANOS_SYS_IPC_SEND, ipc_send, nullptr);
    res &= syscall_register(JANOS_SYS_IPC_CALL, ipc_send, nullptr);
    res &= syscall_register(JANOS_SYS_IPC_RECEIVE, ipc_receive, nullptr);
    res &= syscall_register(JANOS_SYS_IPC_REPLY, ipc_reply, nullptr);
    res &= syscall_register(JANOS_SYS_IPC_NOTIFY, ipc_notify, nullptr);
    res &= syscall_register(JANOS_SYS_IPC_GRANT, ipc_grant, nullptr);
    res &= syscall_register(JANOS_SYS_IPC_CANCEL, ipc_cancel, nullptr);
    res &= syscall_register(JANOS_SYS_IPC_CLOSE, ipc_close, nullptr);
    return res;
}
