CC=gcc
CFLAGS=-Wall `pkg-config --cflags gtk+-3.0 sqlite3`
LIBS=-lm `pkg-config --libs gtk+-3.0 sqlite3`
CUNIT_CFLAGS=`PKG_CONFIG_PATH=$(HOME)/.local/lib/pkgconfig:$(PKG_CONFIG_PATH) pkg-config --cflags cunit`
CUNIT_LIBS=`PKG_CONFIG_PATH=$(HOME)/.local/lib/pkgconfig:$(PKG_CONFIG_PATH) pkg-config --libs cunit`
CUNIT_LIBDIR=`PKG_CONFIG_PATH=$(HOME)/.local/lib/pkgconfig:$(PKG_CONFIG_PATH) pkg-config --variable=libdir cunit`
bindir ?= /usr/bin
mandir ?= /usr/share/man

.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $<

all: main.o sort.o points.o drawing.o model.o datastore.o g3data2.1.gz
	$(CC) $(CFLAGS) -o g3data2 main.o sort.o points.o drawing.o model.o datastore.o $(LIBS)
	strip g3data2

main.o: main.c main.h strings.h vardefs.h

sort.o: sort.c main.h

points.o: points.c main.h

drawing.o: drawing.c main.h

model.o: model.c model.h

datastore.o: datastore.c datastore.h model.h

test_datastore: tests/test_datastore.c model.c model.h datastore.c datastore.h points.c sort.c main.h
	$(CC) $(CFLAGS) $(CUNIT_CFLAGS) -o $@ tests/test_datastore.c model.c datastore.c points.c sort.c $(LIBS) $(CUNIT_LIBS) -Wl,-rpath,$(CUNIT_LIBDIR)

test: test_datastore
	./test_datastore

g3data2.1.gz: g3data2.sgml
	rm -f *.1
	onsgmls g3data2.sgml | sgmlspl /usr/share/sgml/docbook/utils-0.6.14/helpers/docbook2man-spec.pl
	gzip g3data2.1

clean:
	rm -f *.o tests/*.o g3data2 test_datastore .test-datastore-passed g3data2.1.gz *~ manpage.*

install:
	install g3data2 $(bindir)
	install g3data2.1.gz $(mandir)/man1

uninstall:
	rm $(bindir)/g3data2
