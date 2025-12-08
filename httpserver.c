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

#define BUF_SIZE 2048
#define METHOD_MAX_LEN 8   // per spec: 1–8 alpha chars
#define URI_MAX_LEN 63     // chars after leading '/'
#define VERSION_MAX_LEN 15 // "HTTP/x.y" fits easily
#define REQUEST_LINE_MAX BUF_SIZE

typedef struct http_request {
  char *method;
  char *uri;
  char *version;
  char *content_length;
  char *request_id;
} http_request_t;

typedef struct file_lock_entry {
  rwlock_t *rwlock;
  char *uri;
  int count;
} file_lock_entry;

typedef struct {
  file_lock_entry *array;
  int size;
} file_lock_table;

static pthread_mutex_t fl_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---------------- File lock management ---------------- */

file_lock_entry *file_lock_table_init(int size)
{
  file_lock_entry *fl_array = calloc(size, sizeof(file_lock_entry));
  for (int i = 0; i < size; i++) {
    fl_array[i].rwlock = rwlock_new();
    fl_array[i].uri = NULL;
    fl_array[i].count = 0;
  }
  return fl_array;
}

int file_lock_find_empty(file_lock_entry *array, int size)
{
  for (int i = 0; i < size; i++) {
    if (array[i].count == 0 && array[i].uri == NULL) {
      return i;
    }
  }
  return -1;
}

int file_lock_find(file_lock_entry *array, char *uri, int size)
{
  for (int i = 0; i < size; i++) {
    if (array[i].uri != NULL && strcmp(array[i].uri, uri) == 0) {
      return i;
    }
  }
  return -1;
}

int file_lock_acquire_ref(file_lock_entry *array, char *uri, int size)
{
  int pos = file_lock_find(array, uri, size);
  if (pos != -1) {
    array[pos].count++;
  } else {
    pos = file_lock_find_empty(array, size);
    if (pos == -1) {
      // File lock array is full; with current sizing this should not occur.
      fprintf(stderr, "FileLock array full, cannot create lock for %s\n", uri);
      exit(1);
    }
    array[pos].count++;
    array[pos].uri = calloc(strlen(uri) + 1, sizeof(char));
    strcpy(array[pos].uri, uri);
  }
  return pos;
}

void file_lock_release_ref(file_lock_entry *array, char *uri, int size)
{
  int pos = file_lock_find(array, uri, size);
  if (pos != -1) {
    array[pos].count--;
    if (array[pos].count == 0) {
      free(array[pos].uri);
      array[pos].uri = NULL;
    }
  } else {
    fprintf(stderr, "Fatal error: Can't remove nonexistent file lock.\n");
    exit(1);
  }
}

void file_lock_read_lock(file_lock_entry *array, char *uri, int size)
{
  pthread_mutex_lock(&fl_mutex);
  int pos = file_lock_acquire_ref(array, uri, size);
  rwlock_t *lock = array[pos].rwlock;
  pthread_mutex_unlock(&fl_mutex);

  reader_lock(lock);
}

void file_lock_read_unlock(file_lock_entry *array, char *uri, int size)
{
  pthread_mutex_lock(&fl_mutex);
  int pos = file_lock_find(array, uri, size);
  if (pos == -1) {
    pthread_mutex_unlock(&fl_mutex);
    fprintf(stderr, "Fatal error: file_lock_read_unlock for unknown uri %s\n", uri);
    exit(1);
  }
  rwlock_t *lock = array[pos].rwlock;
  file_lock_release_ref(array, uri, size);
  pthread_mutex_unlock(&fl_mutex);

  reader_unlock(lock);
}

void file_lock_write_lock(file_lock_entry *array, char *uri, int size)
{
  pthread_mutex_lock(&fl_mutex);
  int pos = file_lock_acquire_ref(array, uri, size);
  rwlock_t *lock = array[pos].rwlock;
  pthread_mutex_unlock(&fl_mutex);

  writer_lock(lock);
}

void file_lock_write_unlock(file_lock_entry *array, char *uri, int size)
{
  pthread_mutex_lock(&fl_mutex);
  int pos = file_lock_find(array, uri, size);
  if (pos == -1) {
    pthread_mutex_unlock(&fl_mutex);
    fprintf(stderr, "Fatal error: file_lock_write_unlock for unknown uri %s\n", uri);
    exit(1);
  }
  rwlock_t *lock = array[pos].rwlock;
  file_lock_release_ref(array, uri, size);
  pthread_mutex_unlock(&fl_mutex);

  writer_unlock(lock);
}

/* Global file lock array (one rwlock per URI, reused via reference counting).
 */
file_lock_table fl_array;

/* ---------------- HTTP request parsing and handling ---------------- */

http_request_t *http_request_new(void)
{
  http_request_t *req = malloc(sizeof(http_request_t));
  req->method = NULL;
  req->uri = NULL;
  req->version = NULL;
  req->content_length = NULL;
  req->request_id = NULL;
  return (req);
}

void http_request_free(http_request_t **preq)
{
  if (preq == NULL || *preq == NULL)
    return;

  http_request_t *req = *preq;

  free(req->method);
  free(req->uri);
  free(req->version);
  free(req->content_length);
  free(req->request_id);

  free(req);
  *preq = NULL;
}

// Parse the HTTP request line from requestBuffer.
//
// Expected grammar (simplified HTTP/1.1):
//   <METHOD> SP <URI> SP <VERSION> CRLF
//
// Where:
//   METHOD  = 1–8 alphabetic characters
//   URI     = '/' + 1–63 characters of [A-Za-z0-9._-]
//   VERSION = "HTTP/x.y" (you'll later enforce "HTTP/1.1")
//
// On success:
//   - request->method, request->uri, request->version are allocated and set.
//   - Returns the number of bytes consumed by the request line,
//     including the terminating "\r\n" (this is what you pass to
//     buffer_shift_left to move headers to the front).
//
// On failure:
//   - *status_code is set to 400 (bad request) or 500 (internal error).
//   - Returns -1.
ssize_t parse_request_line(char *requestBuffer, http_request_t *request, int *status_code)
{
  // 1) Find the CRLF that terminates the request line.
  char *line_end = strstr(requestBuffer, "\r\n");
  if (line_end == NULL) {
    // No CRLF in buffer → malformed request line.
    *status_code = 400;
    return -1;
  }

  // Number of bytes in the line *excluding* CRLF.
  ssize_t line_len = line_end - requestBuffer;
  if (line_len <= 0 || line_len >= REQUEST_LINE_MAX) {
    *status_code = 400;
    return -1;
  }

  // 2) Copy the request line into a temporary, NUL-terminated string
  //    so we can safely use sscanf / string functions on it.
  char line[REQUEST_LINE_MAX];
  memcpy(line, requestBuffer, line_len);
  line[line_len] = '\0';

  // 3) Split into three tokens: METHOD SP URI SP VERSION
  char method[METHOD_MAX_LEN + 1]; // +1 for NUL
  char uri[URI_MAX_LEN + 2];       // leading '/' + up to 63 chars + NUL
  char version[VERSION_MAX_LEN + 1];

  // sscanf will stop tokens at whitespace, which matches our use case here.
  if (sscanf(line, "%8s %64s %15s", method, uri, version) != 3) {
    *status_code = 400;
    return -1;
  }

  // 4) Validate METHOD: 1–8 alphabetic characters.
  size_t mlen = strlen(method);
  if (mlen < 1 || mlen > METHOD_MAX_LEN) {
    *status_code = 400;
    return -1;
  }
  for (size_t i = 0; i < mlen; i++) {
    char c = method[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
      *status_code = 400;
      return -1;
    }
  }

  // 5) Validate URI:
  //    - Must start with '/'
  //    - After '/', 1–63 chars of [A-Za-z0-9._-]
  size_t ulen = strlen(uri);
  if (ulen < 2 || ulen > (URI_MAX_LEN + 1)) {
    // at least '/' + 1 char, at most '/' + 63 chars
    *status_code = 400;
    return -1;
  }
  if (uri[0] != '/') {
    *status_code = 400;
    return -1;
  }
  for (size_t i = 1; i < ulen; i++) {
    char c = uri[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
          c == '_' || c == '-')) {
      *status_code = 400;
      return -1;
    }
  }

  // 6) Validate VERSION shape: must start with "HTTP/".
  // Check for HTTP/1.1 comes later
  if (strncmp(version, "HTTP/", 5) != 0) {
    *status_code = 400;
    return -1;
  }

  // 7) Allocate and store the parsed fields in the request struct.
  //    - method: full string (e.g. "GET")
  //    - uri   : without leading '/' (matches how your code uses it)
  //    - version: full string (e.g. "HTTP/1.1")
  request->method = calloc(mlen + 1, sizeof(char));
  request->uri = calloc(ulen, sizeof(char)); // ulen-1 chars + NUL
  request->version = calloc(strlen(version) + 1, sizeof(char));

  if (!request->method || !request->uri || !request->version) {
    free(request->method);
    free(request->uri);
    free(request->version);
    *status_code = 500;
    return -1;
  }

  strcpy(request->method, method);
  // store URI without the leading '/'
  strcpy(request->uri, uri + 1);
  strcpy(request->version, version);

  // 8) Return the number of bytes consumed by the request line,
  //    including the trailing "\r\n". This is what you will pass to
  //    buffer_shift_left so that the headers start at index 0.
  ssize_t request_bytes = line_len + 2; // +2 for "\r\n"
  return request_bytes;
}

// Returns the number of bytes of data in buf, assuming data is at the front
// and the rest of the buffer is zero-padded.
size_t buffer_data_length(const char *buf, size_t buf_size)
{
  // Scan from the end backwards to find the last non-zero byte.
  for (ssize_t i = (ssize_t)buf_size - 1; i >= 0; i--) {
    if (buf[i] != '\0') {
      return (size_t)i + 1;
    }
  }
  // All bytes are zero → no data
  return 0;
}

/*
 * buffer_shift_left - shift the contents of buf left by `shift` bytes.
 * The first `shift` bytes are discarded; the remaining data is compacted.
 */
void buffer_shift_left(char *buf, ssize_t max_size, ssize_t shift)
{
  ssize_t remaining_bytes = max_size - shift;
  char tempbuf[remaining_bytes];
  memcpy(tempbuf, buf + shift, remaining_bytes);
  memset(buf, '\0', max_size);
  memcpy(buf, tempbuf, remaining_bytes);
}

/*
 * parse_content_length - parse Content-Length header and trim header bytes.
 * On success, stores the length string in request->content_length and
 * shifts headerBuffer so that it starts at the message body.
 */
void parse_content_length(char *headerBuffer, http_request_t *request, int *status_code)
{
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
    buffer_shift_left(headerBuffer, 2048, newline_pointer - headerBuffer + 4);
  }
}

/*
 * parse_request_id - parse Request-Id header (if present) into
 * request->request_id. Missing header is treated as ID 0.
 */
void parse_request_id(char *headerBuffer, http_request_t *request)
{
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
 * handle_get - validate a GET request target and return file length.
 * Returns file length on success, -1 on error (status_code set accordingly).
 */
int handle_get(http_request_t *request, int *status_code)
{
  if (strcmp(request->version, "HTTP/1.1") != 0) {
    *status_code = 505;
    return -1;
  }
  int fd = open(request->uri, O_RDONLY);
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
 * handle_put - handle PUT request body write.
 * Writes any bytes already in messageBuffer, then streams the rest from socket.
 * Returns 0 on success, -1 on error (status_code set accordingly).
 */
int handle_put(char *messageBuffer, int socket, http_request_t *request, int *status_code)
{
  if (strcmp(request->version, "HTTP/1.1") != 0) {
    *status_code = 505;
    return -1;
  }

  *status_code = 200;
  int fd = open(request->uri, O_WRONLY | O_TRUNC, 0);
  if (fd == -1) {
    *status_code = 201;
    fd = creat(request->uri, 0666);
    if (fd == -1) {
      *status_code = 500;
      return -1;
    }
  }

  // Total number of bytes we expect in the body.
  size_t content_length_num = (size_t)atoi(request->content_length);

  // How many body bytes are already in messageBuffer?
  size_t buffered_bytes = buffer_data_length(messageBuffer, BUF_SIZE);
  if (buffered_bytes > content_length_num) {
    // Be defensive: never write more than Content-Length.
    buffered_bytes = content_length_num;
  }

  // Write the buffered body bytes first (if any).
  if (buffered_bytes > 0) {
    size_t written = write_n_bytes(fd, messageBuffer, buffered_bytes);
    if (written != buffered_bytes) {
      *status_code = 500;
      close(fd);
      return -1;
    }
  }

  // Then stream the remaining bytes from the socket to the file.
  size_t remaining = content_length_num - buffered_bytes;
  if (remaining > 0) {
    pass_n_bytes(socket, fd, remaining);
  }

  close(fd);
  return 0;
}

/*
 * buffer_is_empty - check if buffer contains only zero bytes.
 * Returns 1 if empty, 0 otherwise.
 */
int buffer_is_empty(char *messageBuffer)
{
  for (size_t i = 0; i < BUF_SIZE; i++) {
    if (messageBuffer[i] != 0) {
      return 0;
    }
  }
  return 1;
}

/*
 * log_audit_entry - log request outcome to stderr in CSV format:
 *   METHOD,URI,STATUS_CODE,REQUEST_ID
 */
void log_audit_entry(http_request_t *request, int *status_code)
{
  fprintf(stderr, "%s,%s,%d,%s\n", request->method, request->uri, *status_code,
          request->request_id);
}

/*
 * response - build and send an HTTP/1.1 response.
 * For GET + 200, streams file contents after headers.
 * For other cases, sends a short text body with the status phrase.
 */
void send_response(int socket, http_request_t *request, int *status_code, int content_length)
{
  char response[2048];

  if (strcmp(request->version, "HTTP/1.1") != 0) {
    *status_code = 505;
  }

  char sc_string[10];
  sprintf(sc_string, "%d ", *status_code);

  char status_phrase[30];
  strcpy(status_phrase, "Unknown"); // default

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
    response_length = snprintf(response, sizeof(response), "%s%s%s\r\nContent-Length: %d\r\n\r\n",
                               "HTTP/1.1 ", sc_string, status_phrase, content_length);
    write_n_bytes(socket, response, response_length);

    int fd = open(request->uri, O_RDONLY, 0);
    pass_n_bytes(fd, socket, content_length);
    close(fd);

    log_audit_entry(request, status_code);
  } else {
    // Send headers + short body containing the status phrase.
    response_length =
        snprintf(response, sizeof(response), "%s%s%s\r\nContent-Length: %d\r\n\r\n%s\n",
                 "HTTP/1.1 ", sc_string, status_phrase, content_length, status_phrase);
    write_n_bytes(socket, response, response_length);
    log_audit_entry(request, status_code);
  }
}

/* ---------------- Server thread ---------------- */

/*
 * worker_thread - worker loop.
 * Each thread pulls a socket fd from the queue, serves exactly one request,
 * and then closes the connection.
 */
void *worker_thread(void *arg)
{
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

    http_request_t *request = http_request_new();
    int request_bytes = parse_request_line(requestBuffer, request, status_code);

    if (request_bytes == -1) {
      // Invalid request line: synthesize minimal request for error response.
      request->method = calloc(4, sizeof(char));
      strcpy(request->method, "NONE");
      request->version = calloc(8, sizeof(char));
      strcpy(request->version, "HTTP/1.1");
      send_response(socket, request, status_code, -1);
    } else {
      // Shift buffer to drop the request line and keep header bytes.
      buffer_shift_left(requestBuffer, 2048, request_bytes);
      memcpy(headerBuffer, requestBuffer, 2048);
      memcpy(headerBufferCopy, headerBuffer, 2048);

      parse_content_length(headerBuffer, request, status_code);
      parse_request_id(headerBufferCopy, request);

      // After parse_content_length, headerBuffer now starts at the message
      // body.
      memcpy(messageBuffer, headerBuffer, 2048);

      if (strcmp(request->method, "GET") == 0) {
        if (buffer_is_empty(messageBuffer) == 1) {
          file_lock_read_lock(fl_array.array, request->uri, fl_array.size);
          int file_length = handle_get(request, status_code);
          send_response(socket, request, status_code, file_length);
          file_lock_read_unlock(fl_array.array, request->uri, fl_array.size);
        } else {
          // GET requests must not include a body.
          *status_code = 400;
          send_response(socket, request, status_code, -1);
        }
      } else if (strcmp(request->method, "PUT") == 0) {
        file_lock_write_lock(fl_array.array, request->uri, fl_array.size);
        handle_put(messageBuffer, socket, request, status_code);
        send_response(socket, request, status_code, -1);
        file_lock_write_unlock(fl_array.array, request->uri, fl_array.size);
      } else {
        // Method not supported.
        *status_code = 501;
        send_response(socket, request, status_code, -1);
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

    http_request_free(&request);
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
void process_args(int argc, char **argv, int *num_threads, int *port_number)
{
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

int main(int argc, char **argv)
{
  int num_threads = 0;
  int port_number = 0;

  process_args(argc, argv, &num_threads, &port_number);

  if (port_number < 1 || port_number > 65535) {
    fprintf(stderr, "Invalid Port\n");
    exit(1);
  }

  pthread_t threads[num_threads];
  queue_t *request_queue = queue_new(num_threads);
  fl_array.array = file_lock_table_init(num_threads);
  fl_array.size = num_threads;

  for (int i = 0; i < num_threads; i++) {
    pthread_create(&threads[i], NULL, worker_thread, (void *)request_queue);
  }

  Listener_Socket sock;
  listener_init(&sock, port_number);

  while (1) {
    int *socket_pointer = malloc(sizeof(int));
    *socket_pointer = listener_accept(&sock);
    queue_push(request_queue, socket_pointer);
  }
}
