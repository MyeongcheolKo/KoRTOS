CC   = arm-none-eabi-gcc
AR   = arm-none-eabi-ar
MACH = cortex-m4
BUILD_DIR = build

# change this to the sample application you want to build, e.g. ` make APP=sample_apps/hal/LED_toggle.c`
APP ?= sample_apps/kernel/round_robin_priority.c

# header search paths (the selected app's own dir is added so it finds its headers)
INCLUDES = -I$(dir $(APP)) -Ibsp/STM32F446xx -Iconfig -Ikernel -Iport/arm/cortex_m4 -Ikortos_hal/STM32F446xx

CFLAGS     = -c -mcpu=$(MACH) -mthumb -mfloat-abi=soft -std=gnu11 -Wall -O0 -g $(INCLUDES)
LDFLAGS    = -mcpu=$(MACH) -mthumb -mfloat-abi=soft --specs=nano.specs  -T bsp/STM32F446xx/linker_script.ld -Wl,-Map=$(BUILD_DIR)/final.map

# source groups (one per layer) 
KERNEL_SRCS = kernel/kernel.c \
              port/arm/cortex_m4/port.c
HAL_SRCS = kortos_hal/STM32F446xx/GPIO.c \
			kortos_hal/STM32F446xx/I2C.c \
			kortos_hal/STM32F446xx/SPI.c \
			kortos_hal/STM32F446xx/USART.c \
			kortos_hal/STM32F446xx/rcc.c
APP_SRCS    = $(APP)
BSP_SRCS    = bsp/STM32F446xx/startup.c \
              bsp/STM32F446xx/syscalls.c \
              bsp/STM32F446xx/sysmem.c

# src/main.c -> build/src/main.o (mirrors the tree)
KERNEL_OBJS = $(addprefix $(BUILD_DIR)/,$(KERNEL_SRCS:.c=.o))
HAL_OBJS = $(addprefix $(BUILD_DIR)/,$(HAL_SRCS:.c=.o))
APP_OBJS    = $(addprefix $(BUILD_DIR)/,$(APP_SRCS:.c=.o))
BSP_OBJS    = $(addprefix $(BUILD_DIR)/,$(BSP_SRCS:.c=.o))

LIBKORTOS    = $(BUILD_DIR)/libkortos.a
LIBHAL = $(BUILD_DIR)/libhal.a

# libkortos.a holds the ISRs (PendSV_Handler / SysTick_Handler). startup.c only
# weak-aliases those to Default_Handler, so unless the whole archive is force-linked
# the linker can keep the weak stubs and context switching silently dies. Pull the
# whole kernel archive so the strong handlers win. libhal.a has no vector-table
# handlers, so it links on demand (only what the app actually references).
WHOLE_KORTOS = -Wl,--whole-archive $(LIBKORTOS) -Wl,--no-whole-archive

# full integrated firmware (app + bsp + kernel + HAL)
all: $(BUILD_DIR)/final.elf

# build a layer on its own (kernel has zero hal dependencies, and vice versa)
kernel:  $(LIBKORTOS)
hal: $(LIBHAL)

# compile: any .c -> build/.../.o, creating mirrored subdirs
$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

# static libraries
$(LIBKORTOS): $(KERNEL_OBJS)
	$(AR) rcs $@ $^

$(LIBHAL): $(HAL_OBJS)
	$(AR) rcs $@ $^

# link: app + bsp objects against both libs
$(BUILD_DIR)/final.elf: $(APP_OBJS) $(BSP_OBJS) $(LIBKORTOS) $(LIBHAL)
	$(CC) $(LDFLAGS) $(APP_OBJS) $(BSP_OBJS) $(WHOLE_KORTOS) $(LIBHAL) -o $@

clean:
	rm -rf $(BUILD_DIR)

load:
	openocd -f board/st_nucleo_f4.cfg
