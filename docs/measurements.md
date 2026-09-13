# Priority Inheritance Measurement

Worst-case blocking time of a high-priority task on `os_mutex_lock()` while a
lower-priority task holds the mutex and a medium-priority task runs unrelated CPU
work, with and without priority inheritance.

Benchmark: [`sample_apps/kernel/mutex_pi_benchmark.c`](../sample_apps/kernel/mutex_pi_benchmark.c)

## Setup

| | |
|---|---|
| Board | STM32F446RE Nucleo, 16 MHz HSI, `-O0` |
| Tasks | H (priority 1, measured), M (priority 3, interference), L (priority 5, mutex holder) |
| L critical section | 100 µs |
| M workload | 1 ms / 5 ms / 20 ms, swept |
| Runs per point | 100 |
| Timing | DWT `CYCCNT`, read immediately before and after H's `os_mutex_lock()` |

Inheritance was disabled for the "without" column by commenting out the
`mutex_donate_priority()` call in `os_mutex_lock()` (`kernel/kernel.c`), rebuilding, and
restoring it afterwards.

Each run constructs the classic unbounded-inversion interleaving explicitly: L
locks the mutex and signals H; H releases M and then contends for the mutex with
L's full critical section still ahead. Without inheritance M outranks L and runs its
whole burst first; with inheritance L is boosted over M and finishes first. Every run
is the same interleaving, so median and worst case are within tens of microseconds.

## Results

Blocking time of H on `os_mutex_lock()`, worst / median over 100 runs:

| M workload | Without inheritance | With inheritance |
|---|---|---|
| 1 ms | 1295 µs / 1261 µs | 257 µs / 223 µs |
| 5 ms | 5295 µs / 5261 µs | 257 µs / 223 µs |
| 20 ms | **20295 µs** / 20261 µs | **257 µs** / 223 µs |

Without inheritance, blocking is exactly `M workload + 295 µs` at every point: H
waits out M's entire burst, then L's 100 µs critical section, plus ~195 µs of kernel
overhead. Blocking scales with whatever unrelated work happens to be runnable.

With inheritance, blocking is flat at 257 µs regardless of M: L's 100 µs critical
section plus ~157 µs of kernel overhead. The bound is the critical-section length,
not the interfering workload.

## Raw output

```
with priority inheritance:
    M_WORKLOAD_US=1000, CRITICAL_SECTION_US=100, spins timed on DWT CYCCNT at 16 MHz
    Task H: worst case 257 us, median 223 us, over 100 runs

    M_WORKLOAD_US=5000, CRITICAL_SECTION_US=100, spins timed on DWT CYCCNT at 16 MHz
    Task H: worst case 257 us, median 223 us, over 100 runs

    M_WORKLOAD_US=20000, CRITICAL_SECTION_US=100, spins timed on DWT CYCCNT at 16 MHz
    Task H: worst case 257 us, median 223 us, over 100 runs

without priority inheritance:
    M_WORKLOAD_US=1000, CRITICAL_SECTION_US=100, spins timed on DWT CYCCNT at 16 MHz
    Task H: worst case 1295 us, median 1261 us, over 100 runs

    M_WORKLOAD_US=5000, CRITICAL_SECTION_US=100, spins timed on DWT CYCCNT at 16 MHz
    Task H: worst case 5295 us, median 5261 us, over 100 runs

    M_WORKLOAD_US=20000, CRITICAL_SECTION_US=100, spins timed on DWT CYCCNT at 16 MHz
    Task H: worst case 20295 us, median 20261 us, over 100 runs
```
