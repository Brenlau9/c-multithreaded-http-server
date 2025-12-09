#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#include "../../rwlock.h"

static int tests_run    = 0;
static int tests_failed = 0;

#define RUN_TEST(fn)                        \
  do {                                      \
    tests_run++;                            \
    printf("Running %s...\n", #fn);         \
    fn();                                   \
    if (tests_failed == 0) {                \
      printf("[PASS] %s\n\n", #fn);         \
    } else {                                \
      printf("[DONE] %s (failures so far: %d)\n\n", #fn, tests_failed); \
    }                                       \
  } while (0)

#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond)) {                                                              \
      tests_failed++;                                                           \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      return;                                                                   \
    }                                                                           \
  } while (0)

static void test_rwlock_new_and_delete(void) {
  rwlock_t *rw = rwlock_new();
  CHECK(rw != NULL);

  rwlock_delete(&rw);
  CHECK(rw == NULL);
}

// Simple single-thread smoke test: lock/unlock in reader/writer modes.
static void test_rwlock_single_thread_basic(void) {
  rwlock_t *rw = rwlock_new();
  CHECK(rw != NULL);

  reader_lock(rw);
  reader_unlock(rw);

  writer_lock(rw);
  writer_unlock(rw);

  // Mixed sequence: reader, writer, reader.
  reader_lock(rw);
  reader_unlock(rw);

  writer_lock(rw);
  writer_unlock(rw);

  reader_lock(rw);
  reader_unlock(rw);

  rwlock_delete(&rw);
  CHECK(rw == NULL);
}

// Shared state for multithread tests.
typedef struct {
  rwlock_t *rw;
  int       value;
} shared_t;

static void *reader_thread_func(void *arg) {
  shared_t *shared = (shared_t *) arg;

  for (int i = 0; i < 10000; i++) {
    reader_lock(shared->rw);
    int v = shared->value;
    (void) v;  // We don't assert here; just ensure no crashes/data races under lock.
    reader_unlock(shared->rw);
  }

  return NULL;
}

static void *writer_thread_func(void *arg) {
  shared_t *shared = (shared_t *) arg;

  for (int i = 0; i < 1000; i++) {
    writer_lock(shared->rw);
    shared->value++;
    writer_unlock(shared->rw);

    // Small sleep to give readers a chance to run.
    // This is not strictly necessary but makes interleaving more likely.
    usleep(100);
  }

  return NULL;
}

// Light concurrency test: multiple readers + one writer updating a counter.
static void test_rwlock_readers_and_writer(void) {
  rwlock_t *rw = rwlock_new();
  CHECK(rw != NULL);

  shared_t shared = {
    .rw    = rw,
    .value = 0,
  };

  const int num_readers = 4;
  pthread_t readers[num_readers];
  pthread_t writer;

  // Start readers.
  for (int i = 0; i < num_readers; i++) {
    int rc = pthread_create(&readers[i], NULL, reader_thread_func, &shared);
    CHECK(rc == 0);
  }

  // Start writer.
  int rc = pthread_create(&writer, NULL, writer_thread_func, &shared);
  CHECK(rc == 0);

  // Wait for all threads to finish.
  for (int i = 0; i < num_readers; i++) {
    rc = pthread_join(readers[i], NULL);
    CHECK(rc == 0);
  }
  rc = pthread_join(writer, NULL);
  CHECK(rc == 0);

  // We expect the writer to have incremented the value exactly 1000 times.
  CHECK(shared.value == 1000);

  rwlock_delete(&rw);
  CHECK(rw == NULL);
}

int main(void) {
  RUN_TEST(test_rwlock_new_and_delete);
  RUN_TEST(test_rwlock_single_thread_basic);
  RUN_TEST(test_rwlock_readers_and_writer);

  printf("rwlock unit tests: %d run, %d failed\n", tests_run, tests_failed);
  return tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
