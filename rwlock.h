/**
 * @file rwlock.h
 * @brief Simple writer-priority reader–writer lock.
 */

#pragma once

#include <pthread.h>

/**
 * @struct rwlock_t
 *
 * Opaque type for a writer-priority reader–writer lock.
 * Implementation details are in rwlock.c.
 */
typedef struct rwlock rwlock_t;

/**
 * @brief Allocate and initialize a new reader–writer lock.
 *
 * Policy: writer-priority.
 *
 * @return Pointer to initialized rwlock_t on success, or NULL on failure.
 */
rwlock_t *rwlock_new(void);

/**
 * @brief Destroy and free a reader–writer lock.
 *
 * After this call, *rw will be set to NULL.
 */
void rwlock_delete(rwlock_t **rw);

/**
 * @brief Acquire the lock for reading.
 *
 * Multiple readers may hold the lock concurrently as long as no writer
 * is active or waiting. If a writer is active or waiting, the reader
 * will block until it is safe to proceed.
 */
void reader_lock(rwlock_t *rw);

/**
 * @brief Release the lock from reading.
 */
void reader_unlock(rwlock_t *rw);

/**
 * @brief Acquire the lock for writing.
 *
 * A writer must wait until there are no active readers or writers.
 * Writers are given priority over new readers.
 */
void writer_lock(rwlock_t *rw);

/**
 * @brief Release the lock from writing.
 */
void writer_unlock(rwlock_t *rw);
