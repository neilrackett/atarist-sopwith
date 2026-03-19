# Makefile dispatcher for SDL and Atari ST builds.
# SDL build uses an out-of-tree build in build-sdl/ to avoid overwriting this file.

VARIANT ?= atari

ifeq ($(VARIANT),sdl)
%:
	$(MAKE) -C build-sdl $@
else
SUB_MAKE := Makefile.$(VARIANT)

# Fail early if file is missing
$(SUB_MAKE):
	$(error Variant file not found: $@. Available: atari sdl)

%:
	$(MAKE) -f $(SUB_MAKE) $@
endif