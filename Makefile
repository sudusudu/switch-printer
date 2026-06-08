# Switch 3D Printer NRO - Standalone Makefile

TARGET    := switch_printer
BUILD     := build
SRCDIR    := source

ARCH := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -O2 $(ARCH) -I$(SRCDIR) -D__SWITCH__
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS    := -lnx -lm

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.o,$(SRCS))

APP_TITLE   := Switch Printer
APP_AUTHOR  := aaa
APP_VERSION := 1.0.0

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
	nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $(BUILD)/$(TARGET).nacp
	elf2nro $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nro --nacp=$(BUILD)/$(TARGET).nacp
	@echo "Done: $(BUILD)/$(TARGET).nro"

clean:
	rm -rf $(BUILD)

.PHONY: all clean