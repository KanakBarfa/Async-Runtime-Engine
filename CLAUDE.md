# Zero-Copy Async Runtime Engine

High-performance, lock-free C++26 asynchronous runtime engine in the `async_runtime` namespace. Integrates Linux `io_uring` with `std::execution` (Senders/Receivers) for zero-copy concurrency.

## C++26 Directives

- **Asynchronous Chaining:** Strictly utilize `std::execution` Senders and Receivers. Avoid raw coroutines (`co_await`).
- **Memory Management:** Strictly use smart pointers, RAII, and C++26 memory hazard management (`std::hazard_pointer`/`std::rcu`) for lock-free reads. No naked allocations.
- **Error Handling:** Use `std::expected` for all I/O and runtime errors. No exceptions on the hot path.
- **Buffer Management:** Use `std::span` or `std::mdspan` for all non-owning buffer views.
- **Simplifying Member Functions:** Utilize explicit object parameters (deducing `this`) to reduce boilerplate.
- **Documentation:** Do not generate comments in the source code.

## Directory Structure

- `include/async_runtime/`: Public headers exposing Senders, Receivers, Schedulers.
- `src/`: Private implementation (internal lock-free structures, `io_uring` translation layers).
- `tests/`: Integration and end-to-end tests.
- `examples/`: Minimal socket server example using the runtime.

## Technical Environment

- **Compiler:** GCC 15.2, C++26
- **liburing:** 2.14 (PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig)
- **std::execution (P2300):** NVIDIA stdexec reference implementation (fetched via CMake)
- **std::hazard_pointer:** Deferred (unsupported in GCC 15 stdlib)

## Architecture Decisions

- **Pimpl for liburing:** `io_context` uses `struct ring_impl` in implementation files; public headers never include `<liburing.h>`.
- **Bridge Methods:** `io_context::submit_*` take typed params and callback, allowing Senders to live in headers without including liburing.
- **Explicit Sender Types:** All `async_*` methods return named nested structs; `auto` deduction fails across translation units.
- **`exec::start_detached`:** Use `exec::start_detached` from `<exec/start_detached.hpp>`, as `stdexec::start_detached` is deprecated.
- **Thread Pool Queue:** Mutex queue is currently used; Chase-Lev work-stealing deque has data races with `std::function` and is deferred.

## Status

Scaffold complete. All targets build and 2/2 tests pass. Run `/build-and-test` to compile and run tests.

**TODO:**
1. Replace mutex queue with lock-free work-stealing (Concurrency)
2. Implement `io_uring` registered buffers for true zero-copy (I/O Integration)
3. Add `stdexec::stop_token` cancellation support
4. Add `std::hazard_pointer` reclamation when GCC ships it
