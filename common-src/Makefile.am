# Makefile for Amanda library.

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

BUILT_SOURCES = 	version.c
DISTCLEANFILES =	versuff.c

noinst_LIBRARIES =	libamanda.a libamnolog.a

libamanda_a_SOURCES =	alloc.c		debug.c		dgram.c		\
			error.c		match.c		protocol.c	\
			regex.c		statfs.c	stralloc.c	\
			stream.c	version.c	versuff.c	

libamanda_a_LIBADD =	@ALLOCA@ @LIBOBJS@

libamnolog_a_SOURCES = 	nolog.c

EXTRA_DIST =		genversion.c

noinst_HEADERS =	amanda.h	arglist.h	\
			dgram.h		protocol.h	\
			regex.h		statfs.h	stream.h	\
			version.h

BUILT_SOURCES = 	version.c

EXTRA_DIST = 		alloca.c	flock.c		getcwd.c	\
			mktime.c	strdup.c	strerror.c	\
			strstr.c	\
			genversion.c

.sh:
	cat $< > $@
	chmod a+x $@

version.c:	$(srcdir)/genversion.c versuff.c $(srcdir)/version.h
	$(COMPILE) -DCC="\"$(CC)\"" -DBUILT_DATE="\"`date`\"" -DBUILT_MACH="\"`uname -a`\"" -c $(srcdir)/genversion.c
	$(COMPILE) -c versuff.c
	 $(CC) $(LDFLAGS) genversion.o versuff.o -o genversion
	./genversion > version.c
	rm -f genversion genversion.o versuff.o
