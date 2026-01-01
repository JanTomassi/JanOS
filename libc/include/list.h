#pragma once
#include <stddef.h>
#include <stdint.h>

struct list_head {
	struct list_head *next, *prev;
};

#define __same_type(X, Y) __builtin_types_compatible_p(typeof(X), typeof(Y))

#define container_of(ptr, type, member)                                                                                                          \
	({                                                                                                                                       \
		void *mptr = (void *)(ptr);                                                                                                      \
		static_assert(__same_type(*(ptr), ((type *)0)->member) || __same_type(*(ptr), void), "pointer type mismatch in container_of()"); \
		((type *)(mptr - offsetof(type, member)));                                                                                       \
	})

#define list_entry(ptr, type, list_member) container_of(ptr, type, list_member)

#define LIST_HEAD(var_name) struct list_head var_name = { .next = &(var_name), .prev = &(var_name) }

#define RESET_LIST_ITEM(list) ((list)->next = (list), (list)->prev = (list))

static inline void list_add(struct list_head *new, struct list_head *head)
{
	struct list_head *prev = (head);
	struct list_head *next = (head)->next;
	next->prev = (new);
	prev->next = (new);
	(new)->prev = prev;
	(new)->next = next;
}

static inline void list_rm(struct list_head *head)
{
	struct list_head *prev = (head)->prev;
	struct list_head *next = (head)->next;
	next->prev = prev;
	prev->next = next;
}

static inline bool list_is_first(struct list_head *ptr, struct list_head *head)
{
	return ptr->prev == head;
}

static inline bool list_is_last(struct list_head *ptr, struct list_head *head)
{
	return ptr->next == head;
}

static inline struct list_head *list_pop(struct list_head *head)
{
	struct list_head *pop = head->prev;
	return pop != head ? (list_rm(pop), RESET_LIST_ITEM(pop), pop) : nullptr;
}

#define list_pop_entry(head, type, list_member) (list_entry(list_pop(head), type, list_member))

#define list_first_entry(head, type, list_member) (list_entry((head)->next, type, list_member))

#define list_last_entry(head, type, list_member) (list_entry((head)->prev, type, list_member))

#define list_next_entry(ptr, list_member) (list_entry((ptr)->list_member.next, typeof(*(ptr)), list_member))

#define list_next_entry_circular(ptr, head, list_member) \
	(list_is_last(&(ptr)->list_member, head) ? list_first_entry(head, typeof(*(ptr)), list_member) : list_next_entry(ptr, list_member))

#define list_prev_entry(ptr, list_member) (list_entry((ptr)->list_member.prev, typeof(*(ptr)), list_member))

#define list_prev_entry_circular(ptr, head, list_member) \
	(list_is_first(&(ptr)->list_member, head) ? list_last_entry(head, typeof(*(ptr)), list_member) : list_prev_entry(ptr, list_member))

#define list_for_each(head) for (struct list_head *it = (head)->next; it != (head); it = it->next)

#define list_rev_for_each(head) for (struct list_head *it = (head)->prev; it != (head); it = it->prev)
