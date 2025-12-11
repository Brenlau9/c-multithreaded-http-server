# Multithreaded HTTP Server in C #

![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)
![CI](https://github.com/brenlau9/c-multithreaded-http-server/actions/workflows/ci.yml/badge.svg)

A multithreaded HTTP/1.1 server implemented in C, demonstrating systems programming, concurrency primitives, and low-level networking.

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

## What This Project Demonstrates

- Systems programming with C, POSIX sockets, and file I/O
- Concurrency and synchronization (mutexes, condition variables, rwlocks)
- Design and implementation of a multithreaded server
- HTTP request parsing and robust error handling
- Reproducible integration testing workflows
- CI via GitHub Actions
- Experience with Linux tooling, Makefiles, and debugging

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
        ├── test_put_handler.sh
        └── test_concurrency.sh
    └── unit/
        ├── test_queue.c
        ├── test_rwlock.c
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
### Writer-Priority Read–Write Lock
- Ensures PUT operations get exclusive access.
- Allows multiple concurrent GET requests when no writer is waiting.
- Prevents writer starvation through explicit writer-priority logic.

## 🧭 Future Improvements ##
- Add DELETE support
- Improve modularity and reduce duplication
- Expand test coverage for concurrency and edge cases
- Add graceful shutdown for worker threads

## 📄 License
This project is licensed under the MIT License. See the [LICENSE](./LICENSE) file for details.

