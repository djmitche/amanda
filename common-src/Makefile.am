# Makefile for Amanda library.

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

BUILT_SOURCES = 	version.c
DISTCLEANFILES =	versuff.c

noinst_LIBRARIES =	@BUILD_CLIENT_LIB@ @BUILD_SERVER_LIB@ @BUILD_RECOVER_LIB@ libamanda.a

EXTRA_LIBRARIES = 	libamclient.a	libamserver.a	libamrecover.a

libamanda_a_SOURCES =	alloc.c		debug.c		\
			dgram.c		error.c		\
			log_dummy.c	match.c		protocol.c	\
			regex.c		statfs.c	stralloc.c	\
			stream.c	version.c	versuff.c	

libamclient_a_SOURCES =	amandates.c	fnmatch.c	getfsent.c	\
			unctime.c

libamserver_a_SOURCES =	amindex.c	changer.c	clock.c		\
			conffile.c	diskfile.c	infofile.c	\
			logfile.c	tapefile.c	tapeio.c

libamrecover_a_SOURCES = tapeio.c

EXTRA_DIST =		genversion.c

libamanda_a_LIBADD =	@ALLOCA@ @LIBOBJS@

noinst_HEADERS =	amanda.h	amandates.h	arglist.h	\
			changer.h	clock.h		conffile.h	\
			dgram.h		diskfile.h	findpass.h	\
			fnmatch.h	getfsent.h	infofile.h	\
			logfile.h	protocol.h	regex.h		\
			statfs.h	stream.h	tapefile.h	\
			tapeio.h	amindex.h	version.h

BUILT_SOURCES = 	version.c

.sh:
	cat $< > $@
	chmod a+x $@

version.c:	$(srcdir)/genversion.c versuff.c $(srcdir)/version.h
	$(COMPILE) -DCC="\"$(CC)\"" -DBUILT_DATE="\"`date`\"" -DBUILT_MACH="\"`uname -a`\"" -c $(srcdir)/genversion.c
	$(COMPILE) -c versuff.c
	 $(CC) $(LDFLAGS) genversion.o versuff.o -o genversion
	./genversion > version.c
	rm -f genversion genversion.o versuff.o

getfsent: $(srcdir)/getfsent.c $(srcdir)/getfsent.h
	$(CC) $(LDFLAGS) -DTEST -I.. -g $(srcdir)/getfsent.c -o getfsent
