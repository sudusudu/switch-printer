# Switch 3D Printer NRO

TARGET    := switch_printer
BUILD     := build
SRCDIR    := source

override CC := aarch64-none-elf-gcc

ARCH := -march=armv8-a -mtune=cortex-a57 -fPIE

CFLAGS  := -g -Wall -O2 $(ARCH) -I$(SRCDIR) -I$(DEVKITPRO)/libnx/include -D__SWITCH__
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -L$(DEVKITPRO)/libnx/lib -L$(DEVKITPRO)/portlibs/lib
LIBS    := -lnx -lm

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.o,$(SRCS))

all: $(BUILD) $(BUILD)/$(TARGET).nro

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD)/%.o: $(SRCDIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).nro: $(BUILD)/$(TARGET).elf
	@echo "Building NRO..."
	nacptool --create "Switch Printer" "aaa" "1.0.0" $(BUILD)/nacp.tmp
	elf2nro $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nro --nacp=$(BUILD)/nacp.tmp
	@echo "Done: $(BUILD)/$(TARGET).nro"

clean:
	rm -rf $(BUILD)

.PHONY: all clean