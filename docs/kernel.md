# Kernel

A preemptive, priority-based scheduler that manages user tasks plus an idle task, with semaphores, mutexes (priority inheritance), and message queues as blocking primitives.

The kernel is split into a **portable core** (`kernel/` - scheduling logic, TCB bookkeeping, blocking/tick handling, every primitive; pure C) and an **architecture port** (`port/arm/cortex_m4/` - context switch, SysTick/PendSV setup, critical sections; all ARM asm and register access). Only the port needs rewriting to target a different architecture.

- [Scheduler](#scheduler)
- [Task Lifecycle](#task-lifecycle)
- [Context Switch Flow](#context-switch-flow)
- [Timeouts](#timeouts)
- [Semaphores](#semaphores)
- [Mutexes](#mutexes)
- [Message Queues](#message-queues)
- [Design Choices](#design-choices)
- [Sample Applications](#sample-applications)

## Scheduler

The scheduling policy is portable C. The hardware mechanisms that drive it are supplied by the port layer, marked *(port)* below:

- **SysTick Timer** *(port)* - Generates periodic interrupts (1 ms ticks by default) to drive scheduling
- **PendSV Exception** *(port)* - Performs the actual context switch at the lowest exception priority
- **Dual Stack Pointers** *(port)* - MSP for kernel/handlers, PSP for user tasks
- **Priority scheduling** - On each tick the highest-priority `READY` task is selected (lower priority number = higher priority). The idle task sits at the lowest priority and is only selected when nothing else is `READY`
- **Round-robin within a priority level** - Tasks of equal priority take turns instead of the lowest-indexed one always winning, so no task starves its equal-priority peers
- **Blocking primitives** - Tasks can block on a delay, a semaphore, a mutex, or a queue, with blocked tasks skipped entirely by the scheduler
- **Yield on wake** - Any operation that makes a higher-priority task `READY` (post, unlock, queue send/recv) pends a context switch immediately rather than waiting for the next tick

```
          MSP                               PSP
           │                                 │
           ▼                                 ▼
┌──────────────────────┐      ┌──────────────────────────────┐
│  scheduler_stack[]   │      │  idle_task_stack[]           │  kernel-owned
│  exception handlers  │      ├──────────────────────────────┤  (static, kernel.c)
│  (SysTick, PendSV)   │      │  worker_stack[]   (task 1)   │
└──────────────────────┘      │  logger_stack[]   (task 2)   │  app-owned,
                              │  ...  up to OS_MAX_TASKS     │  one array per task
                              └──────────────────────────────┘
```

Each task has a **base priority** (set at creation, never changes) and an **effective priority** (what the scheduler actually uses). They are equal unless a mutex boosts the task - see [Priority Inheritance](#priority-inheritance).

## Task Lifecycle

- All tasks start in `READY` state
- A task transitions to `BLOCKED` when it calls `os_task_delay()`, or when it waits on a semaphore, mutex, or queue that is unavailable
- Each blocked task records *why* it blocked (`block_reason`), which primitive's wait list it sits in, and if the block has a deadline, the `wakeup_tick` it should wake at
- The scheduler skips blocked tasks and selects the highest-priority `READY` task in round-robin manner
- Global `systick_count` gets updated at every `SysTick_Handler`
- A blocked task returns to `READY` when its `wakeup_tick` is reached (delay elapsed, or a wait timed out), or when another task or ISR releases the primitive it was waiting on

```
    ┌──────────┐  os_task_delay() / os_sem_wait() /  ┌─────────┐
    │  READY   │  os_mutex_lock() / os_queue_*()     │ BLOCKED │
    │          │ ----------------------------------> │         │
    └──────────┘                                     └─────────┘
         |                                                |
         |  wakeup_tick reached  /  post / unlock / send  |
         └────────────<───────────<────────────<──────────┘
```

## Context Switch Flow

```
SysTick fires (every 1ms)
           |
  ┌───────────────────┐
  │ Update tick count │
  │   Unblock tasks   │
  │   Pend PendSV     │ <- Doesn't switch here, just sets pending bit
  └───────────────────┘
           |        (after all higher-priority interrupts complete)
┌──────────────────────┐
│        PendSV        │
│ Save R4-R11 manually │ <- Hardware auto-saves R0-R3, R12, LR, PC, xPSR
│   Select next task   │
│    Restore R4-R11    │
└──────────────────────┘
```

A blocking call from a task (post, unlock, delay, ...) takes the same PendSV path via `port_yield()`

## Timeouts

Every blocking wait that takes a `timeout` parameter has value interpreted in the same three meanings:

| `timeout` | Behavior |
|-----------|----------|
| `0` | never blocks, returns `OS_ERR_UNAVAILABLE` immediately if the primitive isn't available |
| `n > 0` | blocks until released, or until `n` ticks elapse then returns `OS_ERR_UNAVAILABLE` |
| `OS_WAIT_FOREVER` | blocks with no deadline |

## Unblock Policies

Each semaphore and queue chooses how a release picks among multiple waiters (mutex is hardcoded to PRIORITY):

| Policy | Releases | Tradeoff |
|--------|----------|----------|
| `FIFO` | the task that has been waiting longest | fair, no waiter can starve, but a high priority task can sit behind lower priority ones |
| `PRIORITY` | the highest priority waiter, regardless of arrival order | urgent work goes first, but a steady stream of high priority waiters starves the rest |

The two policies are demonstrated against deliberately identical scenarios in `sem_post_fifo.c` and `sem_post_priority.c`.

## Semaphores

A counting semaphore for task synchronization, created with an **initial count** and an **unblock policy**. Waiting takes the count when it is non-zero. When the count is zero the calling task moves to `BLOCKED`, is appended to that semaphore's own *waitlist*, and yields. Posting either releases exactly one waiting task, or if nobody is waiting, increments the count so a later wait succeeds without blocking.

## Mutexes

A mutex is a lock with an owner. Unlike a semaphore, only the task that locked it can unlock it, and the kernel uses that ownership for priority inheritance.

- **Ownership** - `os_mutex_unlock()` from a non-owner returns `OS_ERR_MTX_NOT_OWNER`
- **Non-recursive** - locking a mutex you already own returns `OS_ERR_MTX_RECURSIVE_LOCK` instead of deadlocking
- **Priority-based waitlist** - a mutex always releases its highest priority waiter, there is no FIFO option
- **Direct handoff** - when the owner unlocks with waiters present, ownership transfers to the highest-priority waiter and the mutex stays locked, so a third task can't grab it between the unlock and the waiter running
- **Per-task ownership tracking** - each TCB records the mutexes it holds (up to `OS_MAX_MTX_PER_TASK`), which the priority recompute below relies on

### Priority Inheritance

The problem: low-priority L holds a mutex, high-priority H blocks on it, and medium-priority M (which never touches the mutex) becomes runnable. M outranks L, so M runs, L never finishes its critical section, and H waits on M indefinitely. H's blocking time depends on whatever unrelated work happens to be runnable, not on the critical section from holding the mutex.

Priority inheritance bounds it:

**Donation** (`os_mutex_lock()`) - the owner's effective priority is raised to the waiter's if the waiter outranks it. If the owner is itself blocked on another mutex, the boost propagates to that mutex's owner, and so on until reaching a task that isn't blocked on a mutex. So in a chain H -> mtx1 (held by M) -> mtx2 (held by L), both M and L are boosted to H's priority. Propagation stops after `OS_MAX_TASKS` hops as a guard against cycles.

**Release** (`os_mutex_unlock()` or a waiter timing out) - the affected owner's effective priority is recomputed from scratch as the maximum of its base priority and every task waiting on any mutex it still owns, not just the mutex that changed. This matters when a task holds two mutexes with a waiter on each, releasing one waiter must leave the boost from the other intact (see `mutex_priority_release.c`). The recompute walks the same chain as donation did, so a two-hop boost is undone two hops away (see `mutex_chained_priority.c`, phase 2).

The effect of priority inheritance is measured in [measurements.md](measurements.md): with a 20 ms interfering task, H's worst-case blocking is 20295 µs without inheritance and 257 µs with it.

## Message Queues

A fixed-size ring buffer for passing items between tasks, or between an ISR and a task, by copy.

- **Application-owned storage** - `os_queue_create()` takes a buffer pointer, item size, and item count, the kernel never allocates
- **Fixed item size, copied by value** - `os_queue_send_*()` copies `item_size` bytes in, `reos_queue_recv_*()` copies them out, the buffer can be an array of any struct type
- **Two waitlists** - tasks blocked because the queue is full sit in `send_waitlist`, tasks blocked because it is empty sit in `recv_waitlist`. Both use the queue's `FIFO`/`PRIORITY` policy
- **Blocking with timeouts** - `os_queue_send_from_task()` / `os_queue_recv_from_task()` block for up to `timeout` ticks
- **Wake the other side** - a successful send wakes one receiver (and vice versa) and yields if it outranks the caller
- **ISR-safe variants** - `os_queue_send_from_isr()` / `os_queue_recv_from_isr()` never block. A failed ISR send increments `dropped_count`, a diagnostic counter that never resets

### Recheck on Wake

Being woken from a queue waitlist means the queue changed, not that the slot is still free. Between the wake and the woken task actually running, a higher-priority task or an ISR can take the freed slot (or the arrived item). So after every wake the task rechecks the queue. If it's full (or empty) again, the task reblocks with the time remaining from its original deadline rather than a fresh timeout, so the overall wait still matches what the caller asked for. `queue_recheck_on_wake.c` sets up a "thief" task that takes the slot on every wake and shows the victim timing out at 150 ticks instead of corrupting the buffer or waiting forever.

## Design Choices

- **Dummy stack frame** - Created so the first context switch works. When a task runs for the first time, there's no "previous context" to retrieve, so we initialize the stack with a fake frame.

- **Blocking state + SysTick timer** - The delay time for software delay is actually (task delay + delays of other tasks). For example, if Task1 wants a 1000ms delay but Task2-4 also have delays of 500ms, 250ms, and 2000ms: Task1's real wait time = 1000 + 500 + 250 + 2000 = 3750ms. By adding a blocking state and using the SysTick timer, a blocked task is skipped during scheduling and the scheduler immediately moves to the next ready task. This way each task's delay is independent of what other tasks are doing.

- **Idle task** - Always `READY` and never blocks, so `os_schedule_next_task()` always has a valid task to select when all other tasks are blocked.

- **PendSV for context switching** - Chose PendSV instead of doing it in SysTick because PendSV has lower priority, so it only gets executed after all other interrupts. This way we won't exit an interrupt handler due to a context switch, which causes a usage fault. We only pend the PendSV and do the context switch when all other interrupts and exceptions are dealt with.

- **Naked functions** - Used for `port_switch_to_psp`, `PendSV_Handler`, and `port_init_scheduler_stack` (all in the port layer) to deal with the prologue and epilogue of C functions corrupting LR:
  - `port_switch_to_psp`: Prologue would push LR to the old stack (MSP), then epilogue would pop from the new stack (PSP) -> corruption
  - `PendSV_Handler`: Need manual control over what gets pushed/popped to the stack and where for context switching + function calls corrupt the EXC_RETURN value in LR
  - `port_init_scheduler_stack`: Modifying MSP itself, prologue/epilogue would use old/new MSP inconsistently

- **Race condition in `os_task_delay`** - Disabled interrupts while setting `wakeup_tick` and `current_state` to prevent a race condition with `SysTick_Handler`. Without this, SysTick could update `systick_count` between reading it and setting the blocked state, corrupting the task's wake-up deadline. `unblock_tasks` compares the signed difference (`systick_count - wakeup_tick >= 0`) so it also stays correct when the tick counter wraps around.

- **Per-primitive waitlists** - Each semaphore, mutex, and queue owns its waitlist(s) instead of the kernel keeping one global list of blocked tasks. A release only has to scan the tasks waiting on *that* primitive, and the unblock policy is a property of the primitive rather than a kernel-wide setting.

- **Releasing yields immediately** - A post, unlock, or queue send/recv can make a higher-priority task `READY`, so it pends a context switch rather than letting the tick handler get to it. Without this the released task would sit `READY` for up to a full tick even though it outranks the task that released it.

- **Mutex handoff instead of unlock-then-wake** - On unlock with waiters, ownership goes directly to the top waiter rather than setting the mutex free and waking the waiter to re-contend. No other task can grab the mutex in between, and the waiter's `os_mutex_lock()` returns already owning it.

- **Recompute from scratch on release** - Priority inheritance release doesn't try to subtract the departing waiter's boost, it recomputes the owner's effective priority as `max(base, all waiters on all owned mutexes)`.

- **`from_isr` variants never block** - An ISR has no task context to block, so the ISR-safe queue calls are the task calls with `timeout` hardcoded to 0 plus a drop counter. They are separate functions so it's clear at the call site that they can't block.

## Sample Applications

The `sample_apps/kernel/` directory contains runnable demos of the scheduler and its primitives. Each one is standalone, build with `make APP=<path>` (see [Build System](bare-metal.md#build-system-makefile)) and watch the output over [ITM](bare-metal.md#itm-debug-output). Every file opens with a comment block explaining the task layout, the expected output, and how to read it.

| Application (`sample_apps/kernel/`) | Demonstrates |
|-------------|--------------|
| `round_robin_priority.c` | Priority preemption and round-robin rotation among equal-priority tasks |
| `sem_timeout.c` | Semaphore waits expiring - nothing ever posts, so every wait times out independently |
| `sem_post_fifo.c` | A post releasing a blocked waiter, with `FIFO` draining the wait list in arrival order |
| `sem_post_priority.c` | The same mechanism with `PRIORITY`, draining the wait list by task priority instead |
| `mutex_basic.c` | Lock, recursive-lock rejection, non-owner unlock rejection, blocking lock with handoff, and lock timeout |
| `mutex_priority_donate.c` | Priority inheritance resolving an inversion: the boosted low-priority owner outranks a middle task that never blocks, finishes, and unlocks |
| `mutex_priority_release.c` | A task holding two mutexes with a waiter on each - releasing one waiter's boost must not drop the other's |
| `mutex_chained_priority.c` | Chained inheritance: a boost propagating two hops (H -> M -> L), then being undone two hops away when H times out |
| `mutex_pi_benchmark.c` | Measures H's blocking time with/without inheritance as interfering work grows - results in [measurements.md](measurements.md) |
| `queue_basic.c` | Blocking producer/consumer over a 5-slot queue; producer blocks when full, wakes when a slot frees, FIFO order preserved |
| `queue_from_isr.c` | `os_queue_recv_from_isr()` + `os_queue_send_from_isr()` from a real EXTI button interrupt, forwarding a sample to a task |
| `queue_recheck_on_wake.c` | A higher-priority task takes the freed slot on every wake; the blocked sender re-checks, re-blocks with its remaining time, and times out instead of corrupting the queue |
| `CAN_loopback_mutex_queue.c` | CAN driver + kernel integration: CAN1 loopback, RX ISR -> queue -> task, TX guarded by a mutex under priority inversion |
