# Makefile for Amanda library.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

if WANT_LIBTOOL
lib_LTLIBRARIES =	libamanda.la
OBJ_EXTENSION = lo
else
noinst_LIBRARIES = 	libamanda.a
OBJ_EXTENSION = o
endif

REGDIR = ../regex-src
REGsrcdir = $(srcdir)/$(REGDIR)

INCLUDES = -I$(REGsrcdir)

libamanda_la_SOURCES = \
	alloc.c		amflock.c	debug.c		dgram.c		\
	error.c		file.c		fileheader.c	match.c		\
	protocol.c	regcomp.c	regerror.c	regexec.c	\
	regfree.c	security.c	statfs.c	stream.c	\
	token.c		version.c	versuff.c

libamanda_la_LIBADD =	@LIBOBJS@ @ALLOCA@
libamanda_la_LDFLAGS =  -release $(VERSION)

libamanda_a_SOURCES =	$(libamanda_la_SOURCES)
libamanda_a_LIBADD =	$(libamanda_la_LIBADD)

noinst_HEADERS =	amanda.h	arglist.h	\
			dgram.h		protocol.h	\
			statfs.h	stream.h	\
			token.h		version.h	\
			amregex.h	fileheader.h

.sh:
	cat $< > $@
	chmod a+x $@

EXTRA_PROGRAMS = genversion $(TEST_PROGS)

genversion_SOURCES = genversion.c
genversion_LDADD = versuff.$(OBJ_EXTENSION) $(libamanda_a_LIBADD) \
	alloc.$(OBJ_EXTENSION) error.$(OBJ_EXTENSION)

genversion.o: genversion.c
	$(COMPILE) -DCC="\"$(CC)\"" \
		   -DBUILT_DATE="\"`date`\"" \
		   -DBUILT_MACH="\"`uname -a || echo UNKNOWN HOST`\"" \
		   -c $(srcdir)/genversion.c

version.c:	genversion
	-rm -f version.c
	./genversion > version.c

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

# these are used for testing only:
TEST_PROGS = statfs token file security

CLEANFILES = regex.h regcomp.ih engine.ih regerror.ih *.test.c

DISTCLEANFILES = version.c

regex.h: $(REGEXHSRC) $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -i _REGEX_H_ $(REGEXHSRC) >$@

regcomp.ih: $(REGsrcdir)/regcomp.c $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -p $(REGsrcdir)/regcomp.c >$@

engine.ih: $(REGsrcdir)/engine.c $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -p $(REGsrcdir)/engine.c >$@

regerror.ih: $(REGsrcdir)/regerror.c $(REGsrcdir)/mkh
	sh $(REGsrcdir)/mkh -o -p $(REGsrcdir)/regerror.c >$@

# used for testing only

statfs_SOURCES = statfs.test.c
statfs_LDADD = $(libamanda_a_LIBADD) \
	alloc.$(OBJ_EXTENSION) error.$(OBJ_EXTENSION)

token_SOURCES = token.test.c
token_LDADD = $(libamanda_a_LIBADD) \
	alloc.$(OBJ_EXTENSION) error.$(OBJ_EXTENSION) file.$(OBJ_EXTENSION)

file_SOURCES = file.test.c
file_LDADD = $(libamanda_a_LIBADD) \
	alloc.$(OBJ_EXTENSION) error.$(OBJ_EXTENSION)

security_SOURCES = security.test.c
security_LDADD = $(libamanda_a_LIBADD) \
	alloc.$(OBJ_EXTENSION) debug.$(OBJ_EXTENSION) \
	error.$(OBJ_EXTENSION) file.$(OBJ_EXTENSION)

%.test.c: $(srcdir)/%.c
	echo '#define TEST' >$@
	echo '#include "$<"' >>$@
