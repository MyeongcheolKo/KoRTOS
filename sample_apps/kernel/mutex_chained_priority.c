#include <stdint.h>
#include <stdio.h>

#include "kortos.h"


/*
mutex_chained_priority.c

Demonstrates chained priority inheritance: task H wants mtx1, held by task M,
but M is itself blocked waiting on mtx2, held by task L. A working chained priority 
inheritance boosts M and propagate that boost to L, not just the immediate owner. Task X
is a medium priority task that never blocks, standing for anything that would starve L 
forever without inheritance. If L is boosted, X's output visibly pauses. If L is not, 
X prints forever and L never finishes.

Phase 1 (donation): H waits forever -> proves the boost reaches L, two hops away.
Phase 2 (release): H times out -> proves the boost on L is undone once H gives up.

Tasks:
  taskH, priority 1 (highest)
  taskX, priority 2 
  taskM, priority 3 
  taskL, priority 4 (lowest)

Sequence, phase 1 (donation):
    1. L runs first (H/X/M all delays on entry), locks mtx2, and starts a long
        busy_spin to simulate holding it while working.
    2. M wakes, locks mtx1, then tries to lock mtx2 (held by L) with OS_WAIT_FOREVER, 
        so it now blocks. M now owns mtx1 while itself blocked on mtx2.
    3. X wakes and starts printing, checking between prints whether H has signaled 
        phase1_done_sem yet (nonblocking, timeout == 0). L isn't boosted yet,
        so X (priority 2) preempts L (priority 4). X dominates, L makes no progress.
    4. H wakes and calls os_mutex_lock(&mtx1, OS_WAIT_FOREVER). Since mtx1 is held by M,
        so H blocks. M is boosted directly to H's priority. Because M is itself
        BLOCKED_MUTEX on mtx2, the boost must also propagate to mtx2's owner, L.
    5. If L was correctly boosted to priority 1, it now beats X and preempts it,
        resuming and finishing its busy_spin, then unlocking mtx2. If not, X continues 
        to run forever and L never finishes.
    6. Ownership of mtx2 transfers to M. M finishes quickly, unlocks mtx2, then unlocks mtx1.
    7. H acquires mtx1 and immediately unlocks it again, then signals Z using the phase1_done_sem
        and delays to let L and M lock mtx1 and mtx2 again for phase 2.

Phase 2 setup:
    8. X recieves the phase1_done_sem signal and delays to let L and M lock mtx1 and mtx2 
        again for phase 2.
    9. M, delays to let L lock mtx2 first.
    10. L locks mtx2, then busy spins.
    11. M preempts L and locks mtx1, then M tries to lock mtx2 and blocks again.
    12. X resumes its loop and prints "Task X: running".

Sequence, phase 2 (release):
    13. H delay passes and calls os_mutex_lock(&mtx1, 100) this time. mtx1 is held by M,
        so H blocks, triggering the same propagated boost. M and L both go to priority 1.
        L preempts X again.
    14. L's busy spin this round outlasts H's 100 ticks timeout on purpose, so H's wait
        expires before L's busy spin. H's os_mutex_lock returns OS_ERR_UNAVAILABLE, and H is
        removed from mtx1's waitlist.
    15.  The release path now recomputes priority. M is directly reachable from where H left,
        so M correctly drops back to its base priority even with a unchained recompute. 
        However, L is two hops away. If the release path chains outward the same way donation 
        did, L also drops back to its base priority, X preempts it again, and "Task X: running" 
        resumes. If the release path only ever touches the immediate owner, L stays stuck
        boosted at priority 1 forever and X never prints again. 
    16. H delays for 10 ticks, lets M print "Task X: running" a few times, then the app ends with while(1).

Expected ITM/SWO output:
Task schedular initialized
Task L: locking mtx2
Task L: mtx2 acquired
Task M: locking mtx1
Task M: mtx1 acquired
Task M: locking mtx2
Task X: running    <- X resumes, preempts L, proves L is not boosted yet
... ("Task X: running" repeats)
Task X: running
Task H: locking mtx1 (phase 1 timout==forever)    <- boosts M, then chains to boost L
Task L: unlocking mtx2    <- L wins over X now, finishes and unlocks
Task M: mtx2 acquired
Task M: unlocking mtx2
Task M: unlocking mtx1    <- hands mtx1 to H
Task H: mtx1 acquired (phase 1 done)
Task H: unlocking mtx1
Task L: locking mtx2
Task L: mtx2 acquired
Task M: locking mtx1
Task M: mtx1 acquired
Task M: locking mtx2
Task X: running    <- X resumes, preempts L
... ("Task X: running" repeats)
Task X: running
Task H: locking mtx1 (phase 2 timeout==100)    <- boosts M, chains to boost L, X goes quiet
Task H: mtx1 lock timed out (phase 2 done)
Task X: running    <- L was released all the way, X is free again
Task X: running
...

*/

#define SPIN_BETWEEN_PRINTS 500000u // bigger = slower prints; tune for a readable ITM rate

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_hanlder(void);

void taskH_handler(void);
void taskX_handler(void);
void taskM_handler(void);
void taskL_handler(void);

void busy_spin(uint32_t iterations);

uint32_t taskH_stack[1024] __attribute__((aligned(8)));
uint32_t taskX_stack[1024] __attribute__((aligned(8)));
uint32_t taskM_stack[1024] __attribute__((aligned(8)));
uint32_t taskL_stack[1024] __attribute__((aligned(8)));

mutex_t mtx1;
mutex_t mtx2;
semaphore_t phase1_done_sem;

int main(void)
{
    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    enable_processor_faults();

    if (os_task_create(taskH_handler, 1, taskH_stack, sizeof(taskH_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(taskX_handler, 2, taskX_stack, sizeof(taskX_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(taskM_handler, 3, taskM_stack, sizeof(taskM_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(taskL_handler, 4, taskL_stack, sizeof(taskL_stack)) != OS_OK)
        error_hanlder();

    if (os_mutex_create(&mtx1) != OS_OK)
        error_hanlder();
    if (os_mutex_create(&mtx2) != OS_OK)
        error_hanlder();
    if (os_sem_create(&phase1_done_sem, 0, FIFO) != OS_OK)
        error_hanlder();

    printf("Task schedular initialized\n");

    os_kernel_start();

    while (1)
    {
        // should never get here
    }
}

// burns CPU WITHOUT blocking, so the caller stays READY (this is the key difference from os_task_delay)
void busy_spin(uint32_t iterations)
{
    for (volatile uint32_t k = 0; k < iterations; k++)
    {
    }
}

void taskH_handler(void)
{
    os_task_delay(60); // let L, M, and X form the initial blocked chain first

    printf("Task H: locking mtx1 (phase 1 timout==forever)\n");
    os_err_t lock_result = os_mutex_lock(&mtx1, OS_WAIT_FOREVER);
    if (lock_result == OS_OK)
    {
        printf("Task H: mtx1 acquired (phase 1 done)\n");
    }
    else
    {
        // should not get here
        printf("Task H: phase 1 lock failed\n");
        error_hanlder();
    }
    printf("Task H: unlocking mtx1\n");
    if (os_mutex_unlock(&mtx1) != OS_OK)
    {
        // should not get here
        printf("Task H: phase 1 unlock failed\n");
        error_hanlder();
    }

    os_sem_post(&phase1_done_sem); // tells X phase 1 is finished
    os_task_delay(80); // let L and M re-form the same blocked chain for phase 2

    printf("Task H: locking mtx1 (phase 2 timeout==100)\n");
    lock_result = os_mutex_lock(&mtx1, 100);
    if (lock_result == OS_ERR_UNAVAILABLE)
    {
        printf("Task H: mtx1 lock timed out (phase 2 done)\n");
    }
    else
    {
        // should not get here
        printf("Task H: phase 2 lock failed\n");
        error_hanlder();
    }

    os_task_delay(10); // yield to watch for "Task X: running"
    while(1); // sample app finishes here
}

void taskX_handler(void)
{
    os_task_delay(40); // let L acquire mtx2 and M chain onto it before X starts competing for the CPU

    while (1)
    {
        if (os_sem_wait(&phase1_done_sem, 0) == OS_OK)
        {
            // phase 1 is over, pause to give L and M room to reform the blocked chain for
            // phase 2 without X fighting them for the CPU
            os_task_delay(60);
        }

        printf("Task X: running\n");
    }
}

void taskM_handler(void)
{
    os_task_delay(20); // let L lock mtx2 first

    while (1)
    {
        printf("Task M: locking mtx1\n");
        if (os_mutex_lock(&mtx1, OS_WAIT_FOREVER) != OS_OK)
        {
            // should not get here
            printf("Task M: mtx1 lock failed\n");
            error_hanlder();
        }
        printf("Task M: mtx1 acquired\n");

        printf("Task M: locking mtx2\n");
        if (os_mutex_lock(&mtx2, OS_WAIT_FOREVER) != OS_OK)
        {
            // should not get here
            printf("Task M: mtx2 lock failed\n");
            error_hanlder();
        }
        printf("Task M: mtx2 acquired\n");

        printf("Task M: unlocking mtx2\n");
        if (os_mutex_unlock(&mtx2) != OS_OK)
        {
            // should not get here
            printf("Task M: mtx2 unlock failed\n");
            error_hanlder();
        }

        printf("Task M: unlocking mtx1\n");
        if (os_mutex_unlock(&mtx1) != OS_OK)
        {
            // should not get here
            printf("Task M: mtx1 unlock failed\n");
            error_hanlder();
        }

        os_task_delay(30); // let L relock mtx2 before M tries again, so the chain re-forms instead of M grabbing mtx2 uncontended
    }
}

void taskL_handler(void)
{
    while (1)
    {
        printf("Task L: locking mtx2\n");
        if (os_mutex_lock(&mtx2, OS_WAIT_FOREVER) != OS_OK)
        {
            // should not get here
            printf("Task L: mtx2 lock failed\n");
            error_hanlder();
        }
        printf("Task L: mtx2 acquired\n");

        busy_spin(SPIN_BETWEEN_PRINTS * 5); // simulate long work while holding mtx2; must outlast PHASE2_TIMEOUT

        printf("Task L: unlocking mtx2\n");
        if (os_mutex_unlock(&mtx2) != OS_OK)
        {
            // should not get here
            printf("Task L: mtx2 unlock failed\n");
            error_hanlder();
        }
    }
}

void error_hanlder(void)
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
