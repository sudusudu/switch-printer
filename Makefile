# Switch 3D Printer NRO
# Requires devkitPro (devkitA64 + libnx)

TARGET    := switch_printer
BUILD     := build
SRCDIR    := source
AUTHOR    := switch-printer
VERSION   := 1.1.0

# ---- DEVKITPRO guard ----
DEVKITPRO ?= /opt/devkitpro
ifeq ($(wildcard $(DEVKITPRO)/libnx/include/switch.h),)
  $(error DEVKITPRO not set or invalid. Set DEVKITPRO to your devkitPro installation path)
endif

override CC := aarch64-none-elf-gcc

ARCH := -march=armv8-a -mtune=cortex-a57 -fPIE

CFLAGS  := -g -Wall -Wextra -Wno-unused-parameter \
           -ffunction-sections -fdata-sections \
           -MMD -MP \
           -O2 $(ARCH) -I$(SRCDIR) -I$(DEVKITPRO)/libnx/include -D__SWITCH__
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
           -L$(DEVKITPRO)/libnx/lib -L$(DEVKITPRO)/portlibs/switch/lib \
           -Wl,--gc-sections
LIBS    := -lnx -lm

SRCS := $(shell find $(SRCDIR) -name '*.c' 2>/dev/null || echo $(wildcard $(SRCDIR)/*.c))
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
	nacptool --create "Switch Printer" "$(AUTHOR)" "$(VERSION)" $(BUILD)/nacp.tmp
	elf2nro $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nro --nacp=$(BUILD)/nacp.tmp
	@echo "Done: $(BUILD)/$(TARGET).nro"

clean:
	rm -rf $(BUILD)

# Header dependency tracking
-include $(OBJS:.o=.d)

.PHONY: all clean
