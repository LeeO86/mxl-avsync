# AV Sync calibrator firmware (PY32F002A). Needs arm-none-eabi-gcc in PATH.
#   make        build build/calibrator.elf/.bin/.hex and print the size
#   make test   build and run the host unit tests (plain cc)
#   make clean

PREFIX  ?= arm-none-eabi-
CC_ARM  := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size
HOSTCC  ?= cc

BUILD := build
SRCS  := src/startup.c src/main.c src/cal_logic.c
OBJS  := $(SRCS:src/%.c=$(BUILD)/%.o)

CFLAGS := -mcpu=cortex-m0plus -mthumb -Os -std=c11 -g \
          -ffunction-sections -fdata-sections -ffreestanding -fno-tree-loop-distribute-patterns \
          -Wall -Wextra -Werror -Isrc -Ivendor
LDFLAGS := -mcpu=cortex-m0plus -mthumb -nostartfiles -nostdlib -lgcc \
           -Tsrc/py32f002a.ld -Wl,--gc-sections -Wl,-Map=$(BUILD)/calibrator.map

HOST_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror

.PHONY: all test clean

all: $(BUILD)/calibrator.bin $(BUILD)/calibrator.hex
	$(SIZE) $(BUILD)/calibrator.elf

$(BUILD)/%.o: src/%.c $(wildcard src/*.h) | $(BUILD)
	$(CC_ARM) $(CFLAGS) -c $< -o $@

$(BUILD)/calibrator.elf: $(OBJS) src/py32f002a.ld
	$(CC_ARM) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/calibrator.bin: $(BUILD)/calibrator.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/calibrator.hex: $(BUILD)/calibrator.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/test_cal_logic: tests/test_cal_logic.c src/cal_logic.c src/cal_logic.h | $(BUILD)
	$(HOSTCC) $(HOST_CFLAGS) tests/test_cal_logic.c src/cal_logic.c -o $@

test: $(BUILD)/test_cal_logic
	./$(BUILD)/test_cal_logic

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -f $(BUILD)/*.o $(BUILD)/*.elf $(BUILD)/*.bin $(BUILD)/*.hex $(BUILD)/*.map $(BUILD)/test_cal_logic
