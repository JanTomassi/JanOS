#pragma once

#include <stdint.h>
#include <janos/syscall.h>

/*
 * User-space wrappers for JanOS capability-based IPC.
 *
 * An endpoint handle is a process capability, not a global name.  The owner
 * receives all rights initially and can grant selected rights to other live
 * processes.  A message must contain exactly one message-kind flag and a
 * payload length no greater than JANOS_IPC_PAYLOAD_SIZE.
 *
 * Timeout arguments are measured in scheduler ticks.  A timeout of zero makes
 * an operation nonblocking; JANOS_IPC_TIMEOUT_INFINITE waits without a
 * deadline.  A blocked operation returns -JANOS_EAGAIN when its deadline
 * expires.  Functions return a non-negative request ID or zero on success,
 * depending on the operation, and return negative -JANOS_* values on error.
 */

/**
 * Create an endpoint owned by the calling process.
 *
 * The new owner initially has SEND, RECEIVE, REPLY, and NOTIFY rights.  The
 * endpoint queue is empty and bounded by JANOS_IPC_QUEUE_SIZE messages.  The
 * current ABI reserves `flags`, so it must be zero.
 *
 * @param flags Reserved endpoint-creation flags; pass zero.
 * @return A positive generation-protected endpoint handle, or -JANOS_EINVAL,
 *         -JANOS_ENOMEM, or another negative error.
 */
int32_t janos_ipc_endpoint_create(uint32_t flags);

/**
 * Enqueue a request and optionally wait for its acknowledgement.
 *
 * The message must have JANOS_IPC_REQUEST set.  The kernel copies it before
 * returning, replaces its request_id and sender fields, and returns the
 * request ID immediately when `timeout` is zero.  With a nonzero timeout, the
 * caller waits for queue space if necessary and then waits for the endpoint
 * owner to reply; the reply is deliberately discarded and the function
 * returns zero after successful completion.  A full queue causes
 * -JANOS_EAGAIN for a nonblocking call.
 *
 * @param endpoint Endpoint handle with SEND right.
 * @param message Request message to copy into the endpoint queue.
 * @param timeout Maximum wait in scheduler ticks, zero for nonblocking, or
 *                JANOS_IPC_TIMEOUT_INFINITE for no deadline.
 * @return A positive request ID for a nonblocking enqueue, zero after a
 *         reply acknowledges a timed call, or a negative -JANOS_* error.
 */
int32_t janos_ipc_send(uint32_t endpoint, const struct janos_ipc_message *message,
                       uint32_t timeout);

/**
 * Perform a synchronous request/reply transaction.
 *
 * This is the request/reply form of janos_ipc_send().  The request is queued,
 * the caller waits for the owner to reply, and the complete reply message is
 * copied to `reply`.  The reply must have JANOS_IPC_REPLY set and must echo the
 * request's request_id.  A zero timeout is invalid because a call always
 * needs time to receive its reply.
 *
 * @param endpoint Endpoint handle with SEND right.
 * @param message Request message to copy into the endpoint queue.
 * @param reply Writable buffer receiving the reply message.
 * @param timeout Maximum wait in scheduler ticks, or
 *                JANOS_IPC_TIMEOUT_INFINITE for no deadline.
 * @return Zero after the reply has been copied, or a negative -JANOS_* error.
 */
int32_t janos_ipc_call(uint32_t endpoint, const struct janos_ipc_message *message,
                       struct janos_ipc_message *reply, uint32_t timeout);

/**
 * Receive the next queued request or notification from an endpoint.
 *
 * On success the complete message is copied to `message`, and the same
 * kernel-assigned request ID is returned.  With a zero timeout an empty queue
 * returns -JANOS_ENOMSG; a nonzero timeout blocks until a message arrives or
 * the deadline expires with -JANOS_EAGAIN.  Receiving is restricted to the
 * endpoint owner by the current ABI.
 *
 * @param endpoint Endpoint handle with RECEIVE right and endpoint ownership.
 * @param message Writable destination for the received message.
 * @param timeout Maximum wait in scheduler ticks, zero for nonblocking, or
 *                JANOS_IPC_TIMEOUT_INFINITE for no deadline.
 * @return The received request ID, or a negative -JANOS_* error.
 */
int32_t janos_ipc_receive(uint32_t endpoint, struct janos_ipc_message *message,
                          uint32_t timeout);

/**
 * Reply to a request previously returned by janos_ipc_receive().
 *
 * The reply's flags must be JANOS_IPC_REPLY and its header.request_id must
 * equal `request_id`.  A successful reply wakes the original sender; a caller
 * using janos_ipc_call() also receives a copy in its reply buffer.
 *
 * @param endpoint Endpoint on which the request was received.
 * @param request_id Request ID returned by janos_ipc_receive().
 * @param message Reply message whose request_id matches `request_id`.
 * @return Zero when the pending request is completed, or a negative
 *         -JANOS_* error.
 */
int32_t janos_ipc_reply(uint32_t endpoint, uint32_t request_id,
                        const struct janos_ipc_message *message);

/**
 * Queue an asynchronous notification containing one 32-bit value.
 *
 * The kernel creates a JANOS_IPC_NOTIFICATION message with `type` in its
 * header and stores `value` in the first four bytes of its payload.  No reply
 * is expected.  The operation is nonblocking and fails with -JANOS_EAGAIN if
 * the endpoint queue is full.
 *
 * @param endpoint Endpoint handle with NOTIFY right.
 * @param type Application-defined notification type.
 * @param value Notification value delivered in the message payload.
 * @return A positive notification request ID, or a negative -JANOS_* error.
 */
int32_t janos_ipc_notify(uint32_t endpoint, uint32_t type, uint32_t value);

/**
 * Grant or replace endpoint rights for another live process.
 *
 * Only the endpoint owner may change capabilities.  `rights` must contain at
 * least one supported JANOS_IPC_RIGHT_* bit; passing zero is not a revoke
 * operation.  Closing the endpoint or terminating the target process removes
 * its capability.
 *
 * @param endpoint Endpoint owned by the calling process.
 * @param pid Existing process that receives the capability.
 * @param rights OR-combination of SEND, RECEIVE, REPLY, and NOTIFY rights.
 * @return Zero when the capability is granted, or a negative -JANOS_* error.
 */
int32_t janos_ipc_grant(uint32_t endpoint, uint32_t pid, uint32_t rights);

/**
 * Cancel an outstanding request identified by its endpoint-local request ID.
 *
 * The request sender (with SEND right) or the endpoint owner may cancel a
 * pending request.  A waiting sender is completed with -JANOS_EAGAIN and no
 * reply is generated.  Cancellation does not retroactively alter a message
 * that has already been delivered to a receiver.
 *
 * @param endpoint Endpoint containing the request.
 * @param request_id Request ID returned by janos_ipc_send() or
 *                   janos_ipc_receive() for the request being cancelled.
 * @return Zero when a pending request is cancelled, or a negative
 *         -JANOS_* error.
 */
int32_t janos_ipc_cancel(uint32_t endpoint, uint32_t request_id);

/**
 * Close an endpoint owned by the calling process.
 *
 * The handle and all delegated capabilities become invalid.  Queued messages
 * are discarded and blocked senders, receivers, and callers are woken with
 * an error.  Any later use of the old handle returns -JANOS_EBADF.
 *
 * @param endpoint Endpoint handle owned by the calling process.
 * @return Zero when closed, or a negative -JANOS_* error.
 */
int32_t janos_ipc_close(uint32_t endpoint);
