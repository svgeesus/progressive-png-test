CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PYTHON  ?= python3
PNG_CFLAGS := $(shell pkg-config --cflags libpng)
PNG_LIBS   := $(shell pkg-config --libs libpng)

all: adam7split adam7trunc

adam7split: adam7split.c
	$(CC) $(CFLAGS) $(PNG_CFLAGS) -o $@ $< $(PNG_LIBS)

adam7trunc: adam7trunc.c
	$(CC) $(CFLAGS) $(PNG_CFLAGS) -o $@ $< $(PNG_LIBS) -lz

tests/interlace tests/progressive_decode: %: %.c
	$(CC) $(CFLAGS) $(PNG_CFLAGS) -o $@ $< $(PNG_LIBS)

# Check adam7split against tests/expected/, then adam7trunc against adam7split.
check: adam7split adam7trunc tests/progressive_decode
	$(PYTHON) tests/check.py ./adam7split
	$(PYTHON) tests/check_trunc.py ./adam7trunc ./adam7split tests/progressive_decode

# Write truncated files, adam7split references and masks for every test input
# to tests/truncated/, with an index.html for comparing them in browsers.
truncated: adam7trunc adam7split
	$(PYTHON) tests/make_truncated.py ./adam7trunc ./adam7split

# Copy the images the WPT reftests need from tests/truncated/ into
# wpt/support/, then write the reftests.  Images whose first truncation is
# already the complete file (1x1) are skipped: they show nothing progressive.
wpt: truncated
	rm -f wpt/*.html wpt/support/*.png
	mkdir -p wpt/support
	for f in tests/input/*.png; do \
	    n=$$(basename "$$f" .png); t=tests/truncated/$$n; \
	    cmp -s "$$t-trunc1.png" "$$t-trunc7.png" && continue; \
	    for p in 1 2 3 4 5 6; do \
	        cp "$$t-trunc$$p.png" "$$t-mask$$p.png" wpt/support/ || exit 1; \
	    done; \
	    cp "$$t-pass7.png" wpt/support/ || exit 1; \
	done
	$(PYTHON) tests/make_wpt.py

# Regenerate all test data: sources, Adam7 inputs and expected outputs.
test-data: tests/interlace
	rm -rf tests/source tests/input tests/expected
	$(PYTHON) tests/gen_sources.py
	mkdir -p tests/input
	for f in tests/source/*.png; do \
	    tests/interlace "$$f" "tests/input/$$(basename "$$f")" || exit 1; \
	done
	$(PYTHON) tests/add_hdr_chunks.py
	$(PYTHON) tests/make_expected.py

clean:
	rm -f adam7split adam7trunc tests/interlace tests/progressive_decode

.PHONY: all check truncated wpt test-data clean
