# Zero-Copy Async Runtime Engine

High-performance, lock-free C++26 asynchronous runtime engine in the `async_runtime` namespace. Integrates Linux `io_uring` with `std::execution` (Senders/Receivers) for zero-copy concurrency.

Keep updating this file as the project evolves. This is a living document. Dont add unnecessary comments to the source code. Use this file for design notes, architecture decisions, and TODOs. Keep this file for concise project documentation, and for rarely useful information, instead create a skill using `skill-creator` skill.

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
- **CI/CD Verification:** GitHub Actions builds/tests via GCC 15, checks format via `clang-format`, and lints via `clang-tidy`.
- **Clang-Tidy CI Workarounds:** Removes `stdexec-src/.clang-tidy`, strips GCC-specific flags from `compile_commands.json`, and invokes `clang-tidy` targeting `libc++` (`-extra-arg=-stdlib=libc++`) to bypass parsing incompatibilities.

## Architecture Decisions

- **Pimpl for liburing:** `io_context` uses `struct ring_impl` in implementation files; public headers never include `<liburing.h>`.
- **Bridge Methods:** `io_context::submit_*` take typed params and callback, allowing Senders to live in headers without including liburing.
- **Explicit Sender Types:** All `async_*` methods return named nested structs; `auto` deduction fails across translation units.
- **`exec::start_detached`:** Use `exec::start_detached` from `<exec/start_detached.hpp>`, as `stdexec::start_detached` is deprecated.
- **Thread Pool Queue:** Lock-free work-stealing Chase-Lev deque managing pointers to tasks (`std::function<void()>*`) to prevent data races on task objects, plus a mutex-protected global fallback queue for external submissions.
- **Registered Buffers:** Expose `register_buffers` and `unregister_buffers` using C++ standard types (`std::span` of `std::span<std::byte>`) translated to `iovec` internally. Fixed buffer I/O is exposed via `async_read_fixed` and `async_write_fixed` returning explicit sender types.
- **Cancellation:** Support cancellation of pending asynchronous I/O requests via `stdexec::stop_token` using the generic `detail::stop_helper` template which coordinates cancellation with `io_uring_prep_cancel` (by cancel SQEs referencing the request's heap-allocated completion callback). Maps `-ECANCELED` to `set_stopped`.
- **C++ Module Scanning:** Disabled scanning via `set(CMAKE_CXX_SCAN_FOR_MODULES OFF)` in CMake to avoid toolchain dependency scanning failures on compiler-specific module flags during CI runs.
- **Chase-Lev Deque Concurrency:** Implemented `work_stealing_deque` holding pointer-to-task (`std::function<void()>*`) to prevent data races on task object moves during concurrent pop/steal. Uses atomic CAS on `top_` to coordinate concurrent pops and steals on single-element states. Retains resized `circular_array` structures in `history_` for lifetime safety.
- **Thread Pool Loop:** Worker threads loop: pop local -> pop global -> steal from others. Threads spin-yield 32 times before falling back to wait on a condition variable.

**TODO:**
1. Add `std::hazard_pointer` and RCU reclamation when GCC/libstdc++ and Clang/libc++ fully ship `<hazard_pointer>` and `<rcu>` (P2530).
