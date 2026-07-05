# Architecture & Concurrency Design

This document details the internal design and concurrency patterns of the `async_engine` runtime.

---

## 1. CPU Scheduling & Work-Stealing Thread Pool

The thread pool scheduler (`thread_pool_scheduler`) is designed for high-throughput task execution with minimal lock contention.

### Chase-Lev Work-Stealing Deque
Each worker thread maintains a thread-local queue based on a lock-free Chase-Lev deque (`work_stealing_deque`):
* **Single-Producer, Multi-Consumer**: The owning thread pushes and pops tasks from the bottom of the queue. Other idle worker threads steal tasks from the top.
* **Pointer-to-Task Storage**: The queue stores heap-allocated pointers to tasks (`std::function<void()>*`) instead of direct task objects. This prevents data race hazards during concurrent steals and pops, as `std::function` objects are not trivially copyable or movable.
* **Atomic CAS Coordination**: When a deque contains a single task, concurrent pop and steal operations on that task are resolved using an atomic `compare_exchange_strong` on the `top_` index.
* **Resizing Safety**: If a deque runs out of space, the circular array grows by doubling capacity. To ensure thread safety for concurrent stealers, the old arrays are retained in a `history_` vector and kept alive for the lifetime of the deque.

### Worker Execution Loop
When a worker thread is idle, it falls back to the following lookup hierarchy to find work:
1. **Local Deque**: Pop a task from its thread-local deque.
2. **Global Fallback Queue**: Pop from the global task queue (which handles external tasks or submissions outside worker threads and is protected by a mutex).
3. **Work-Stealing**: Steal a task from another worker's deque by selecting a target worker randomly (using a thread-local LCG RNG) and attempting a steal.
4. **Spin-Yield Loop**: If no task is found, the thread yields execution using `std::this_thread::yield()` up to 32 times while retrying the search.
5. **Conditional Sleeping**: If the spin-yield phase completes without finding a task, the thread increments the count of sleeping workers and waits on a condition variable. It is woken up when new tasks are enqueued.

---

## 2. I/O Context & `io_uring` Mapping

The `io_context` manages the submission and completion queues of the Linux `io_uring` instance.

### Pimpl Idiom for Encapsulation
To keep compilation times fast and prevent namespace pollution, public headers never include `<liburing.h>` or expose low-level `io_uring` details. Instead:
* The public header `io_context.hpp` defines a forward-declared `struct ring_impl`.
* The private source file `io_uring_ctx.cpp` defines `ring_impl`, which wraps the `io_uring` struct, submission mutex, and callback tracking.
* Bridge methods like `submit_read` are used to translate public sender requests into `io_uring` SQEs.

### Event Processing Loop
When `io_context::run()` is called:
1. The thread enters a loop calling `io_uring_wait_cqe` to block until completions (CQEs) are available.
2. It processes all available CQEs in the ring, invoking the completed tasks' callback functions.
3. Every submitted SQE maps its completion callback (heap-allocated `io_callback`) to the SQE's user data pointer (`io_uring_sqe_set_data`).
4. On CQE resolution, the callback is read, executed with the operation result (`io_result`), and deleted.

---

## 3. Buffer Management & Zero-Copy I/O

To achieve zero-copy networking and file system throughput, `io_context` supports registered buffer configurations.

* **Buffer Registration**: Standard buffer layouts (`std::span<const std::span<std::byte>>`) are registered in `io_uring` using `io_uring_register_buffers` (internally mapped to `iovec` structures).
* **Fixed operations**: Senders returned by `async_read_fixed` and `async_write_fixed` pass the buffer index directly to the submission ring (`io_uring_prep_read_fixed`/`io_uring_prep_write_fixed`), bypassing virtual memory mapping lookup overhead in the kernel.

---

## 4. Cancellation Model

Asynchronous operations can be canceled natively using C++ standard execution stop tokens:

1. **`detail::stop_helper`**: Connects to the receiver's stop token (if cancellation is possible).
2. **Cancellation Submission**: If a stop is requested (e.g. via `stop_token.request_stop()`), the stop callback retrieves the operation's active callback pointer and submits a cancel SQE (`io_uring_prep_cancel`) referencing it.
3. **Pipeline Routing**: If the I/O request is successfully canceled, `io_uring` resolves the CQE with `-ECANCELED`. The receiver's callback maps this result code to the `stdexec::set_stopped` pipeline channel.
