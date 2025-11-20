# Multithreaded HTTP Server in C #

A lightweight, POSIX-threaded HTTP/1.1 server supporting concurrent GET and PUT requests.
Built with a thread pool, a custom writer-priority reader–writer lock, and robust file I/O helpers.

## 🚀 Features ##
### HTTP/1.1 GET ###
- Serves files from the working directory
- Returns:
    - 200 OK — file exists
    - 404 Not Found — file missing
### HTTP/1.1 PUT ###
- Creates or overwrites files
- Handles arbitrary binary data
- Uses Content-Length for body parsing
- Returns:
    - 201 Created — new file
    - 200 OK — overwrite
### Thread Pool ###
- Configurable with -t <num_threads>
- Listener thread accepts connections
- Worker threads pop requests from a thread-safe queue
### Writer-Priority Reader–Writer Lock ###
- Allows concurrent GETs on the same file
- Ensures PUT gets exclusive access
- Prevents writer starvation
- Implemented using:
    - pthread_mutex_t
    - pthread_cond_t can_read
    - pthread_cond_t can_write
### Robust I/O Helpers ###
- read_until() for safe header parsing
- read_n_bytes() + pass_n_bytes() for PUT bodies
- Clean socket setup and teardown
## 📁 Project Structure ##
```bash
.
├── httpserver.c            # Core HTTP logic (GET/PUT), parsing, routing
├── queue.c / queue.h       # Thread-safe job queue for worker threads
├── rwlock.c / rwlock.h     # Writer-priority reader–writer lock
├── helper_funcs.c/.h       # Socket helpers + robust IO helpers
├── Makefile
└── tests/
    └── integration/
        ├── test_cli.sh
        ├── test_endpoints.sh
        └── test_concurrency.sh   # Optional concurrency test
```
## 🛠️ Build Instructions ##
### Build ###
```bash
make
```
### Produces: ###
```bash
./httpserver
```
### Clean ###
```bash
make clean
```
## ▶️ Running the Server ##
### Single-threaded ###
```bash
./httpserver 8080
```
### With a thread pool (example: 4 threads) ###
```bash
./httpserver -t 4 8080
```
### Test with curl ###
```bash
curl http://127.0.0.1:8080/somefile
```
### 🧪 Testing ###
All integration tests live in tests/integration/.
Run all tests:
```bash
make test
```
### Included Tests ###
✔️ test_cli.sh
- Validates CLI argument behavior
- Ensures invalid usage returns exit code 1
✔️ test_endpoints.sh
- GET existing file → 200
- GET nonexistent → 404
- PUT new file → 201
- GET after PUT → 200 + full body check
✔️ test_concurrency.sh (optional)
- Runs parallel GETs and PUTs
- Ensures:
    - No deadlocks
    - Correct final file contents
    - Writer-priority enforcement
## 🧩 Architecture Overview ##
### Thread Pool Model ###
- Listener thread continuously accepts connections
- Each connection’s file descriptor is pushed into a bounded thread-safe queue
- Worker threads pop FDs and process requests concurrently
### Per-File Locking ###
Each file name is associated with:
- A rwlock_t *
- A reference counter
- A heap-allocated URI string
Worker behavior:
- GET → reader_lock(), read file, reader_unlock()
- PUT → writer_lock(), write/overwrite, writer_unlock()
### Writer-Priority Read–Write Lock ###
Tracks:
- active_readers
- active_writers
- waiting_readers
- waiting_writers
Rules:
- A writer must wait until no readers or writers are active
- If any writer is waiting, new readers must wait
- Writer gets exclusive access
- Readers may run concurrently when no writer is waiting
## 🧪 Manual Testing Examples ##
### GET existing file ###
```bash
echo "hello" > a.txt
curl http://127.0.0.1:8080/a.txt
```
PUT new file
```bash
curl -X PUT -H "Content-Length: 11" \
     --data-binary "new content" \
     http://127.0.0.1:8080/b.txt
```
Overwrite existing file
```bash
curl -X PUT -H "Content-Length: 3" \
     --data-binary "xyz" \
     http://127.0.0.1:8080/a.txt
```
## 🧭 Future Improvements ##
- Break down large functions into smaller, modular units to improve clarity and maintainability.
- Refactor internal logic to reduce duplication and simplify complex control flow.
- Improve documentation and inline comments, especially for concurrency mechanisms and I/O handling.
- Reorganize the codebase into more focused source files to establish cleaner separation of concerns.
- Optimize performance-critical routines such as request parsing and file read/write operations.


