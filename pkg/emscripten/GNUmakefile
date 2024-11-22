
include ../config.make

MANPAGE_MANGLE_AWK = \
	/begin no-emscripten/ { strip = 1 } \
	!strip; \
	/end no-emscripten/ { strip = 0 }

TOPLEVEL = ../..
DIST_FILES = error.png loading.gif icon-192.png icon-512.png
SOPWITH_JS = $(TOPLEVEL)/src/sopwith
SOPWITH_WASM = $(TOPLEVEL)/src/sopwith.wasm
PANDOC_FLAGS = -s --template=default.html5 -H ../style.html

all: page

page: $(SOPWITH_JS) $(SOPWITH_WASM) $(DIST_FILES) sopwith.html mangled.6
	mkdir -p $@
	sed "s/__PACKAGE_STRING__/$(PACKAGE_STRING)/" \
	     < sopwith.html > $@/sopwith.html
	sed "s/__PACKAGE_NAME__/$(PACKAGE_NAME)/" \
	     < manifest.json > $@/manifest.json
	cp $(DIST_FILES) $(SOPWITH_WASM) $@/
	cp $(TOPLEVEL)/icon.png $@/favicon.png
	cp $(SOPWITH_JS) $@/sopwith.js
	mkdir -p $@/doc
	pandoc -f man $(PANDOC_FLAGS) -s $(TOPLEVEL)/doc/sopwith.6 \
	       -o $@/doc/sopwith.html
	pandoc -f man $(PANDOC_FLAGS) -s mangled.6 \
	       -o $@/doc/sopwith-emscripten.html
	pandoc -f man $(PANDOC_FLAGS) -s $(TOPLEVEL)/doc/sopwith.cfg.5 \
	       -o $@/doc/sopwith.cfg.html
	pandoc -f man $(PANDOC_FLAGS) -s $(TOPLEVEL)/doc/sopwith-mission.5 \
	       -o $@/doc/sopwith-mission.html

mangled.6: $(TOPLEVEL)/doc/sopwith.6
	awk "$(MANPAGE_MANGLE_AWK)" < $< > $@

clean:
	rm -rf page mangled.6
