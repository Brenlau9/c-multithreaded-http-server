#include "rwlock.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct rwlock {
  pthread_mutex_t mutex;
  pthread_cond_t can_read;
  pthread_cond_t can_write;
  int active_readers;
  int active_writers;
  int waiting_readers;
  int waiting_writers;
} rwlock;

rwlock_t *rwlock_new() {
  rwlock_t *rwlock = calloc(1, sizeof(rwlock_t));
  pthread_mutex_init(&rwlock->mutex, NULL);
  pthread_cond_init(&rwlock->can_read, NULL);
  pthread_cond_init(&rwlock->can_write, NULL);
  rwlock->active_readers = 0;
  rwlock->active_writers = 0;
  rwlock->waiting_readers = 0;
  rwlock->waiting_writers = 0;
  return rwlock;
}
void rwlock_delete(rwlock_t **rw) {
  if (rw != NULL && *rw != NULL) {
    pthread_mutex_destroy(&(*rw)->mutex);
    pthread_cond_destroy(&(*rw)->can_read);
    pthread_cond_destroy(&(*rw)->can_write);
    free(*rw);
    *rw = NULL;
  }
}
void reader_lock(rwlock_t *rw) {
  pthread_mutex_lock(&rw->mutex);
  /*
  If there are active/waiting writers, readers must wait
  */
  rw->waiting_readers++;
  while (rw->active_writers > 0 || rw->waiting_writers > 0) {
    pthread_cond_wait(&rw->can_read, &rw->mutex);
  }
  rw->waiting_readers--;
  rw->active_readers++;

  pthread_mutex_unlock(&rw->mutex);
}
void reader_unlock(rwlock_t *rw) {
  pthread_mutex_lock(&rw->mutex);
  /*
  If there are waiting writers, prioritize them
  Signal to writers when there are no readers
  Otherwise
  Readers can obtain the lock if there are no writers
  */
  rw->active_readers--;
  if (rw->active_readers == 0 && rw->waiting_writers > 0) {
    pthread_cond_signal(&rw->can_write);
  }

  pthread_mutex_unlock(&rw->mutex);
}
void writer_lock(rwlock_t *rw) {
  pthread_mutex_lock(&rw->mutex);
  /*
  If there are active readers/writers, writer must wait
  Wait until writer receives a signal
  */
  rw->waiting_writers++;
  while (rw->active_writers > 0 || rw->active_readers > 0) {
    pthread_cond_wait(&rw->can_write, &rw->mutex);
  }
  rw->waiting_writers--;
  rw->active_writers = 1;

  pthread_mutex_unlock(&rw->mutex);
}
void writer_unlock(rwlock_t *rw) {
  pthread_mutex_lock(&rw->mutex);
  /*
  If there are waiting writers, signal them first
  */
  rw->active_writers = 0;
  if (rw->waiting_writers > 0) {
    pthread_cond_signal(&rw->can_write);
  } else if (rw->waiting_readers > 0) {
    pthread_cond_broadcast(&rw->can_read);
  }

  pthread_mutex_unlock(&rw->mutex);
}
