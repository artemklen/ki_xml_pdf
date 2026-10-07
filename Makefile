CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2 -g -fPIC -D_POSIX_C_SOURCE=200809L
CPPFLAGS += -Isrc -Iinclude -DXML2PDF_FONT_PATH=\"$(PREFIX)/share/ki_xml_pdf/DejaVuSans.ttf\"
PREFIX ?= /usr/local

LIB_SRCS = src/xml_parse.c src/font.c src/page_sink.c src/pdf_write.c src/layout.c src/report.c src/convert.c
LIB_OBJS = $(LIB_SRCS:.c=.o)

PDFTOTEXT ?= pdftotext

.PHONY: all test check clean test-san install example dist distcheck font_prefix_check

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
	$(CC) -shared -Wl,-soname,libki_xml_pdf.so.0 -Wl,--version-script=libki_xml_pdf.map -o $@ $(LIB_OBJS)

xml2pdf: src/main.c libki_xml_pdf.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ src/main.c libki_xml_pdf.a

test_stream: libki_xml_pdf.a tests/test_stream.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/test_stream.c libki_xml_pdf.a -pthread -Wl,--wrap=fsync

test: test_stream
	./test_stream

check:
	$(MAKE) clean
	$(MAKE) test
	./tests/verify.sh

test-san:
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(CPPFLAGS) -o test_stream_san $(LIB_SRCS) tests/test_stream.c -pthread -Wl,--wrap=fsync
	./test_stream_san

example: libki_xml_pdf.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o examples/consume examples/consume.c libki_xml_pdf.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o examples/report examples/report.c libki_xml_pdf.a
	$(CXX) $(CXXFLAGS) -Wall -Wextra $(CPPFLAGS) -o examples/consume_cpp examples/consume.cpp libki_xml_pdf.a

install: xml2pdf libki_xml_pdf.a libki_xml_pdf.so
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include \
		$(DESTDIR)$(PREFIX)/share/ki_xml_pdf
	cp xml2pdf $(DESTDIR)$(PREFIX)/bin/xml2pdf
	cp libki_xml_pdf.a libki_xml_pdf.so $(DESTDIR)$(PREFIX)/lib/
	cp include/ki_xml_pdf.h $(DESTDIR)$(PREFIX)/include/
	cp fonts/DejaVuSans.ttf fonts/DejaVuSans.copyright LICENSE $(DESTDIR)$(PREFIX)/share/ki_xml_pdf/
	ln -sfn libki_xml_pdf.so $(DESTDIR)$(PREFIX)/lib/libki_xml_pdf.so.0

dist:
	git archive --format=tar.gz --prefix=ki_xml_pdf-0.1.0/ -o ki_xml_pdf-0.1.0.tar.gz HEAD
	sha256sum ki_xml_pdf-0.1.0.tar.gz

distcheck: dist
	CC="$(CC)" CXX="$(CXX)" PDFTOTEXT="$(PDFTOTEXT)" \
		DISTCHECK_ORACLE="$(DISTCHECK_ORACLE)" DISTCHECK_BREAK="$(DISTCHECK_BREAK)" \
		bash tests/distcheck.sh ki_xml_pdf-0.1.0.tar.gz

clean:
	rm -f xml2pdf test_stream test_stream_san libki_xml_pdf.a libki_xml_pdf.so examples/consume examples/consume_cpp
	rm -f $(LIB_OBJS) $(LIB_OBJS:.o=.d) src/.font_prefix src/.font_prefix.new

-include $(LIB_OBJS:.o=.d)
