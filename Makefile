DESTDIR=
PREFIX=/usr
BINDIR=/bin
INSTALL=install
CFLAGS=-Wall -O2
OFLAGS=-O2

everything: ni

ni: ni.c
	$(CC) ${CFLAGS} -o ni ni.c

clean:
	-rm -rf ni nia tmp

nia: ni.c
	asan -O2 -o nia ni.c

test: nia
	./test.sh ./nia

install: ni
	-mkdir -p $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 755 ni $(DESTDIR)$(PREFIX)/bin/ni

uninstall:
	-rm $(DESTDIR)$(PREFIX)/bin/ni

