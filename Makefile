CC = m68k-atari-mint-gcc
TARGET ?= build/SOPWITH.TOS
OBJDIR ?= obj
BUILDDIR ?= build

CPPFLAGS += -I. -Isrc
CFLAGS ?= -O2 -m68000 -fomit-frame-pointer -ffast-math -Wall -Wextra
LDFLAGS ?=
LDLIBS ?= -lm

COMMON_SOURCES = \
	src/hiscore.c \
	src/swasynio.c \
	src/swauto.c \
	src/swcollsn.c \
	src/swconf.c \
	src/swend.c \
	src/swgames.c \
	src/swgrpha.c \
	src/swinit.c \
	src/swmain.c \
	src/swmenu.c \
	src/swmove.c \
	src/swobject.c \
	src/swsound.c \
	src/swsplat.c \
	src/swstbar.c \
	src/swsymbol.c \
	src/swtext.c \
	src/swtitle.c \
	src/touch_area.c \
	src/yocton.c

ATARI_SOURCES = \
	src/atari/controller.c \
	src/atari/main.c \
	src/atari/pcsound.c \
	src/atari/tcpcomm_stub.c \
	src/atari/timer.c \
	src/atari/video.c


SOURCES = $(COMMON_SOURCES) $(ATARI_SOURCES)
OBJECTS = $(patsubst %.c,$(OBJDIR)/%.o,$(SOURCES)) \
          $(patsubst %.S,$(OBJDIR)/%.o,$(ATARI_ASM))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILDDIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(OBJDIR)/%.o: %.c config.h | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: %.S | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -c -o $@ $<

$(OBJDIR):
	@mkdir -p $@

$(BUILDDIR):
	@mkdir -p $@

clean:
	rm -rf $(OBJDIR) $(BUILDDIR)
