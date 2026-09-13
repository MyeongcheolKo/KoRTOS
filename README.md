# KoRTOS

KoRTOS is a mini RTOS kernel and peripheral driver library for ARM Cortex-M4, written from scratch without a vendor HAL or CMSIS. The kernel core is portable C with all architecture-specific code behind a port layer; the kernel itself has no assembly and no register access. The repo also includes the bare-metal pieces (linker script, startup code, syscalls) needed to run on the STM32F446RE, the board that it has been developed and tested on.

## Highlights

- **Preemptive priority scheduler** with round-robin among equal priorities, using SysTick and PendSV with separate MSP/PSP stacks
- **Semaphores, mutexes, and message queues**. Every blocking call takes a timeout (`0`, `n` ticks, or `OS_WAIT_FOREVER`), and each wait list releases in `FIFO` or `PRIORITY` order
- **Priority inheritance** on mutexes, including propagation through chains of held mutexes and is recomputed on release when a task holds several mutexes
- **ISR-safe queue** with recheck on wake. A blocked sender or receiver isn't necessarily guaranteed the next task to run once a slot frees, a higher priority task or an ISR may take it first. The woken task rechecks the queue instead of assuming the slot is still free. 
- **Portable core with swappable port**. `kernel/` is pure C, `port/arm/cortex_m4/` holds all the assembly and registers. The two build as independent static libraries alongside the HAL
- **Register-level HAL** for GPIO, SPI, I2C, USART, and CAN, with polling and interrupt-driven modes and weak callbacks
- Hand-written linker script and startup code, `printf` over ITM, plain `make` build
- **Bare-metal from the vector table up**: linker script, startup code, `make` build
- **24 sample apps**, one per feature, each with the expected output in its header comment

## Priority inheritance benchmark

Blocking time of a high-priority task on `os_mutex_lock()` while a low-priority task holds the mutex for a fixed 100 µs and a medium-priority task runs unrelated CPU work. Worst case over 100 runs, timed on the DWT cycle counter:

| Interfering workload | Without inheritance | With inheritance |
|---|---|---|
| 1 ms | 1295 µs | 257 µs |
| 5 ms | 5295 µs | 257 µs |
| 20 ms | 20295 µs | 257 µs |

Without inheritance the blocking time grows with the interfering task's workload. With inheritance it stays at the critical section length plus kernel overhead. Setup, method, and raw output are in [docs/measurements.md](docs/measurements.md). The benchmark is [`mutex_pi_benchmark.c`](sample_apps/kernel/mutex_pi_benchmark.c).

## Project structure

```
.
├── kernel/                    # portable RTOS core (pure C, no arch code)
│   ├── kernel.c               # scheduler, tasks, tick, semaphores, mutexes, queues
│   ├── kortos.h               # public API, apps include this
│   └── kernel_internal.h      # core <-> port interface
├── port/arm/cortex_m4/        # architecture port (all ARM asm + register access)
│   └── port.c/.h              # context switch, SysTick/PendSV setup, critical sections
├── config/
│   └── kortos_config.h        # compile-time kernel config (tick rate, task count, ...)
├── kortos_hal/STM32F446xx/    # peripheral HAL (GPIO, SPI, I2C, USART, CAN, RCC), chip-specific
├── bsp/STM32F446xx/           # board bring-up: startup.c, linker_script.ld, syscalls (ITM printf)
├── sample_apps/
│   ├── kernel/                # RTOS demos (mutex_pi_benchmark.c, queue_from_isr.c, ...)
│   └── hal/                   # peripheral demos (LED_toggle.c, CAN_polling_loopback.c, ...)
├── docs/                      # detailed documentation (see below)
└── makefile
```

```
          MSP                               PSP
           │                                 │
           ▼                                 ▼
┌──────────────────────┐      ┌──────────────────────────────┐
│  scheduler_stack[]   │      │  idle_task_stack[]           │  kernel-owned
│  exception handlers  │      ├──────────────────────────────┤
│  (SysTick, PendSV)   │      │  task stacks, one per task   │  app-owned
└──────────────────────┘      │  ...  up to OS_MAX_TASKS     │
                              └──────────────────────────────┘
```

The kernel (`libkortos.a`) and HAL (`libhal.a`) don't depend on each other. Porting to another architecture means writing a new `port/` against the same interface; `kernel/` doesn't change.

## Quick start

Build and flash (needs `arm-none-eabi-gcc` and OpenOCD):

```
make APP=sample_apps/kernel/mutex_priority_donate.c   # any file under sample_apps/
make load                                             # starts OpenOCD; flash with arm-none-eabi-gdb
```

Output goes to the SWV ITM console. Each sample app's header comment describes what the output should look like.

A minimal app: give each task a stack, register the tasks, create any primitives, then start the kernel.

```c
#include <stdio.h>
#include "kortos.h"

uint32_t producer_stack[1024] __attribute__((aligned(8)));
uint32_t consumer_stack[1024] __attribute__((aligned(8)));

queue_t  q;
uint32_t q_buf[8];               // the app owns the queue's storage

void producer(void)
{
    uint32_t n = 0;
    while (1)
    {
        os_queue_send_from_task(&q, &n, OS_WAIT_FOREVER);
        n++;
        os_task_delay(100);      // blocks 100 ticks; other tasks run
    }
}

void consumer(void)
{
    uint32_t n;
    while (1)
        if (os_queue_recv_from_task(&q, &n, 1000) == OS_OK)
            printf("got %lu\n", (unsigned long)n);
}

int main(void)
{
    os_task_create(producer, 1, producer_stack, sizeof(producer_stack));  // lower number = higher priority
    os_task_create(consumer, 2, consumer_stack, sizeof(consumer_stack));
    os_queue_create(&q, q_buf, sizeof(uint32_t), 8, FIFO);
    os_kernel_start();           // never returns
}
```

Every call returns `os_err_t`. See [docs/api.md](docs/api.md) for the full API.

## Documentation

| | |
|---|---|
| [docs/kernel.md](docs/kernel.md) | Scheduler, task lifecycle, context switch, semaphores, mutexes and priority inheritance, queues, design choices, kernel sample apps |
| [docs/api.md](docs/api.md) | Every public function, type, error code, and config macro, and what the kernel expects from an app |
| [docs/hal.md](docs/hal.md) | Driver architecture, GPIO / SPI / I2C / USART / CAN features, HAL sample apps |
| [docs/bare-metal.md](docs/bare-metal.md) | Memory map, linker script, startup code, build system, flashing, ITM debug output |
| [docs/measurements.md](docs/measurements.md) | Priority inheritance benchmark: setup, results, raw output |

## See it in action

[YouTube playlist](https://www.youtube.com/playlist?list=PLLaVu9P3il1isXzX8xk3gsnbkaftZR-8b) of the kernel and driver sample applications running on the board.

## Ideas for future improvements

- Additional ports (Cortex-M0, RISC-V) to exercise the port layer
- Stack canaries or MPU protection
- Migrate the older drivers (GPIO/SPI/I2C/USART) to the `KHAL_status_t` return convention the CAN driver uses
