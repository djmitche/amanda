# Makefile for Amanda library.

libamanda_version = 0:0:0
libnolog_version = 0:0:0

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

BUILT_SOURCES = 	version.c

noinst_DATA =		versuff.c

lib_LTLIBRARIES =	libamanda.la libamnolog.la

REGDIR = ../regex-src
REGsrcdir = $(srcdir)/$(REGDIR)

INCLUDES += -I$(REGsrcdir)

libamanda_la_SOURCES =	alloc.c		debug.c		dgram.c		\
			error.c		match.c		protocol.c	\
			statfs.c	stream.c	token.c		\
			version.c	versuff.c	amflock.c	\
			regcomp.c	regexec.c	\
			regerror.c	regfree.c

libamanda_la_LIBADD =	@LTLIBOBJS@

libamanda_la_LDFLAGS =  -version-info $(libamanda_version)

libamnolog_la_SOURCES =	nolog.c

libamnolog_la_LDFLAGS = -version-info $(libnolog_version)

noinst_HEADERS =	amanda.h	arglist.h	\
			dgram.h		protocol.h	\
			statfs.h	stream.h	\
			token.h		version.h	\
			amregex.h

EXTRA_DIST = 		getcwd.c	mktime.c	strerror.c	\
			strstr.c	genversion.c	memmove.c

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

.deps/regcomp.P: regex.h regcomp.ih
.deps/regexec.P: regex.h engine.ih
.deps/regerror.P: regex.h regerror.ih
.deps/regfree.P: regex.h

REGEXHSRC =	$(REGsrcdir)/regex2.h \
		$(REGsrcdir)/regcomp.c \
		$(REGsrcdir)/regexec.c \
		$(REGsrcdir)/regerror.c \
		$(REGsrcdir)/regfree.c

regex.h: $(REGEXHSRC) $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -i _REGEX_H_ $(REGEXHSRC) >$@

regcomp.ih: $(REGsrcdir)/regcomp.c $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -p $(REGsrcdir)/regcomp.c >$@

engine.ih: $(REGsrcdir)/engine.c $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -p $(REGsrcdir)/engine.c >$@

regerror.ih: $(REGsrcdir)/regerror.c $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -p $(REGsrcdir)/regerror.c >$@
