# AV Sync calibrator firmware (WCH CH32X035G8R6, RISC-V). Needs a RISC-V GCC in PATH.
#   make                                   xPack riscv-none-elf-gcc (default prefix)
#   make CROSS=riscv64-unknown-elf-        Ubuntu/Debian gcc-riscv64-unknown-elf
#   make test                              host unit tests (plain cc)
#   make clean

CROSS   ?= riscv-none-elf-
CC_RV   := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
HOSTCC  ?= cc

BUILD := build
SRCS  := src/startup.c src/main.c src/cal_logic.c
OBJS  := $(SRCS:src/%.c=$(BUILD)/%.o)

# Plain RV32IMAC (no WCH vendor extensions), no C library.
ARCH   := -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medlow
CFLAGS := $(ARCH) -Os -std=c11 -g \
          -ffunction-sections -fdata-sections -ffreestanding -fno-tree-loop-distribute-patterns \
          -Wall -Wextra -Werror -Isrc -Ivendor $(EXTRA_CFLAGS)
LDFLAGS := $(ARCH) -nostartfiles -nostdlib -Tsrc/ch32x035.ld \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/calibrator.map

HOST_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror

.PHONY: all test clean

all: $(BUILD)/calibrator.bin $(BUILD)/calibrator.hex
	$(SIZE) $(BUILD)/calibrator.elf

$(BUILD)/%.o: src/%.c $(wildcard src/*.h) vendor/ch32x035_regs.h | $(BUILD)
	$(CC_RV) $(CFLAGS) -c $< -o $@

$(BUILD)/calibrator.elf: $(OBJS) src/ch32x035.ld
	$(CC_RV) $(OBJS) $(LDFLAGS) -o $@

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
