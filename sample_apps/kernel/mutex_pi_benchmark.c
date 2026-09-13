#include <stdint.h>
#include <stdio.h>

#include "kortos.h"

#if !defined(__SOFT_FP__) && defined(__ARM_FP)
#warning "FPU is not initialized, but the project is compiling for an FPU. Please initialize the FPU before use."
#endif

/*
mutex_pi_benchmark.c

Measures H's worst-case and median blocking time on os_mutex_lock() as M's
CPU workload grows, with and without priority inheritance. See measurements.md 
for results.

Tasks:
    taskH, priority 1 (highest) - the task being measured
    taskM, priority 3           - interference, touches no mutex, just burns CPU
    taskL, priority 5 (lowest)  - holds mtx for a fixed ~100us critical section

Sequence:
    1. L locks mtx, then posts sem_h_go. The sem post yields to H immediately, so L is
        preempted at the very start of its critical section (full 100us remaining).
    2. H posts sem_m_go, making M READY. H is higher priority, so H keeps running.
    3. H timestamps and calls os_mutex_lock(). mtx is held by L, so H blocks.
    4. Without inheritance: M (3) outranks L (5), so M runs its whole burst before
        L can finish and unlock. H waits M_WORKLOAD_US + CRITICAL_SECTION_US + kernel overhead.
    With inheritance: L is boosted to 1, outranks M, finishes its 100us critical
        section and unlocks the mutex. H waits CRITICAL_SECTION_US + kernel overhead.
    5. H timestamps, records, unlocks, and waits for L's next post.

To compare against inheritance OFF: comment out the mutex_donate_priority()
call sites in kernel.c's os_mutex_lock().

Timing: busy_spin_us() spins until the DWT cycle counter (get_cycle_count()) has
advanced by the requested number of cycles, so M's workload and L's critical section
are exactly their labelled length regardless of optimization level or loop codegen.
*/

#define NUM_RUNS 100
#define CRITICAL_SECTION_US 100 // L's fixed critical section length
#define M_WORKLOAD_US 20000  // change this between runs: 1000 / 5000 / 20000

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_handler(void);

void taskH_handler(void);
void taskM_handler(void);
void taskL_handler(void);

static void busy_spin_us(uint32_t us);
static void dwt_cycle_counter_init(void);
static uint32_t get_cycle_count(void);

uint32_t taskH_stack[1024] __attribute__((aligned(8)));
uint32_t taskM_stack[1024] __attribute__((aligned(8)));
uint32_t taskL_stack[1024] __attribute__((aligned(8)));

mutex_t mtx;
semaphore_t sem_h_go; // L to H
semaphore_t sem_m_go; // H to M

uint32_t blocked_cycles_us[NUM_RUNS];

int main(void)
{
    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    enable_processor_faults();

    dwt_cycle_counter_init();

    if (os_task_create(taskH_handler, 1, taskH_stack, sizeof(taskH_stack)) != OS_OK)
        error_handler();
    if (os_task_create(taskM_handler, 3, taskM_stack, sizeof(taskM_stack)) != OS_OK)
        error_handler();
    if (os_task_create(taskL_handler, 5, taskL_stack, sizeof(taskL_stack)) != OS_OK)
        error_handler();

    if (os_mutex_create(&mtx) != OS_OK)
        error_handler();
    if (os_sem_create(&sem_h_go, 0, PRIORITY) != OS_OK)
        error_handler();
    if (os_sem_create(&sem_m_go, 0, PRIORITY) != OS_OK)
        error_handler();

    printf("Task schedular initialized\n");
    printf("M_WORKLOAD_US=%lu, CRITICAL_SECTION_US=%lu, spins timed on DWT CYCCNT at %lu MHz\n",
           (unsigned long)M_WORKLOAD_US, (unsigned long)CRITICAL_SECTION_US,
           (unsigned long)(OS_SYSTICK_CLOCK_HZ / 1000000u));

    os_kernel_start();

    while (1)
    {
        // should never get here
    }
}

// enables the DWT cycle counter (CYCCNT)
static void dwt_cycle_counter_init(void)
{
    uint32_t *p_DEMCR = (uint32_t *)0xE000EDFC; // debug exception and monitor control register
    *p_DEMCR |= (1 << 24); // TRCENA: enable the trace/debug unit that DWT lives in

    uint32_t *p_DWT_CYCCNT = (uint32_t *)0xE0001004;
    *p_DWT_CYCCNT = 0; // reset the counter

    uint32_t *p_DWT_CTRL = (uint32_t *)0xE0001000;
    *p_DWT_CTRL |= 1; // CYCCNTENA: start counting processor cycles
}

// returns the current DWT cycle counter value, wraps around at UINT32_MAX cycles
static uint32_t get_cycle_count(void)
{
    uint32_t *p_DWT_CYCCNT = (uint32_t *)0xE0001004;
    return *p_DWT_CYCCNT;
}

// burns CPU for `us` microseconds WITHOUT blocking, so the caller stays READY (the key
// difference from os_task_delay). Timed on the DWT cycle counter rather than a fixed
// iteration count, so the duration doesn't depend on loop codegen or calibration.
static void busy_spin_us(uint32_t us)
{
    uint32_t cycles = us * (OS_SYSTICK_CLOCK_HZ / 1000000u);
    uint32_t start = get_cycle_count();
    while ((get_cycle_count() - start) < cycles)
    {
    }
}

static uint32_t worst_of(uint32_t *samples, int count)
{
    uint32_t worst = 0;
    for (int i = 0; i < count; i++)
        if (samples[i] > worst) worst = samples[i];
    return worst;
}

static uint32_t median_of(uint32_t *samples, int count)
{
    // simple insertion sort
    for (int i = 1; i < count; i++)
    {
        uint32_t key = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > key)
        {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = key;
    }
    return samples[count / 2];
}

void taskH_handler(void)
{
    uint32_t cycles_per_us = OS_SYSTICK_CLOCK_HZ / 1000000u;

    for (int i = 0; i < NUM_RUNS; i++)
    {
        // wait until L holds mtx and is at the start of its critical section
        if (os_sem_wait(&sem_h_go, OS_WAIT_FOREVER) != OS_OK)
        {
            // should not get here
            printf("Task H: sem_h_go wait failed\n");
            error_handler();
        }

        // release M so its burst is READY the instant we block, H is higher priority so we keep the CPU
        if (os_sem_post(&sem_m_go) != OS_OK)
        {
            // should not get here
            printf("Task H: sem_m_go post failed\n");
            error_handler();
        }

        uint32_t before = get_cycle_count();
        if (os_mutex_lock(&mtx, OS_WAIT_FOREVER) != OS_OK)
        {
            // should not get here
            printf("Task H: lock failed\n");
            error_handler();
        }
        uint32_t after = get_cycle_count();

        blocked_cycles_us[i] = (after - before) / cycles_per_us;

        if (os_mutex_unlock(&mtx) != OS_OK)
        {
            // should not get here
            printf("Task H: unlock failed\n");
            error_handler();
        }
    }

    uint32_t worst = worst_of(blocked_cycles_us, NUM_RUNS);
    uint32_t median = median_of(blocked_cycles_us, NUM_RUNS); // sorts blocked_cycles_us in place; run after worst_of()

    printf("Task H: worst case %lu us, median %lu us, over %lu runs\n",
           (unsigned long)worst, (unsigned long)median, (unsigned long)NUM_RUNS);

    os_task_delay(OS_WAIT_FOREVER);
}

void taskM_handler(void)
{
    while (1)
    {
        if (os_sem_wait(&sem_m_go, OS_WAIT_FOREVER) != OS_OK)
        {
            // should not get here
            printf("Task M: sem_m_go wait failed\n");
            error_handler();
        }

        busy_spin_us(M_WORKLOAD_US);
    }
}

void taskL_handler(void)
{
    while (1)
    {
        if (os_mutex_lock(&mtx, OS_WAIT_FOREVER) != OS_OK)
        {
            // should not get here
            printf("Task L: lock failed\n");
            error_handler();
        }

        // post yields to H immediately, so L is preempted here -- before any of the critical section runs
        if (os_sem_post(&sem_h_go) != OS_OK)
        {
            // should not get here
            printf("Task L: sem_h_go post failed\n");
            error_handler();
        }

        busy_spin_us(CRITICAL_SECTION_US); // stand-in for spi_transmit_dummy()

        if (os_mutex_unlock(&mtx) != OS_OK)
        {
            // should not get here
            printf("Task L: unlock failed\n");
            error_handler();
        }

    }
}

void error_handler(void)
{
    while (1);
}

void enable_processor_faults(void)
{
    uint32_t *p_SHCSR = (uint32_t *)0xE000ED24;
    *p_SHCSR |= (1 << 18); // usage fault
    *p_SHCSR |= (1 << 17); // bus fault
    *p_SHCSR |= (1 << 16); // mem fault
}

void HardFault_Handler(void)
{
    printf("hard fault\n");
    while (1);
}

void MemManage_Handler(void)
{
    printf("mem fault\n");
    while (1);
}

void BusFault_Handler(void)
{
    printf("bus fault\n");
    while (1);
}

void UsageFault_Handler(void)
{
    printf("usage fault\n");
    while (1);
}
