#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "../../queue.h"

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

  // Now queue should be empty.
  CHECK(queue_pop(q, (void **) &out) == false);

  queue_delete(&q);
  CHECK(q == NULL);
}

static void test_queue_push_until_full(void) {
  const int capacity = 3;
  queue_t  *q        = queue_new(capacity);
  CHECK(q != NULL);

  int vals[4] = {10, 20, 30, 40};

  // Fill to capacity.
  CHECK(queue_push(q, &vals[0]) == true);
  CHECK(queue_push(q, &vals[1]) == true);
  CHECK(queue_push(q, &vals[2]) == true);

  // Now it should be full; the next push should fail.
  CHECK(queue_push(q, &vals[3]) == false);

  // Pop everything and verify order.
  int *out = NULL;
  CHECK(queue_pop(q, (void **) &out) == true);
  CHECK(*out == 10);
  CHECK(queue_pop(q, (void **) &out) == true);
  CHECK(*out == 20);
  CHECK(queue_pop(q, (void **) &out) == true);
  CHECK(*out == 30);

  // Now empty again.
  CHECK(queue_pop(q, (void **) &out) == false);

  queue_delete(&q);
  CHECK(q == NULL);
}

static void test_queue_null_behavior(void) {
  // These should fail safely, not crash.
  CHECK(queue_push(NULL, NULL) == false);

  void *out = (void *) 0xDEADBEEF;
  CHECK(queue_pop(NULL, &out) == false);
  // out contents are unspecified; we only care that it doesn't crash.
}

int main(void) {
  RUN_TEST(test_queue_new_and_delete);
  RUN_TEST(test_queue_push_pop_basic);
  RUN_TEST(test_queue_push_until_full);
  RUN_TEST(test_queue_null_behavior);

  printf("Queue unit tests: %d run, %d failed\n", tests_run, tests_failed);
  return tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
