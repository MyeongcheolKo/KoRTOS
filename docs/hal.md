# Peripheral HAL

Register-level drivers for STM32F446xx peripherals, written against the reference manual without the vendor HAL. They live in `kortos_hal/STM32F446xx/` and build into their own static library (`libhal.a`), which doesn't depend on the kernel (and the kernel doesn't depend on it).

- [Architecture](#architecture)
- [Common Features](#common-features)
- [GPIO](#gpio)
- [SPI](#spi)
- [I2C](#i2c)
- [USART](#usart)
- [CAN](#can)
- [Sample Applications](#sample-applications)

## Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                        Application Layer                          │
├───────────────────────────────────────────────────────────────────┤
│   GPIO    │    SPI    │    I2C    │   USART   │    CAN    │  RCC  │
├───────────────────────────────────────────────────────────────────┤
│                          STM32F446xx.h                            │
│      (peripheral register structs, base addresses, IRQ numbers)   │
└───────────────────────────────────────────────────────────────────┘
```

The drivers share one shape:

- a `*_config_t` struct the application fills in (`GPIO_config_t`, `SPI_config_t`, `CAN_config_t`, ...)
- a `*_Handle_t` struct pairing that config with a pointer to the peripheral's register block (`p_GPIOx`, `CANx`, ...) and, for interrupt-driven drivers, the in-flight transfer state
- `*_clock_control()` / `*_init()` / `*_deinit()` lifecycle calls
- polling send/receive, and `*_IT` non-blocking variants that complete in an IRQ handler
- NVIC enable/priority helpers and an IRQ-handling entry point the app's ISR calls
- weak application callbacks the driver invokes on completion or error

## Common Features

- **Clock control** - enable/disable the peripheral clock via RCC
- **Init / deinit** - configure from the user's config struct; reset the peripheral to its default state
- **Polling and interrupt-driven transfers** - blocking calls for simple cases, `*_IT` variants plus an IRQ handler for non-blocking use
- **Weak callbacks** - overridable application hooks for interrupt events (transfer complete, error), so the driver doesn't need to know about the application

The CAN driver returns `KHAL_status_t` (`KHAL_common.h`) from every call: `KHAL_OK`, `KHAL_ERR_BUSY`, `KHAL_ERR_TIMEOUT`, `KHAL_ERR_TX`, `KHAL_ERR_RX`, `KHAL_ERR_INVALID_PARAM`, `KHAL_ERR_NULL_PTR`. The older drivers (GPIO/SPI/I2C/USART) predate that convention and return `void` or a busy flag.

## GPIO

General Purpose Input/Output driver supporting all GPIO ports (A–H).

- Pin mode configuration (Input, Output, Alternate Function, Analog)
- Output type (Push-Pull, Open-Drain)
- Speed configuration (Low, Medium, Fast, High)
- Pull-up/Pull-down resistor configuration
- Interrupt support via EXTI (Rising edge, Falling edge, Both edges)
- Alternate function selection
- Pin- and port-level read/write/toggle

## SPI

Serial Peripheral Interface driver supporting SPI1–SPI4.

- Master/Slave mode configuration
- Full-duplex, Half-duplex, and Simplex communication
- Configurable clock speed (prescaler from /2 to /256)
- 8-bit or 16-bit data frame format
- Clock polarity (CPOL) and phase (CPHA) configuration
- Hardware/Software slave select management (SSM/SSI/SSOE)
- Interrupt-driven transmit and receive
- Overrun error handling

## I2C

Inter-Integrated Circuit driver supporting I2C1–I2C3.

- Master and Slave mode
- Standard mode (100 kHz) and Fast mode (200 kHz, 400 kHz)
- 7-bit addressing
- Repeated start condition support
- ACK/NACK management
- Interrupt-driven communication with separate event and error IRQ handlers
- Error handling (Bus error, Arbitration loss, ACK failure, Overrun, Timeout)

## USART

Universal Synchronous/Asynchronous Receiver-Transmitter driver supporting USART1–3, UART4–5, USART6.

- Transmit-only, Receive-only, or Full-duplex mode
- Wide range of baud rates (1200 to 3M)
- 8-bit or 9-bit word length
- Configurable parity (None, Even, Odd)
- Stop bit configuration (0.5, 1, 1.5, 2 bits)
- Hardware flow control (CTS, RTS)
- Interrupt-driven communication
- Error detection (Framing, Noise, Overrun)

## CAN

Controller Area Network driver for the bxCAN peripheral (CAN1/CAN2).

- Bit timing configuration (prescaler, TS1, TS2, SJW) and time-triggered mode
- Normal, Loopback, Silent, and Silent-Loopback test modes
- Acceptance filter banks (0–27): mask or list mode, 16- or 32-bit scale, FIFO0/FIFO1 assignment
- Standard (11-bit) and extended (29-bit) identifiers, data and remote frames
- `CAN_transmit()` - loads a free mailbox and polls until the frame is sent, with timeout
- `CAN_transmit_IT()` - loads a mailbox and returns; completion reported through `CAN_tx_callback()` once per mailbox
- `CAN_receive()` - non-blocking check of both RX FIFOs
- `CAN_RX_IRQHandler()` - decodes a frame from the interrupting FIFO into a `CAN_frame_t` and hands it to `CAN_rx_callback()`, which the app overrides (for example to push onto a kernel queue - see `CAN_loopback_mutex_queue.c`)
- Every call returns `KHAL_status_t`, with parameter validation and hardware timeouts surfaced as distinct codes

## Sample Applications

The `sample_apps/hal/` directory contains working driver examples (kernel demos live in `sample_apps/kernel/`, see [Kernel Sample Applications](kernel.md#sample-applications)). Build any of them with `make APP=<path>` (see [Build System](bare-metal.md#build-system-makefile)):

| Application (`sample_apps/hal/`) | Description |
|-------------|-------------|
| `LED_toggle.c` | Basic GPIO output - toggles the onboard LED |
| `button_LED.c` | GPIO input/output - LED controlled by the onboard button |
| `external_button_LED.c` | External button and LED with pull-up configuration |
| `interrupt_button_LED.c` | GPIO interrupt - LED toggle from an EXTI button interrupt |
| `SPI_testing.c` | SPI loopback test |
| `SPI_transmit_arduino.c` | SPI master transmit to an Arduino slave |
| `SPI_send_receive_arduino.c` | SPI bidirectional communication with an Arduino |
| `I2C_master_send_arduino.c` | I2C master send to an Arduino slave |
| `I2C_master_send_receive.c` | I2C master send/receive with an Arduino |
| `I2C_interrupt_send_receive.c` | I2C interrupt-driven communication |
| `CAN_polling_loopback.c` | CAN1 in loopback mode: configure, filter, transmit, and poll-receive the same frame back |
