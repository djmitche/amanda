# Makefile for Amanda library.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

# last updated on amanda 2.4.0b5
libamanda_version = 3:0:1
# last updated on amanda 2.4.0b1
libnolog_version = 0:0:0

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

BUILT_SOURCES = 	version.c

noinst_DATA =		versuff.c

lib_LTLIBRARIES =	libamanda.la libamnolog.la

REGDIR = ../regex-src
REGsrcdir = $(srcdir)/$(REGDIR)

INCLUDES = -I$(REGsrcdir)

libamanda_la_SOURCES = \
	alloc.c		amflock.c	debug.c		dgram.c		\
	error.c		file.c		match.c		protocol.c	\
	security.c	statfs.c	stream.c	token.c		\
	version.c	versuff.c	fileheader.c			\
	regcomp.c	regerror.c	regexec.c	regfree.c

libamanda_la_LIBADD =	@LTLIBOBJS@

libamanda_la_LDFLAGS =  -version-info $(libamanda_version) -allow-undefined

libamnolog_la_SOURCES =	nolog.c

libamnolog_la_LDFLAGS = -version-info $(libnolog_version)

noinst_HEADERS =	amanda.h	arglist.h	\
			dgram.h		protocol.h	\
			statfs.h	stream.h	\
			token.h		version.h	\
			amregex.h	fileheader.h

EXTRA_DIST = 		getcwd.c	mktime.c	strerror.c	\
			strstr.c	genversion.c	memmove.c

.sh:
	cat $< > $@
	chmod a+x $@


version.c:	$(srcdir)/genversion.c versuff.c $(srcdir)/version.h
	$(COMPILE) -DCC="\"$(CC)\"" -DBUILT_DATE="\"`date`\"" -DBUILT_MACH="\"`uname -a`\"" -c $(srcdir)/genversion.c
	$(COMPILE) -c versuff.c
	$(CC) $(LDFLAGS) genversion.o versuff.o -o genversion $(LIBS)
	./genversion > version.c
	rm -f genversion genversion.o versuff.o

.deps/match.P match.o match.lo: regex.h
.deps/regcomp.P regcomp.o regcomp.lo: regex.h regcomp.ih
.deps/regexec.P regexec.o regexec.lo: regex.h engine.ih
.deps/regerror.P regerror.o regerror.lo: regex.h regerror.ih
.deps/regfree.P regfree.o regfree.lo: regex.h

REGEXHSRC =	$(REGsrcdir)/regex2.h \
		$(REGsrcdir)/regcomp.c \
		$(REGsrcdir)/regexec.c \
		$(REGsrcdir)/regerror.c \
		$(REGsrcdir)/regfree.c

CLEANFILES = regex.h regcomp.ih engine.ih regerror.ih

DISTCLEANFILES = version.c

regex.h: $(REGEXHSRC) $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -i _REGEX_H_ $(REGEXHSRC) >$@

regcomp.ih: $(REGsrcdir)/regcomp.c $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -p $(REGsrcdir)/regcomp.c >$@

engine.ih: $(REGsrcdir)/engine.c $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -p $(REGsrcdir)/engine.c >$@

regerror.ih: $(REGsrcdir)/regerror.c $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -p $(REGsrcdir)/regerror.c >$@


LINK_TEST=$(COMPILE) $(LDFLAGS) -DTEST

statfs: $(srcdir)/statfs.c $(srcdir)/statfs.h
	$(LINK_TEST) $(srcdir)/statfs.c -o statfs

token: $(srcdir)/token.c $(srcdir)/token.h libamanda.la libamnolog.la
	$(LINK_TEST) $(srcdir)/token.c -o token .libs/libamanda.a .libs/libamnolog.a

file: $(srcdir)/file.c libamanda.la libamnolog.la
	$(LINK_TEST) $(srcdir)/file.c -o file .libs/libamanda.a .libs/libamnolog.a
