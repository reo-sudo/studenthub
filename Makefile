ifeq ($(strip $(DEVKITARM)),)
$(error "Set DEVKITARM first, e.g. export DEVKITARM=/opt/devkitpro/devkitARM")
endif

DEVKITPRO ?= /opt/devkitpro
TARGET    := studenthub
PREFIX    := $(DEVKITARM)/bin/arm-none-eabi-
CC        := $(PREFIX)gcc
LIBCTRU   := $(DEVKITPRO)/libctru
PORTLIBS  := $(DEVKITPRO)/portlibs/3ds
TOOLS     := $(DEVKITPRO)/tools/bin

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) -D__3DS__ \
           -I$(LIBCTRU)/include -I$(PORTLIBS)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(LIBCTRU)/lib -L$(PORTLIBS)/lib
LIBS    := -lcitro2d -lcitro3d -lctru -lm

SRC := $(wildcard source/*.c)
OBJ := $(SRC:.c=.o)

all: $(TARGET).3dsx

APP_TITLE  := Student Hub
APP_DESC   := Personal student dashboard
APP_AUTHOR := Personal project
APP_ICON   := icon.png

# The .smdh holds the title, description and icon that the Homebrew Launcher shows.
$(TARGET).smdh: $(APP_ICON)
	$(TOOLS)/smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $(APP_ICON) $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	$(TOOLS)/3dsxtool $< $@ --smdh=$(TARGET).smdh

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh

.PHONY: all clean
