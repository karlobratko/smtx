# SMTX - Shared Mutex

A single-header, high-performance shared mutex (reader-writer lock) implementation using C11 atomics, designed for efficient concurrent access patterns.

## Features

- **Lock-free implementation**: Uses atomic operations instead of traditional mutexes
- **Spin-then-yield strategy**: Optimal balance between performance and CPU utilization
- **Cache-friendly design**: Optional padding to prevent false sharing
- **Architecture-specific optimizations**: Uses CPU pause instructions when available
- **Comprehensive API**: Complete set of lock operations including timed variants
- **Customizable**: Configurable spinning behavior, backoff strategy, and yielding
- **Debugging support**: Runtime consistency validation and assertions

## Usage

1. Include the header in your project
2. Define `SMTX_IMPLEMENTATION` in exactly one source file

## Configuration Options

Define these before including the header to customize behavior:

- `SMTX_NDEBUG`: Disable debug assertions and checks
- `SMTX_ASSERT(expr)`: Override default assert implementation
- `SMTX_NEXT_SPINS(curr_spins)`: Override spin count progression strategy (default: exponential backoff)
- `SMTX_MAX_WRITER_WAIT_SPINS`: Maximum spin count when waiting for writers (default: 1024)
- `SMTX_MAX_READER_WAIT_SPINS`: Maximum spin count when waiting for readers (default: 1024)
- `SMTX_YIELD_THRESHOLD`: Spin count threshold before yielding the thread (default: 512)
- `SMTX_YIELD`: Override thread yielding mechanism (default: thrd_yield())
- `SMTX_CLOCK_ID`: Clock ID to use for timeouts (default: CLOCK_MONOTONIC)
- `SMTX_CACHE_LINE_SIZE`: Set cache line size in bytes (default: 64)
- `SMTX_PREVENT_FALSE_SHARING`: Add padding and enforce alignment to prevent false sharing

## API

### Initialization

- `smtx_init`: Initialize a shared mutex

### Shared (Reader) Lock Operations

- `smtx_lock_shared`: Acquire a shared lock (multiple readers allowed)
- `smtx_trylock_shared`: Try to acquire a shared lock without blocking
- `smtx_timedlock_shared`: Try to acquire a shared lock with timeout
- `smtx_unlock_shared`: Release a shared lock

### Exclusive (Writer) Lock Operations

- `smtx_lock_exclusive`: Acquire an exclusive lock (one writer, no readers)
- `smtx_trylock_exclusive`: Try to acquire an exclusive lock without blocking
- `smtx_timedlock_exclusive`: Try to acquire an exclusive lock with timeout
- `smtx_unlock_exclusive`: Release an exclusive lock

## Performance Considerations

- Best performance for short-duration critical sections
- For high-contention workloads, tune spin count parameters
- Enable `SMTX_PREVENT_FALSE_SHARING` for multi-socket systems
- Use trylock variants for non-blocking operations when possible

## Thread Safety

All SMTX functions are thread-safe when used correctly:
- Each lock operation must have a corresponding unlock operation
- Shared locks are held by multiple reader threads simultaneously
- Exclusive locks are held by exactly one writer thread with no active readers

## License

MIT License. See the [LICENSE](./LICENSE) file for details.