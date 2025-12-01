#include "helper_funcs.h"
#include "queue.h"
#include "rwlock.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct RequestObj *Request;
typedef struct RequestObj {
  char *method;
  char *URI;
  char *version;
  char *content_length;
  char *request_id;
} RequestObj;

typedef struct FileLockStruct {
  rwlock_t *rwlock;
  char *URI;
  int count;
} FileLockStruct;

typedef struct {
  FileLockStruct *array;
  int size;
} FileLockArray;

static pthread_mutex_t fl_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---------------- File lock management ---------------- */

FileLockStruct *newLockArray(int size) {
  FileLockStruct *fl_array = calloc(size, sizeof(FileLockStruct));
  for (int i = 0; i < size; i++) {
    fl_array[i].rwlock = rwlock_new();
    fl_array[i].URI = NULL;
    fl_array[i].count = 0;
  }
  return fl_array;
}

int find_emptyPos(FileLockStruct *array, int size) {
  for (int i = 0; i < size; i++) {
    if (array[i].count == 0 && array[i].URI == NULL) {
      return i;
    }
  }
  return -1;
}

int find_filePos(FileLockStruct *array, char *URI, int size) {
  for (int i = 0; i < size; i++) {
    if (array[i].URI != NULL && strcmp(array[i].URI, URI) == 0) {
      return i;
    }
  }
  return -1;
}

int add_fileLock(FileLockStruct *array, char *URI, int size) {
  int pos = find_filePos(array, URI, size);
  if (pos != -1) {
    array[pos].count++;
  } else {
    pos = find_emptyPos(array, size);
    if (pos == -1) {
      // File lock array is full; with current sizing this should not occur.
      fprintf(stderr, "FileLock array full, cannot create lock for %s\n", URI);
      exit(1);
    }
    array[pos].count++;
    array[pos].URI = calloc(strlen(URI) + 1, sizeof(char));
    strcpy(array[pos].URI, URI);
  }
  return pos;
}

void remove_fileLock(FileLockStruct *array, char *URI, int size) {
  int pos = find_filePos(array, URI, size);
  if (pos != -1) {
    array[pos].count--;
    if (array[pos].count == 0) {
      free(array[pos].URI);
      array[pos].URI = NULL;
    }
  } else {
    fprintf(stderr, "Fatal error: Can't remove nonexistent file lock.\n");
    exit(1);
  }
}

void reader_file_lock(FileLockStruct *array, char *URI, int size) {
  pthread_mutex_lock(&fl_mutex);
  int pos = add_fileLock(array, URI, size);
  rwlock_t *lock = array[pos].rwlock;
  pthread_mutex_unlock(&fl_mutex);

  reader_lock(lock);
}

void reader_file_unlock(FileLockStruct *array, char *URI, int size) {
  pthread_mutex_lock(&fl_mutex);
  int pos = find_filePos(array, URI, size);
  if (pos == -1) {
    pthread_mutex_unlock(&fl_mutex);
    fprintf(stderr, "Fatal error: reader_file_unlock for unknown URI %s\n",
            URI);
    exit(1);
  }
  rwlock_t *lock = array[pos].rwlock;
  remove_fileLock(array, URI, size);
  pthread_mutex_unlock(&fl_mutex);

  reader_unlock(lock);
}

void writer_file_lock(FileLockStruct *array, char *URI, int size) {
  pthread_mutex_lock(&fl_mutex);
  int pos = add_fileLock(array, URI, size);
  rwlock_t *lock = array[pos].rwlock;
  pthread_mutex_unlock(&fl_mutex);

  writer_lock(lock);
}

void writer_file_unlock(FileLockStruct *array, char *URI, int size) {
  pthread_mutex_lock(&fl_mutex);
  int pos = find_filePos(array, URI, size);
  if (pos == -1) {
    pthread_mutex_unlock(&fl_mutex);
    fprintf(stderr, "Fatal error: writer_file_unlock for unknown URI %s\n",
            URI);
    exit(1);
  }
  rwlock_t *lock = array[pos].rwlock;
  remove_fileLock(array, URI, size);
  pthread_mutex_unlock(&fl_mutex);

  writer_unlock(lock);
}

/* Global file lock array (one rwlock per URI, reused via reference counting).
 */
FileLockArray fl_array;

/* ---------------- HTTP request parsing and handling ---------------- */

Request newRequest(void) {
  Request R;
  R = malloc(sizeof(RequestObj));
  R->method = NULL;
  R->URI = NULL;
  R->version = NULL;
  R->content_length = NULL;
  R->request_id = NULL;
  return (R);
}

void freeRequest(Request *pR) {
  if (pR != NULL && *pR != NULL) {
    if ((*pR)->method != NULL) {
      free((*pR)->method);
    }
    if ((*pR)->URI != NULL) {
      free((*pR)->URI);
    }
    if ((*pR)->version != NULL) {
      free((*pR)->version);
    }
    if ((*pR)->content_length != NULL) {
      free((*pR)->content_length);
    }
    if ((*pR)->request_id != NULL) {
      free((*pR)->request_id);
    }
    free(*pR);
    *pR = NULL;
  }
}

ssize_t parseRequest(char *requestBuffer, Request request, int *status_code) {
  const char *re = "^([a-zA-Z]{1,8}) (/[a-zA-Z0-9._-]{1,63}) "
                   "(HTTP/[0-9]\\.[0-9])\r\n(.|\n)*$";
  regex_t regex;
  regmatch_t pmatch[5];
  int *sc_ptr = status_code;

  if (regcomp(&regex, re, REG_EXTENDED | REG_NEWLINE)) {
    *sc_ptr = 500;
    return -1;
  }
  if (regexec(&regex, requestBuffer, 5, pmatch, 0)) {
    *sc_ptr = 400;
    regfree(&regex);
    return -1;
  }

  request->method = calloc(pmatch[1].rm_eo - pmatch[1].rm_so + 1, sizeof(char));
  request->URI = calloc(pmatch[2].rm_eo - pmatch[2].rm_so + 1, sizeof(char));
  request->version =
      calloc(pmatch[3].rm_eo - pmatch[3].rm_so + 1, sizeof(char));

  strncpy(request->method, requestBuffer + pmatch[1].rm_so,
          pmatch[1].rm_eo - pmatch[1].rm_so);
  strncpy(request->URI, requestBuffer + pmatch[2].rm_so + 1,
          pmatch[2].rm_eo - pmatch[2].rm_so - 1);
  strncpy(request->version, requestBuffer + pmatch[3].rm_so,
          pmatch[3].rm_eo - pmatch[3].rm_so);

  ssize_t request_bytes = strlen(request->method) + strlen(request->URI) +
                          strlen(request->version) + 3 + 2;
  regfree(&regex);

  return (request_bytes);
}

/*
 * shiftBuffer - shift the contents of buf left by `shift` bytes.
 * The first `shift` bytes are discarded; the remaining data is compacted.
 */
void shiftBuffer(char *buf, ssize_t max_size, ssize_t shift) {
  ssize_t remaining_bytes = max_size - shift;
  char tempbuf[remaining_bytes];
  memcpy(tempbuf, buf + shift, remaining_bytes);
  memset(buf, '\0', max_size);
  memcpy(buf, tempbuf, remaining_bytes);
}

/*
 * getContentLength - parse Content-Length header and trim header bytes.
 * On success, stores the length string in request->content_length and
 * shifts headerBuffer so that it starts at the message body.
 */
void getContentLength(char *headerBuffer, Request request, int *status_code) {
  char *cl_pointer = strstr(headerBuffer, "Content-Length: ");
  int content_length = 0;
  if (cl_pointer != NULL) {
    sscanf(cl_pointer, "Content-Length: %d", &content_length);
  }
  char cl_string[10];
  sprintf(cl_string, "%d", content_length);
  request->content_length = calloc(10, sizeof(char));
  strcpy(request->content_length, cl_string);

  char *newline_pointer = strstr(headerBuffer, "\r\n\r\n");
  if (newline_pointer == NULL) {
    *status_code = 500;
  } else {
    shiftBuffer(headerBuffer, 2048, newline_pointer - headerBuffer + 4);
  }
}

/*
 * getRequestID - parse Request-Id header (if present) into request->request_id.
 * Missing header is treated as ID 0.
 */
void getRequestID(char *headerBuffer, Request request) {
  char *cl_pointer = strstr(headerBuffer, "Request-Id: ");
  int request_id = 0;
  if (cl_pointer != NULL) {
    sscanf(cl_pointer, "Request-Id: %d", &request_id);
  }
  char id_string[10];
  sprintf(id_string, "%d", request_id);
  request->request_id = calloc(10, sizeof(char));
  strcpy(request->request_id, id_string);
}

/*
 * getRequest - validate a GET request target and return file length.
 * Returns file length on success, -1 on error (status_code set accordingly).
 */
int getRequest(Request request, int *status_code) {
  if (strcmp(request->version, "HTTP/1.1") != 0) {
    *status_code = 505;
    return -1;
  }
  int fd = open(request->URI, O_RDONLY);
  if (fd == -1) {
    *status_code = 404;
    return -1;
  }

  char testbuf[1];
  int l = read_n_bytes(fd, testbuf, 1); // Probe read to check permissions.
  if (l == -1) {
    *status_code = 403;
    close(fd);
    return -1;
  }

  int content_length = lseek(fd, 0, SEEK_END);
  close(fd);

  *status_code = 200;
  return (content_length);
}

/*
 * putRequest - handle PUT request body write.
 * Writes any bytes already in messageBuffer, then streams the rest from socket.
 * Returns 0 on success, -1 on error (status_code set accordingly).
 */
int putRequest(char *messageBuffer, int socket, Request request,
               int *status_code) {
  if (strcmp(request->version, "HTTP/1.1") != 0) {
    *status_code = 505;
    return -1;
  }

  *status_code = 200;
  int fd = open(request->URI, O_WRONLY | O_TRUNC, 0);
  if (fd == -1) {
    *status_code = 201;
    fd = creat(request->URI, 0666);
    if (fd == -1) {
      *status_code = 500;
      return -1;
    }
  }

  // Write any body bytes already buffered after header parsing.
  size_t content_length_num = atoi(request->content_length);
  size_t bytes_written =
      write_n_bytes(fd, messageBuffer, strlen(messageBuffer));

  // Stream the remaining bytes directly from socket to file.
  pass_n_bytes(socket, fd, content_length_num - bytes_written);

  close(fd);
  return 0;
}

/*
 * messagebufEmpty - check if buffer contains only zero bytes.
 * Returns 1 if empty, 0 otherwise.
 */
int messagebufEmpty(char *messageBuffer) {
  for (size_t i = 0; i < 2048; i++) {
    if (messageBuffer[i] != 0) {
      return 0;
    }
  }
  return 1;
}

/*
 * audit_log - log request outcome to stderr in CSV format:
 *   METHOD,URI,STATUS_CODE,REQUEST_ID
 */
void audit_log(Request request, int *status_code) {
  fprintf(stderr, "%s,%s,%d,%s\n", request->method, request->URI, *status_code,
          request->request_id);
}

/*
 * response - build and send an HTTP/1.1 response.
 * For GET + 200, streams file contents after headers.
 * For other cases, sends a short text body with the status phrase.
 */
void response(int socket, Request request, int *status_code,
              int content_length) {
  char response[2048];

  if (strcmp(request->version, "HTTP/1.1") != 0) {
    *status_code = 505;
  }

  char sc_string[10];
  sprintf(sc_string, "%d ", *status_code);

  char status_phrase[30];
  if (*status_code == 200) {
    strcpy(status_phrase, "OK");
  } else if (*status_code == 201) {
    strcpy(status_phrase, "Created");
  } else if (*status_code == 400) {
    strcpy(status_phrase, "Bad Request");
  } else if (*status_code == 403) {
    strcpy(status_phrase, "Forbidden");
  } else if (*status_code == 404) {
    strcpy(status_phrase, "Not Found");
  } else if (*status_code == 500) {
    strcpy(status_phrase, "Internal Server Error");
  } else if (*status_code == 501) {
    strcpy(status_phrase, "Not Implemented");
  } else if (*status_code == 505) {
    strcpy(status_phrase, "Version Not Supported");
  }

  // For PUT or error responses, use a small text body with the status phrase.
  if (strcmp(request->method, "PUT") == 0 || content_length == -1) {
    content_length = (int)strlen(status_phrase) + 1;
  }

  char cl_string[10];
  sprintf(cl_string, "%d", content_length);

  int response_length;
  if (strcmp(request->method, "GET") == 0 && *status_code == 200) {
    // Header-only response, followed by file contents.
    response_length = snprintf(
        response, sizeof(response), "%s%s%s\r\nContent-Length: %d\r\n\r\n",
        "HTTP/1.1 ", sc_string, status_phrase, content_length);
    write_n_bytes(socket, response, response_length);

    int fd = open(request->URI, O_RDONLY, 0);
    pass_n_bytes(fd, socket, content_length);
    close(fd);

    audit_log(request, status_code);
  } else {
    // Send headers + short body containing the status phrase.
    response_length = snprintf(
        response, sizeof(response), "%s%s%s\r\nContent-Length: %d\r\n\r\n%s\n",
        "HTTP/1.1 ", sc_string, status_phrase, content_length, status_phrase);
    write_n_bytes(socket, response, response_length);
    audit_log(request, status_code);
  }
}

/* ---------------- Server thread ---------------- */

/*
 * server_thread - worker loop.
 * Each thread pulls a socket fd from the queue, serves exactly one request,
 * and then closes the connection.
 */
void *server_thread(void *arg) {
  queue_t *request_queue = (queue_t *)arg;

  while (1) {
    int *socket_pointer;
    queue_pop(request_queue, (void **)&socket_pointer);

    int socket = *socket_pointer;
    int *status_code = malloc(sizeof(int));
    char *requestBuffer = calloc(2048, sizeof(char));
    char *headerBuffer = calloc(2048, sizeof(char));
    char *messageBuffer = calloc(2048, sizeof(char));
    char *headerBufferCopy = calloc(2048, sizeof(char));

    // Read request line and headers (up to the blank line).
    read_until(socket, requestBuffer, 2048, "\r\n\r\n");

    Request request = newRequest();
    int request_bytes = parseRequest(requestBuffer, request, status_code);

    if (request_bytes == -1) {
      // Invalid request line: synthesize minimal request for error response.
      request->method = calloc(4, sizeof(char));
      strcpy(request->method, "NONE");
      request->version = calloc(8, sizeof(char));
      strcpy(request->version, "HTTP/1.1");
      response(socket, request, status_code, -1);
    } else {
      // Shift buffer to drop the request line and keep header bytes.
      shiftBuffer(requestBuffer, 2048, request_bytes);
      memcpy(headerBuffer, requestBuffer, 2048);
      memcpy(headerBufferCopy, headerBuffer, 2048);

      getContentLength(headerBuffer, request, status_code);
      getRequestID(headerBufferCopy, request);

      // After getContentLength, headerBuffer now starts at the message body.
      memcpy(messageBuffer, headerBuffer, 2048);

      if (strcmp(request->method, "GET") == 0) {
        if (messagebufEmpty(messageBuffer) == 1) {
          reader_file_lock(fl_array.array, request->URI, fl_array.size);
          int file_length = getRequest(request, status_code);
          response(socket, request, status_code, file_length);
          reader_file_unlock(fl_array.array, request->URI, fl_array.size);
        } else {
          // GET requests must not include a body.
          *status_code = 400;
          response(socket, request, status_code, -1);
        }
      } else if (strcmp(request->method, "PUT") == 0) {
        writer_file_lock(fl_array.array, request->URI, fl_array.size);
        putRequest(messageBuffer, socket, request, status_code);
        response(socket, request, status_code, -1);
        writer_file_unlock(fl_array.array, request->URI, fl_array.size);
      } else {
        // Method not supported.
        *status_code = 501;
        response(socket, request, status_code, -1);
      }
    }

    /*
     * Optional: drain any unread data from the socket.
     * Kept commented out because the current protocol only expects
     * a single request per connection.
     *
     * char garbage_buf[2048];
     * int garbage_bytes = 1;
     * while (garbage_bytes != 0) {
     *     garbage_bytes = read(socket, garbage_buf, 2048);
     * }
     */

    freeRequest(&request);
    free(status_code);
    free(requestBuffer);
    free(headerBuffer);
    free(headerBufferCopy);
    free(messageBuffer);
    free(socket_pointer);
    close(socket);
  }
}

/* ---------------- Command-line argument processing ---------------- */

/*
 * process_args - parse -t <num_threads> and port number.
 * On error, prints a message and exits.
 */
void process_args(int argc, char **argv, int *num_threads, int *port_number) {
  getopt(argc, argv, "t:");
  if (argc == 4) {
    if (optarg == NULL) {
      *num_threads = 4;
    } else {
      *num_threads = atoi(optarg);
    }
    if (argv[optind] == NULL) {
      fprintf(stderr, "Invalid command\n");
      exit(1);
    } else {
      *port_number = atoi(argv[optind]);
    }
  } else if (argc == 2) {
    *num_threads = 4;
    if (optind == 1) {
      *port_number = atoi(argv[optind]);
    } else {
      fprintf(stderr, "Invalid command\n");
      exit(1);
    }
  } else {
    fprintf(stderr, "Invalid command\n");
    exit(1);
  }
}

/* ---------------- Main entry point ---------------- */

int main(int argc, char **argv) {
  int num_threads = 0;
  int port_number = 0;

  process_args(argc, argv, &num_threads, &port_number);

  if (port_number < 1 || port_number > 65536) {
    fprintf(stderr, "Invalid Port\n");
    exit(1);
  }

  pthread_t threads[num_threads];
  queue_t *request_queue = queue_new(num_threads);
  fl_array.array = newLockArray(num_threads);
  fl_array.size = num_threads;

  for (int i = 0; i < num_threads; i++) {
    pthread_create(&threads[i], NULL, server_thread, (void *)request_queue);
  }

  Listener_Socket sock;
  listener_init(&sock, port_number);

  while (1) {
    int *socket_pointer = malloc(sizeof(int));
    *socket_pointer = listener_accept(&sock);
    queue_push(request_queue, socket_pointer);
  }
}
