#pragma once

#include <stdbool.h>
#include <stddef.h>

struct address_space;
struct process_stack;

#define PROCESS_DEFAULT_KERNEL_STACK_SIZE (16 * 1024)
#define PROCESS_DEFAULT_USER_STACK_SIZE (16 * 1024)

struct process_stack *process_kernel_stack_create(size_t size);
struct process_stack *process_user_stack_create(struct address_space *space,
                                                size_t size);
void process_stack_destroy(struct process_stack *stack);

void *process_stack_base(const struct process_stack *stack);
void *process_stack_top(const struct process_stack *stack);
size_t process_stack_size(const struct process_stack *stack);
bool process_stack_is_user(const struct process_stack *stack);

/* Reserve the conventional downward-growing argv area in a user stack. */
bool process_user_stack_layout(struct process_stack *stack, int argc,
                               const char *const argv[], void **user_sp);
