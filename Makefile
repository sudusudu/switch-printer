# Switch 3D Printer NRO
# Build: make
# Requires: devkitPro with switch-dev installed

TARGET    := switch_printer
BUILD     := build
SOURCES   := source
INCLUDES  := source

# --- devkitPro paths ---
ifeq ($(strip $(DEVKITPRO)),)
  $(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitPro")
endif

include $(DEVKITPRO)/libnx/switch_rules

# --- Compiler flags ---
ARCH    := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  := -g -Wall -O2 $(ARCH) $(INCLUDE)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map

LIBS    := -lnx -lm

# --- Files ---
CFILES   := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.c))
OFILES   := $(patsubst $(SOURCES)/%.c, $(BUILD)/%.o, $(CFILES))

# Make sure NAME matches NACP
APP_TITLE    := Switch Printer
APP_AUTHOR   := aaa
APP_VERSION  := 1.0.0

all: $(BUILD) $(BUILD)/$(TARGET).nro

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD)/%.o: $(SOURCES)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).nro: $(BUILD)/$(TARGET).elf
	@echo "Building NRO..."
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $(BUILD)/$(TARGET).nacp
	elf2nro $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nro \
		--nacp=$(BUILD)/$(TARGET).nacp
	@echo "Done: $(BUILD)/$(TARGET).nro"

clean:
	rm -rf $(BUILD)

.PHONY: all clean