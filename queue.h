/*
 * @file queue.h
 * @brief A simple bounded FIFO queue implementation.
 *
 * This queue stores generic pointers (`void *`) and supports
 * basic thread-unsafe push and pop operations. The caller is
 * responsible for ensuring external synchronization if needed.
 *
 * The queue has a fixed maximum capacity determined at creation
 * time. Push will fail when full; pop will fail when empty.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/**
 * @typedef queue_t
 * @brief Opaque queue handle.
 *
 * The underlying structure is defined privately in queue.c.
 */
typedef struct queue queue_t;

/**
 * @brief Allocate and initialize a new bounded queue.
 *
 * @param size Maximum number of elements the queue can store.
 *             Must be > 0.
 *
 * @return Pointer to a newly-allocated queue on success.
 *         Returns NULL if allocation fails or `size` is invalid.
 */
queue_t *queue_new(int size);

/**
 * @brief Destroy a queue and free all its resources.
 *
 * After this call, the pointer referenced by `q` is set to NULL.
 *
 * @param q Pointer to a queue pointer (`queue_t **`).
 *          Must not be NULL.
 */
void queue_delete(queue_t **q);

/**
 * @brief Push an element onto the back of the queue.
 *
 * @param q    Queue instance.
 * @param elem Pointer to the element to insert.
 *
 * @return true  if the element was successfully added.
 *         false if `q` is NULL or the queue is full.
 */
bool queue_push(queue_t *q, void *elem);

/**
 * @brief Pop the element at the front of the queue.
 *
 * On success, `*elem` is set to the popped value and the queue
 * size is reduced by one.
 *
 * @param q    Queue instance.
 * @param elem Output pointer to store the removed element.
 *
 * @return true  if an element was popped successfully.
 *         false if `q` is NULL or the queue is empty.
 */
bool queue_pop(queue_t *q, void **elem);
