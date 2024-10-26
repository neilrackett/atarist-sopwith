
include ../config.make

TOPLEVEL = ../..
DIST_FILES = error.png loading.gif
SOPWITH_JS = $(TOPLEVEL)/src/sopwith
SOPWITH_WASM = $(TOPLEVEL)/src/sopwith.wasm
PANDOC_FLAGS = -s --template=default.html5 -H ../style.html

all: page

page: $(SOPWITH_JS) $(SOPWITH_WASM) $(DIST_FILES) sopwith.html
	mkdir -p $@
	sed "s/__PACKAGE_STRING__/$(PACKAGE_STRING)/" \
	     < sopwith.html > $@/sopwith.html
	cp $(DIST_FILES) $(SOPWITH_WASM) $@/
	cp $(TOPLEVEL)/icon.png $@/favicon.png
	cp $(SOPWITH_JS) $@/sopwith.js
	mkdir -p $@/doc
	pandoc -f man $(PANDOC_FLAGS) -s $(TOPLEVEL)/doc/sopwith.6 \
	       -o $@/doc/sopwith.html
	pandoc -f man $(PANDOC_FLAGS) -s $(TOPLEVEL)/doc/sopwith.cfg.5 \
	       -o $@/doc/sopwith.cfg.html
	pandoc -f man $(PANDOC_FLAGS) -s $(TOPLEVEL)/doc/sopwith-mission.5 \
	       -o $@/doc/sopwith-mission.html

clean:
	rm -rf page
