CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2 -g -fPIC -D_POSIX_C_SOURCE=200809L
CPPFLAGS += -Isrc -Iinclude -DXML2PDF_FONT_PATH=\"$(PREFIX)/share/ki_xml_pdf/DejaVuSans.ttf\"
PREFIX ?= /usr/local
VERSION := $(shell sed -n 's/^\#define KI_XML_PDF_VERSION "\([0-9.]*\)"/\1/p' include/ki_xml_pdf.h)
VERSION_MAJOR := $(word 1,$(subst ., ,$(VERSION)))
VERSION_MINOR := $(word 2,$(subst ., ,$(VERSION)))
# A source archive always describes HEAD, including when the working tree is dirty.
DIST_VERSION = $(shell git show HEAD:include/ki_xml_pdf.h 2>/dev/null | sed -n 's/^\#define KI_XML_PDF_VERSION "\([0-9.]*\)"/\1/p')
DIST_ARCHIVE = ki_xml_pdf-$(DIST_VERSION).tar.gz

LIB_SRCS = src/xml_parse.c src/font.c src/geometry.c src/page_sink.c src/pdf_write.c src/layout.c src/report.c src/convert.c
LIB_OBJS = $(LIB_SRCS:.c=.o)

PDFTOTEXT ?= pdftotext
# zlib provides Flate compression for page and font streams.
KI_LIBS = -lz
# Internal tests replace these zlib entry points to reach compressor errors.
TEST_WRAP = -Wl,--wrap=fsync -Wl,--wrap=deflate -Wl,--wrap=deflateInit2_

.PHONY: all test check clean test-san install example dist distcheck font_prefix_check print-version print-dist-name

all: xml2pdf

src/.font_prefix: font_prefix_check
	printf '%s\n' '$(PREFIX)' > $@.new
	if cmp -s $@.new $@; then rm -f $@.new; else mv $@.new $@; fi

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c -o $@ $<

src/font.o: src/.font_prefix

libki_xml_pdf.a: $(LIB_OBJS)
	ar rcs $@ $(LIB_OBJS)

libki_xml_pdf.so: $(LIB_OBJS) libki_xml_pdf.map
	$(CC) -shared -Wl,-soname,libki_xml_pdf.so.0 -Wl,--version-script=libki_xml_pdf.map -o $@ $(LIB_OBJS) $(KI_LIBS)

xml2pdf: src/main.c libki_xml_pdf.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ src/main.c libki_xml_pdf.a $(KI_LIBS)

test_stream: libki_xml_pdf.a tests/test_stream.c tests/test_xml_subset.c tests/test_xml_coords.c tests/test_font_metrics.c tests/test_pdf_bounds.c tests/test_flate.c tests/test_layout_wrap.c tests/test_geometry.c tests/pdf_inflate.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/test_stream.c libki_xml_pdf.a $(KI_LIBS) -pthread $(TEST_WRAP)

test: test_stream xml2pdf
	./test_stream
	python3 tests/pdf_offsets.py --binary ./xml2pdf
	python3 tests/pdf_streams.py --binary ./xml2pdf

check:
	$(MAKE) clean
	$(MAKE) test
	./tests/verify.sh
	$(MAKE) example
	python3 tests/cli_pipeline.py
	python3 tests/font_metrics_check.py
	python3 tests/wrap_check.py
	python3 tests/geometry_check.py
	python3 tests/catalog_verify.py

test-san:
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(CPPFLAGS) -o test_stream_san $(LIB_SRCS) tests/test_stream.c $(KI_LIBS) -pthread $(TEST_WRAP)
	ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./test_stream_san

example: libki_xml_pdf.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o examples/consume examples/consume.c libki_xml_pdf.a $(KI_LIBS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o examples/report examples/report.c libki_xml_pdf.a $(KI_LIBS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o examples/catalog examples/catalog_driver.c examples/catalog_map.c libki_xml_pdf.a $(KI_LIBS)
	$(CXX) $(CXXFLAGS) -Wall -Wextra $(CPPFLAGS) -o examples/consume_cpp examples/consume.cpp libki_xml_pdf.a $(KI_LIBS)

install: xml2pdf libki_xml_pdf.a libki_xml_pdf.so
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include \
		$(DESTDIR)$(PREFIX)/share/ki_xml_pdf
	cp xml2pdf $(DESTDIR)$(PREFIX)/bin/xml2pdf
	cp libki_xml_pdf.a libki_xml_pdf.so $(DESTDIR)$(PREFIX)/lib/
	cp include/ki_xml_pdf.h $(DESTDIR)$(PREFIX)/include/
	cp fonts/DejaVuSans.ttf fonts/DejaVuSans.copyright LICENSE $(DESTDIR)$(PREFIX)/share/ki_xml_pdf/
	ln -sfn libki_xml_pdf.so $(DESTDIR)$(PREFIX)/lib/libki_xml_pdf.so.0
	mkdir -p "$(DESTDIR)$(PREFIX)/lib/cmake/ki_xml_pdf" "$(DESTDIR)$(PREFIX)/lib/pkgconfig"
	sed -e 's|@KI_VERSION@|$(VERSION)|g' -e 's|@KI_DEFAULT_SHARED@|ON|g' \
		cmake/ki_xml_pdfConfig.cmake.in > "$(DESTDIR)$(PREFIX)/lib/cmake/ki_xml_pdf/ki_xml_pdfConfig.cmake"
	sed -e 's|@KI_VERSION@|$(VERSION)|g' -e 's|@KI_VERSION_MAJOR@|$(VERSION_MAJOR)|g' \
		-e 's|@KI_VERSION_MINOR@|$(VERSION_MINOR)|g' cmake/ki_xml_pdfConfigVersion.cmake.in \
		> "$(DESTDIR)$(PREFIX)/lib/cmake/ki_xml_pdf/ki_xml_pdfConfigVersion.cmake"
	cp cmake/ki_xml_pdfMakeTargets.cmake.in "$(DESTDIR)$(PREFIX)/lib/cmake/ki_xml_pdf/ki_xml_pdfTargets.cmake"
	sed -e 's|@KI_VERSION@|$(VERSION)|g' -e 's|@KI_PREFIX@|$(PREFIX)|g' \
		-e 's|@KI_LIBDIR@|$(PREFIX)/lib|g' -e 's|@KI_INCLUDEDIR@|$(PREFIX)/include|g' \
		cmake/ki_xml_pdf.pc.in > "$(DESTDIR)$(PREFIX)/lib/pkgconfig/ki_xml_pdf.pc"

dist:
	test -n "$(DIST_VERSION)"
	git archive --format=tar.gz --prefix=ki_xml_pdf-$(DIST_VERSION)/ -o $(DIST_ARCHIVE) HEAD
	sha256sum $(DIST_ARCHIVE)

print-version:
	@printf '%s\n' '$(VERSION)'

print-dist-name:
	@test -n '$(DIST_VERSION)'
	@printf '%s\n' '$(DIST_ARCHIVE)'

distcheck: dist
	CC="$(CC)" CXX="$(CXX)" PDFTOTEXT="$(PDFTOTEXT)" \
		DISTCHECK_ORACLE="$(DISTCHECK_ORACLE)" DISTCHECK_BREAK="$(DISTCHECK_BREAK)" \
		bash tests/distcheck.sh $(DIST_ARCHIVE)

clean:
	rm -f xml2pdf test_stream test_stream_san libki_xml_pdf.a libki_xml_pdf.so examples/consume examples/consume_cpp examples/report examples/catalog
	rm -f $(LIB_OBJS) $(LIB_OBJS:.o=.d) src/.font_prefix src/.font_prefix.new

-include $(LIB_OBJS:.o=.d)
