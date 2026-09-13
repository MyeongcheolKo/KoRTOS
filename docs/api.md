# Kernel API Reference

Everything below lives in [`kernel/kortos.h`](../kernel/kortos.h), the single header an application includes. Tunables live in [`config/kortos_config.h`](../config/kortos_config.h).

- [Kernel Control](#kernel-control)
- [Tasks](#tasks)
- [Semaphores](#semaphores)
- [Mutexes](#mutexes)
- [Message Queues](#message-queues)
- [Types](#types)
- [Error Codes](#error-codes-os_err_t)
- [Compile-Time Configuration](#compile-time-configuration-configkortos_configh)
- [How to Use](#how-to-use)

Every blocking call takes a `timeout` in ticks: `0` never blocks, `n > 0` blocks for up to `n` ticks, `OS_WAIT_FOREVER` blocks with no deadline. See [Timeouts](kernel.md#timeouts).

## Kernel Control

| Function | Description |
|----------|-------------|
| `void os_kernel_start(void)` | Starts the scheduler and dispatches the first task. Call once after every create call,  **never returns**. |
| `void os_idle_task_hook(void)` | Weak symbol called once per idle-loop iteration. Define it in your app to do background work, the kernel's default is empty. |

## Tasks

| Function | Description |
|----------|-------------|
| `os_err_t os_task_create(void (*handler)(void), uint8_t priority, uint32_t *stack_base, uint32_t stack_size)` | Registers a task with its own private stack. `priority` must be within `OS_PRIORITY_HIGHEST`..`OS_PRIORITY_LOWEST` (lower value = higher priority). `stack_size` is in **bytes**, and `stack_base` must meet the port's alignment (8 bytes on Cortex-M). Returns `OS_ERR_MAX_TASKS`, `OS_ERR_OUT_OF_RANGE`, or `OS_ERR_NULL_PTR` on failure. |
| `void os_task_delay(uint32_t tick_count)` | Blocks the calling task for `tick_count` ticks and yields to the next ready task. `OS_WAIT_FOREVER` blocks the task permanently. |

## Semaphores

| Function | Description |
|----------|-------------|
| `os_err_t os_sem_create(semaphore_t *sem, uint8_t initial_count, schedule_policy_t policy)` | Initializes a semaphore with a starting count and a release policy (`FIFO` or `PRIORITY`, see [Unblock Policies](kernel.md#unblock-policies)). `initial_count` may not exceed `OS_MAX_TASKS`. |
| `os_err_t os_sem_wait(semaphore_t *sem, uint32_t timeout)` | Takes the count if non-zero, otherwise blocks for up to `timeout` ticks. Returns `OS_OK` if acquired, `OS_ERR_UNAVAILABLE` if it gave up, `OS_ERR_WAITLIST_FULL` if it couldn't even queue. |
| `os_err_t os_sem_post(semaphore_t *sem)` | Releases exactly one waiter (chosen by the policy) and yields so a higher-priority release can run immediately, increments the count instead if nobody is waiting. |

## Mutexes

| Function | Description |
|----------|-------------|
| `os_err_t os_mutex_create(mutex_t *mtx)` | Initializes a mutex, unlocked with no owner. The wait list is always priority-ordered. |
| `os_err_t os_mutex_lock(mutex_t *mtx, uint32_t timeout)` | Locks if free, otherwise blocks for up to `timeout` ticks and boosts the owner's priority (chained through any mutex the owner is itself blocked on). Returns `OS_OK` owning the mutex, `OS_ERR_UNAVAILABLE` on timeout (boost undone), `OS_ERR_MTX_RECURSIVE_LOCK` if the caller already owns it, `OS_ERR_WAITLIST_FULL` if it couldn't queue. |
| `os_err_t os_mutex_unlock(mutex_t *mtx)` | Unlocks. If a task is waiting, ownership transfers directly to the highest-priority waiter (mutex stays locked) and both tasks' effective priorities are recomputed. Returns `OS_ERR_MTX_NOT_OWNER` if the caller doesn't own it. |

See [Mutexes](kernel.md#mutexes) for the handoff and inheritance rules.

## Message Queues

| Function | Description |
|----------|-------------|
| `os_err_t os_queue_create(queue_t *q, void *buffer, uint32_t item_size, uint32_t max_items, schedule_policy_t policy)` | Initializes a ring buffer over `buffer`, which must hold `max_items * item_size` bytes and is owned by the application. `policy` applies to both the send and receive wait lists. Returns `OS_ERR_OUT_OF_RANGE` if either size is 0. |
| `os_err_t os_queue_send_from_task(queue_t *q, const void *item, uint32_t timeout)` | Copies `item_size` bytes in if a slot is free, otherwise blocks for up to `timeout` ticks. Re-checks the queue on every wake. On success wakes one receiver and yields if it outranks the caller. |
| `os_err_t os_queue_recv_from_task(queue_t *q, void *item, uint32_t timeout)` | Copies the oldest item out if one exists, otherwise blocks for up to `timeout` ticks. Re-checks on every wake. On success wakes one sender and yields if it outranks the caller. |
| `os_err_t os_queue_send_from_isr(queue_t *q, const void *item)` | ISR-safe send, never blocks. Returns `OS_ERR_UNAVAILABLE` and increments `q->dropped_count` if the queue is full. |
| `os_err_t os_queue_recv_from_isr(queue_t *q, void *item)` | ISR-safe receive, never blocks. Returns `OS_ERR_UNAVAILABLE` if the queue is empty. |

See [Message Queues](kernel.md#message-queues) for the recheck-on-wake behaviour.

## Types

| Type | Purpose |
|------|---------|
| `os_err_t` | Return code for every fallible call — see below |
| `semaphore_t` | A semaphore instance; declare one and pass its address |
| `mutex_t` | A mutex instance; declare one and pass its address |
| `queue_t` | A queue instance; declare one and pass its address along with your own storage buffer |
| `schedule_policy_t` | `FIFO` or `PRIORITY` — which waiter a release picks |
| `OS_WAIT_FOREVER` | Timeout value meaning "block with no deadline" |

`TCB_t`, `waitlist_t`, `task_state_t`, `task_block_reason_t`, and `mutex_state_t` are also visible in the header because the primitive structs embed them, but applications don't construct or inspect them.

## Error Codes (`os_err_t`)

| Code | Returned when |
|------|---------------|
| `OS_OK` | the call succeeded |
| `OS_ERR_OUT_OF_RANGE` | task priority outside `OS_PRIORITY_HIGHEST`..`OS_PRIORITY_LOWEST`, or a queue `item_size` / `max_items` of 0 |
| `OS_ERR_MAX_TASKS` | already holding `OS_MAX_TASKS` tasks |
| `OS_ERR_NULL_PTR` | a required pointer argument was `NULL` |
| `OS_ERR_UNAVAILABLE` | a wait/lock/send/recv returned without the resource — timed out, or `timeout == 0` and it wasn't free |
| `OS_ERR_INVALID_SCEHDULE_POLICY` | policy was neither `FIFO` nor `PRIORITY` |
| `OS_ERR_WAITLIST_FULL` | the primitive's wait list already holds `OS_MAX_TASKS` tasks |
| `OS_ERR_SEM_INVALID_INIT_COUNT` | initial semaphore count exceeded `OS_MAX_TASKS` |
| `OS_ERR_MTX_RECURSIVE_LOCK` | the calling task already owns the mutex it tried to lock |
| `OS_ERR_MTX_NOT_OWNER` | the calling task tried to unlock a mutex it doesn't own |

## Compile-Time Configuration (`config/kortos_config.h`)

| Macro | Default | Controls |
|-------|---------|----------|
| `OS_TICK_HZ` | `1000` | scheduler tick rate — defines what one "tick" means in every delay and timeout |
| `OS_SYSTICK_CLOCK_HZ` | `16000000` | input clock the port uses to program SysTick |
| `OS_MAX_TASKS` | `20` | size of the task table, and the cap on any wait list |
| `OS_PRIORITY_HIGHEST` / `OS_PRIORITY_LOWEST` | `0` / `31` | valid task priority range |
| `OS_MAX_MTX_PER_TASK` | `4` | how many mutexes one task may hold at once |
| `OS_SCHEDULER_STACK_WORDS` | `256` | MSP stack that exception handlers run on |
| `OS_IDLE_STACK_WORDS` | `64` | idle task's private stack |

## How to Use

Applications include exactly one kernel header:

```c
#include "kortos.h"
```

That single include brings in the task, semaphore, mutex, and queue APIs, the `os_err_t` error codes, and the primitive types. `kernel_internal.h` is the core↔port glue and is not meant to be included by an application.

Every app follows the same shape: give each task a stack, register the tasks, create any synchronization objects, then hand control to the kernel.

```c
#include "kortos.h"

// each task owns a private stack, 8 byte aligned as the ARM ABI requires
uint32_t sensor_stack[1024] __attribute__((aligned(8)));
uint32_t logger_stack[1024] __attribute__((aligned(8)));

typedef struct { uint32_t id; int32_t value; } sample_t;

queue_t   sample_q;
sample_t  sample_buf[8]; // the app owns the queue's storage
mutex_t   uart_mtx;

void sensor_task(void)
{
    uint32_t id = 0;
    while (1) // a task handler must never return
    {
        sample_t s = { id++, 42 };
        os_queue_send_from_task(&sample_q, &s, OS_WAIT_FOREVER);
        os_task_delay(100); // BLOCKED for 100 ticks, other tasks run
    }
}

void logger_task(void)
{
    sample_t s;
    while (1)
    {
        if (os_queue_recv_from_task(&sample_q, &s, 1000) == OS_OK)
        {
            os_mutex_lock(&uart_mtx, OS_WAIT_FOREVER);
            // ... print s over UART, the mutex keeps the line intact
            os_mutex_unlock(&uart_mtx);
        }
    }
}

int main(void)
{
    // lower priority number = higher priority
    if (os_task_create(sensor_task, 1, sensor_stack, sizeof(sensor_stack)) != OS_OK) { /* handle */ }
    if (os_task_create(logger_task, 2, logger_stack, sizeof(logger_stack)) != OS_OK) { /* handle */ }

    if (os_queue_create(&sample_q, sample_buf, sizeof(sample_t), 8, FIFO) != OS_OK) { /* handle */ }
    if (os_mutex_create(&uart_mtx) != OS_OK) { /* handle */ }

    os_kernel_start(); // never returns
}
```

Things the API expects of you:

- **Task handlers never return.** Each one is an infinite loop; returning from a task is undefined.
- **The application owns all memory.** Task stacks and queue buffers are declared by you and passed in by pointer — the kernel does not allocate.
- **Create everything before starting.** `os_kernel_start()` never returns, so all `os_task_create`, `os_sem_create`, `os_mutex_create`, `os_queue_create` calls come first.
- **Everything returns `os_err_t`.** A creation call rejects an out-of-range priority, a null pointer, or too many tasks rather than failing silently — check the result.
- **Delays and timeouts are in ticks**, not milliseconds. The tick rate is `OS_TICK_HZ` in `config/kortos_config.h` (1000 Hz by default, so one tick is 1 ms).
- **Only `*_from_isr` calls are ISR-safe.** The task variants of send/recv, and every wait/lock, may block and must be called from a task.
- **The idle task is overridable.** Define `os_idle_task_hook()` in your app to run work in the idle loop; the kernel's default is an empty weak symbol.
- **The stack declaration is the only port-dependent line.** Every `os_*` call above is the same on any port; the `uint32_t` element type and the 8 byte alignment come from the frame the Cortex-M port builds for a new task.

For complete working programs, see [Sample Applications](kernel.md#sample-applications).
