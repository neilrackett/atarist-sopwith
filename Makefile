# Makefile redirect so that Atari version stays safe if you use automake
# to build the SDL version (which will overwrite this with its own)

VARIANT ?= atari
SUB_MAKE := Makefile.$(VARIANT)

# Fail early if file is missing
$(SUB_MAKE):
	$(error Variant file not found: $@. Available: $(wildcard Makefile.*))

%:
	$(MAKE) -f $(SUB_MAKE) $@