#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#include "../../queue.h"

static int tests_run    = 0;
static int tests_failed = 0;

#define RUN_TEST(fn)                        \
  do {                                      \
    tests_run++;                            \
    printf("Running %s...\n", #fn);         \
    fn();                                   \
    printf("[DONE] %s\n\n", #fn);           \
  } while (0)

#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond)) {                                                              \
      tests_failed++;                                                           \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      return;                                                                   \
    }                                                                           \
  } while (0)

/* For use inside pthread entry functions (which must return void *). */
#define CHECK_THREAD(cond)                                                      \
  do {                                                                          \
    if (!(cond)) {                                                              \
      tests_failed++;                                                           \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      return NULL;                                                              \
    }                                                                           \
  } while (0)

static void test_queue_new_and_delete(void) {
  queue_t *q = queue_new(4);
  CHECK(q != NULL);

  queue_delete(&q);
  CHECK(q == NULL);
}

static void test_queue_push_pop_basic(void) {
  queue_t *q = queue_new(4);
  CHECK(q != NULL);

  int a = 1;
  int b = 2;
  int c = 3;

  CHECK(queue_push(q, &a) == true);
  CHECK(queue_push(q, &b) == true);
  CHECK(queue_push(q, &c) == true);

  int *out = NULL;

  CHECK(queue_pop(q, (void **) &out) == true);
  CHECK(out == &a);
  CHECK(*out == 1);

  CHECK(queue_pop(q, (void **) &out) == true);
  CHECK(out == &b);
  CHECK(*out == 2);

  CHECK(queue_pop(q, (void **) &out) == true);
  CHECK(out == &c);
  CHECK(*out == 3);

  queue_delete(&q);
  CHECK(q == NULL);
}

typedef struct {
  queue_t *q;
  int      count;
} pc_args_t;

static void *producer_thread(void *arg) {
  pc_args_t *pc = arg;
  for (int i = 0; i < pc->count; i++) {
    int *value = malloc(sizeof(int));
    CHECK_THREAD(value != NULL);
    *value = i;
    CHECK_THREAD(queue_push(pc->q, value) == true);
  }
  return NULL;
}

static void *consumer_thread(void *arg) {
  pc_args_t *pc = arg;
  for (int i = 0; i < pc->count; i++) {
    int *value = NULL;
    CHECK_THREAD(queue_pop(pc->q, (void **) &value) == true);
    CHECK_THREAD(value != NULL);
    CHECK_THREAD(*value == i);
    free(value);
  }
  return NULL;
}

static void test_queue_blocking_producer_consumer(void) {
  const int capacity = 4;
  const int nitems   = 20;

  queue_t *q = queue_new(capacity);
  CHECK(q != NULL);

  pc_args_t pc = {
    .q     = q,
    .count = nitems,
  };

  pthread_t prod, cons;
  int rc;

  rc = pthread_create(&prod, NULL, producer_thread, &pc);
  CHECK(rc == 0);
  rc = pthread_create(&cons, NULL, consumer_thread, &pc);
  CHECK(rc == 0);

  rc = pthread_join(prod, NULL);
  CHECK(rc == 0);
  rc = pthread_join(cons, NULL);
  CHECK(rc == 0);

  queue_delete(&q);
  CHECK(q == NULL);
}

static void test_queue_null_behavior(void) {
  CHECK(queue_push(NULL, NULL) == false);

  void *out = (void *) 0xDEADBEEF;
  CHECK(queue_pop(NULL, &out) == false);
}

int main(void) {
  RUN_TEST(test_queue_new_and_delete);
  RUN_TEST(test_queue_push_pop_basic);
  RUN_TEST(test_queue_blocking_producer_consumer);
  RUN_TEST(test_queue_null_behavior);

  printf("Queue unit tests: %d run, %d failed\n", tests_run, tests_failed);
  return tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
