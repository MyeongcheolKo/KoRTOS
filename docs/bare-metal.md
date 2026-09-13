# Bare-Metal Infrastructure

Everything needed to get code running on the MCU before the scheduler takes over: the linker script, startup code, debug output, and the build system. None of it comes from vendor startup files or an IDE-generated project.

- [Memory Map](#memory-map)
- [Linker Script](#linker-script)
- [Startup Code](#startup-code)
- [Build System (Makefile)](#build-system-makefile)
- [Debugging](#debugging)

## Memory Map

```
┌─────────────────────────────────────┐ (SRAM_END) (T1_STACK_START)
│         Task 1 Stack (PSP)          │
│              (1 KB)                 │
├─────────────────────────────────────┤ (T2_STACK_START)
│         Task 2 Stack (PSP)          │
│              (1 KB)                 │
├─────────────────────────────────────┤ (T3_STACK_START)
│         Task 3 Stack (PSP)          │
│              (1 KB)                 │
├─────────────────────────────────────┤ (T4_STACK_START)
│         Task 4 Stack (PSP)          │
│              (1 KB)                 │
├─────────────────────────────────────┤ (IDLE_STACK_START)
│        Idle Task Stack (PSP)        │
│              (1 KB)                 │
├─────────────────────────────────────┤ (SCHEDU_STACK_START)
│         MSP Stack (Kernel)          │
│              (1 KB)                 │
├─────────────────────────────────────┤
│                                     │
│          Available SRAM             │
│        (Heap grows upward)          │
│                                     │
├─────────────────────────────────────┤
│              .bss                   │
├─────────────────────────────────────┤
│              .data                  │
└─────────────────────────────────────┘ 0x20000000 (SRAM_START)


┌─────────────────────────────────────┐
│                                     │
│           Available FLASH           │
│                                     │
├─────────────────────────────────────┤
│             .rodata                 │
├─────────────────────────────────────┤
│              .text                  │
├─────────────────────────────────────┤
│            .isr_vector              │
│          (Vector Table)             │
└─────────────────────────────────────┘ 0x08000000 (FLASH_START)
```

## Linker Script

The linker script defines how the compiled code is organized in FLASH and SRAM.

| Region | Start Address | Size | Attributes |
|--------|--------------|------|------------|
| FLASH  | 0x08000000   | 512K | rx (read, execute) |
| SRAM   | 0x20000000   | 128K | rwx (read, write, execute) |

### Sections

| Section | Location | Description |
|---------|----------|-------------|
| `.isr_vector` | FLASH | Interrupt vector table (must be at 0x08000000) |
| `.text` | FLASH | Executable code |
| `.rodata` | FLASH | Read-only data (constants, strings) |
| `.data` | SRAM (VMA), FLASH (LMA) | Initialized global/static variables |
| `.bss` | SRAM | Uninitialized global/static variables (zeroed at startup) |

### .data Section

The `.data` section is special because it is stored in FLASH (LMA) since RAM is volatile, but is then copied to SRAM (VMA) by startup code before `main()` executes and resides in SRAM at runtime.

### Linker Symbols

```c
_estack      // Top of stack (end of SRAM)
_sdata       // Start of .data in SRAM (VMA)
_edata       // End of .data in SRAM
_sidata      // Start of .data in FLASH (LMA) - initialization source
_sbss        // Start of .bss
_ebss        // End of .bss
_end         // End of used SRAM (heap starts here)
_Min_Stack_Size  // Reserved stack space (0x400 = 1KB)
```
- `. = ALIGN(4)` - used at the start and end of each section to force 4-byte alignment, which ensures word-aligned access and proper copying in startup code (which copies 4 bytes at a time).
- `_sidata = LOADADDR(.data)` - to ensure proper copying of the `.data` section from FLASH to SRAM in startup code.

## Startup Code

The startup code runs before `main()` and initializes `.data` and `.bss` in SRAM.

### Vector Table

The vector table is implemented according to the STM32F446xx Reference Manual, including all the **system exception handlers**, **IRQ handlers**, and **reserved** entries.

### Reset Handler Sequence

```
        Power On / Reset
               |
               |
  ┌──────────────────────────┐
  │   1. Copy .data section  │
  │    from FLASH to SRAM    │
  └──────────────────────────┘
               |
               |
  ┌──────────────────────────┐
  │    2. Fill .bss with 0   │
  └──────────────────────────┘
               |
┌───────────────────────────────┐
│  3. Call __libc_init_array()  │
└───────────────────────────────┘
               |
               |
  ┌──────────────────────────┐
  │     4. Call main()       │
  └──────────────────────────┘
```

### Weak Aliases

All interrupt handlers are declared as `__attribute__((weak, alias("Default_Handler")))`. By default every handler points to `Default_Handler`, an infinite `while(1)` loop, so an interrupt that was never given a real handler gets stuck somewhere obvious. The `weak` attribute lets the kernel (`PendSV_Handler`, `SysTick_Handler`) and applications (`EXTI15_10_IRQHandler`, `CAN1_RX0_IRQHandler`, ...) override the ones they implement.

## Build System (Makefile)

A plain `make` build (no IDE required). The kernel and HAL compile into independent static libraries, which link with a selected sample application into one firmware image under `build/`.

### Compiler Flags

- `arm-none-eabi-gcc` - the cross compiler for ARM
- `-mcpu=cortex-m4` - the target processor for the STM32F446RE
- `-mthumb` - generate Thumb ISA rather than ARM, since Cortex-M4 only runs Thumb
- `-mfloat-abi=soft` - software floating point; there's no floating-point math in the kernel or drivers, so the FPU is never enabled and there's no FPU library overhead
- `-O0 -g` - unoptimized with debug info, so stepping through in the debugger follows the source
- `--specs=nano.specs` - Newlib-nano for a small footprint

### Build Commands

| Command | Description |
|---------|-------------|
| `make` | build the full firmware (`build/final.elf`) |
| `make kernel` | build only the kernel library (`libkortos.a`) - proves it compiles with zero HAL dependencies |
| `make hal` | build only the HAL library (`libhal.a`) |
| `make clean` | remove the `build/` directory |
| `make load` | start OpenOCD for the Nucleo-F4 (then flash with `arm-none-eabi-gdb`, see [Flashing](#flashing)) |

### Selecting the Application

The default app is whatever the `APP ?=` line at the top of the makefile points at. Build any other sample by overriding `APP`:

```
make                                                # the makefile's default app
make APP=sample_apps/kernel/mutex_pi_benchmark.c    # a kernel sample
make APP=sample_apps/hal/LED_toggle.c               # a HAL sample
```

The selected app's own directory is added to the include path automatically.

### Static Libraries

| Artifact | Built from | Notes |
|----------|-----------|-------|
| `build/libkortos.a` | `kernel/` + `port/` | the RTOS kernel as a standalone library |
| `build/libhal.a` | `kortos_hal/` | the peripheral HAL as a standalone library |

`libkortos.a` is force-linked with `--whole-archive` because it holds the `PendSV`/`SysTick` handlers that `startup.c` only weak-aliases (otherwise the linker keeps the weak stubs and context switching silently dies). `libhal.a` links on demand, only the drivers the selected app references.

### Output Files

All build artifacts go under `build/` (git-ignored), mirroring the source tree:

| File | Description |
|------|-------------|
| `build/final.elf` | the flashable firmware |
| `build/libkortos.a` / `build/libhal.a` | kernel / HAL static libraries |
| `build/final.map` | linker map (symbol addresses, section sizes) |

### Flashing

`make load` starts an OpenOCD server for the board. In a second terminal:

```
arm-none-eabi-gdb build/final.elf
(gdb) target remote localhost:3333
(gdb) monitor reset init
(gdb) monitor flash write_image erase build/final.elf
(gdb) monitor reset
```

## Debugging

### ITM Debug Output

`printf` is routed to SWV ITM stimulus port 0 in `bsp/STM32F446xx/syscalls.c`, viewable in STM32CubeIDE's SWV ITM Data Console. ITM is non-blocking, so logging doesn't interfere with task timing while debugging the scheduler.

### Fault Handlers

The kernel sample apps enable UsageFault, BusFault, and MemManageFault in `SHCSR` and give each its own handler that prints which fault fired, instead of letting everything escalate to HardFault.
