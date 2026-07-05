# Zero-Copy Async Runtime Engine

## Project Overview

This document defines the architectural guidelines and agent directives for building a high-performance, lock-free asynchronous runtime engine in **C++26**. The engine provides a zero-copy I/O event loop utilizing `io_uring` and a lock-free work-stealing thread pool based on the `std::execution` (Senders/Receivers) framework. It is designed to be a pluggable namespace (`async_runtime`) that can be embedded into existing architectures to eliminate kernel-to-user space overhead and lock contention.

## Core Goals

* **Zero-Copy I/O:** Bypass standard system calls using Linux `io_uring` and shared memory ring buffers.
* **Lock-Free Concurrency:** Eliminate `std::mutex` contention on the fast path using atomic operations and modern memory reclamation techniques (`std::hazard_pointer` or `std::rcu`).
* **Execution Framework:** Distribute tasks across CPU cores utilizing C++26 `std::execution` Senders and Receivers to construct directed acyclic graphs of asynchronous work without centralized locks.
* **Drop-In Modularity:** Expose the runtime as an isolated namespace with a clean API, decoupled from application-level routing logic.

## C++26 Technical Directives

All code generation must strictly adhere to the C++26 standard.

* **Asynchronous Chaining:** Strictly utilize `std::execution` (Senders and Receivers) for all asynchronous I/O and task chaining. Avoid raw coroutines (`co_await`) unless necessary for interfacing with specific legacy patterns.
* **Memory Management:** Strictly use smart pointers, RAII, and C++26 memory hazard management (`std::hazard_pointer` / `std::rcu`) for lock-free reads. Naked allocations are forbidden.
* **Error Handling:** Use `std::expected` for all I/O and runtime errors. Exceptions are strictly forbidden in the hot path.
* **Buffer Management:** Use `std::span` or `std::mdspan` for all non-owning buffer views.
* **Simplifying Member Functions:** Utilize explicit object parameters (deducing `this`) to reduce boilerplate in perfect forwarding and CRTP patterns.
* **Documentation:** Do not generate comments in the source code.

## Agent Roles and Responsibilities

### 1. Architectural Agent
* **Scope:** Project structure, namespace isolation, API design.
* **Rules:** Ensure the `async_runtime` namespace does not leak internal dependencies (like `liburing`) into the public headers. Enforce strict Pimpl (Pointer to Implementation) idioms where necessary to keep the ABI clean.

### 2. Concurrency Agent
* **Scope:** Thread pool management, execution contexts, lock-free data structures.
* **Rules:** All concurrent queues must be lock-free. Use `std::atomic` with explicit and correct memory ordering (`std::memory_order_acquire`, `std::memory_order_release`). Rely heavily on `std::execution` schedulers to manage task distribution.

### 3. I/O Integration Agent
* **Scope:** Kernel interaction via `io_uring`, socket polling, buffer allocation.
* **Rules:** Map network sockets directly to Submission Queue Entries (SQEs) and Completion Queue Entries (CQEs). Ensure the ring buffers are safely synchronized with the concurrency layer without locks, exposing readiness as C++26 Senders.

### 4. Code Generation Agent
* **Scope:** Writing the actual `.hpp` and `.cpp` files.
* **Rules:** Write modern, clean, and concise C++26. Do not generate comments in the code under any circumstances unless explicitly overridden by the user. Ensure all headers include `#pragma once`.

## Directory Structure

* `include/async_runtime/`: Public headers exposing the runtime API (Senders, Receivers, Schedulers).
* `src/`: Private implementation files, internal lock-free structures, and `io_uring` translation layers.
* `tests/`: Stress tests and memory safety validation.
* `examples/`: Minimal implementations demonstrating how to embed the engine into a basic socket server using `std::execution`.