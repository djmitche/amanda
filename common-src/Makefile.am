# Makefile for Amanda library.

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

BUILT_SOURCES = 	version.c

noinst_DATA =		versuff.c

noinst_LIBRARIES =	libamanda.a libamnolog.a

libamanda_a_SOURCES =	alloc.c		debug.c		dgram.c		\
			error.c		match.c		protocol.c	\
			regex.c		statfs.c	stralloc.c	\
			stream.c	token.c		version.c	\
			versuff.c	

libamanda_a_LIBADD =	@ALLOCA@ @LIBOBJS@

libamnolog_a_SOURCES = 	nolog.c

noinst_HEADERS =	amanda.h	arglist.h	\
			dgram.h		protocol.h	\
			regex.h		statfs.h	stream.h	\
			token.h		version.h

EXTRA_DIST = 		alloca.c	flock.c		getcwd.c	\
			mktime.c	strdup.c	strerror.c	\
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

token: $(srcdir)/token.c $(srcdir)/token.h
	$(COMPILE) $(LDFLAGS) -DTEST -I.. $(srcdir)/token.c -o token
