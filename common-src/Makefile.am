# Makefile for Amanda library.

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

DISTCLEANFILES =	version.c

noinst_LIBRARIES =	libamanda.a

libamanda_a_SOURCES =	alloc.c		amandates.c	changer.c	\
			clock.c		conffile.c	debug.c		\
			dgram.c		diskfile.c	error.c		\
			fnmatch.c	getfsent.c	infofile.c	\
			logfile.c	match.c		protocol.c	\
			regex.c		statfs.c	stralloc.c	\
			stream.c	tapefile.c	tapeio.c	\
			unctime.c	version.c	version.h	\
			versuff.c

EXTRA_DIST =		genversion.c

libamanda_a_LIBADD =	@ALLOCA@ @LIBOBJS@

noinst_HEADERS =	amanda.h	amandates.h	arglist.h	\
			changer.h	clock.h		conffile.h	\
			dgram.h		diskfile.h	findpass.h	\
			fnmatch.h	getfsent.h	infofile.h	\
			logfile.h	protocol.h	regex.h		\
			statfs.h	stream.h	tapefile.h	\
			tapeio.h

.sh:
	cat $< > $@
	chmod a+x $@

version.c:	genversion.c versuff.c version.h
	$(COMPILE) -DCC="\"$(CC)\"" -DBUILT_DATE="\"`date`\"" -DBUILT_MACH="\"`uname -a`\"" -c $(srcdir)/genversion.c
	$(COMPILE) -c $(srcdir)/versuff.c
	 $(CC) $(LDFLAGS) genversion.o versuff.o -o genversion
	./genversion > version.c
	rm -f genversion genversion.o versuff.o
