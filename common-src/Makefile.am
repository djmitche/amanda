# Makefile for Amanda library.

libamanda_version = 0:0:0
libnolog_version = 0:0:0

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

BUILT_SOURCES = 	version.c

noinst_DATA =		versuff.c

lib_LTLIBRARIES =	libamanda.la libamnolog.la

libamanda_la_SOURCES =	alloc.c		debug.c		dgram.c		\
			error.c		match.c		protocol.c	\
			regex.c		statfs.c	\
			stream.c	token.c		version.c	\
			versuff.c	amflock.c

libamanda_la_LIBADD =	@LTALLOCA@ @LTLIBOBJS@

libamanda_la_LDFLAGS =  -version-info $(libamanda_version)

libamnolog_la_SOURCES =	nolog.c

libamnolog_la_LDFLAGS = -version-info $(libnolog_version)

noinst_HEADERS =	amanda.h	arglist.h	\
			dgram.h		protocol.h	\
			regex.h		statfs.h	stream.h	\
			token.h		version.h

EXTRA_DIST = 		alloca.c	getcwd.c	\
			mktime.c	strerror.c	\
			strstr.c	genversion.c

.sh:
	cat $< > $@
	chmod a+x $@

version.c:	$(srcdir)/genversion.c versuff.c $(srcdir)/version.h
	$(COMPILE) -DCC="\"$(CC)\"" -DBUILT_DATE="\"`date`\"" -DBUILT_MACH="\"`uname -a`\"" -c $(srcdir)/genversion.c
	$(COMPILE) -c versuff.c
	$(CC) $(LDFLAGS) genversion.o versuff.o -o genversion
	./genversion > version.c
	rm -f genversion genversion.o versuff.o

statfs: $(srcdir)/statfs.c $(srcdir)/statfs.h
	$(COMPILE) $(LDFLAGS) -DTEST -I.. $(srcdir)/statfs.c -o statfs

token: $(srcdir)/token.c $(srcdir)/token.h libamanda.la libamnolog.la
	$(COMPILE) $(LDFLAGS) -DTEST -I.. $(srcdir)/token.c libamanda.la libamnolog.la -o token
