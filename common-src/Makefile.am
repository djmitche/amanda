# Makefile for Amanda library.

if WANT_LIBTOOL
lib_LTLIBRARIES =	libamanda.la
else
noinst_LIBRARIES = 	libamanda.a
endif

REGDIR = ../regex-src
REGsrcdir = $(srcdir)/$(REGDIR)

INCLUDES = -I$(REGsrcdir)

libamanda_la_SOURCES = \
	alloc.c		amflock.c	\
	bsd-security.c	\
	debug.c		dgram.c		\
	event.c		error.c		\
	file.c		fileheader.c	\
	krb4-security.c	krb5-security.c	\
	match.c		\
	packet.c	pipespawn.c	protocol.c	\
	regcomp.c	regerror.c	regexec.c	regfree.c	\
	rsh-security.c	\
	security.c	sl.c		statfs.c	stream.c	\
	token.c		\
	util.c		\
	version.c	versuff.c

libamanda_la_LIBADD =	@LTLIBOBJS@ @LTALLOCA@
libamanda_la_LDFLAGS =  -release $(VERSION)

libamanda_a_SOURCES =	$(libamanda_la_SOURCES)
libamanda_a_LIBADD =	@LIBOBJS@ @ALLOCA@

noinst_HEADERS =	amanda.h	amregex.h	arglist.h	\
			dgram.h		\
			event.h		\
			packet.h	pipespawn.h	protocol.h	\
			queue.h		\
			sl.h		security.h	statfs.h	\
			stream.h	\
			token.h		\
			util.h		\
			version.h	\
			fileheader.h

.sh:
	cat $< > $@
	chmod a+x $@

EXTRA_PROGRAMS = genversion $(TEST_PROGS)

genversion_SOURCES = genversion.c versuff.c
genversion_LDADD = $(libamanda_a_LIBADD)

genversion.@OBJEXT@: genversion.h
genversion.h: $(top_builddir)/config.status
	-rm -f $@ $@.new
	echo '#define CC "$(CC)"' > $@.new
	echo '#define BUILT_DATE "'`date`'"' >> $@.new
	echo '#define BUILT_MACH "'`uname -a || echo UNKNOWN HOST`'"' >> $@.new
	mv $@.new $@

version.c:	genversion$(EXEEXT)
	-rm -f version.c
	./genversion > version.c

match.@OBJEXT@ match.lo: regex.h
regcomp.@OBJEXT@ regcomp.lo: regex.h regcomp.ih
regexec.@OBJEXT@ regexec.lo: regex.h engine.ih
regerror.@OBJEXT@ regerror.lo: regex.h regerror.ih
regfree.@OBJEXT@ regfree.lo: regex.h

REGEXHSRC =	$(REGsrcdir)/regex2.h \
		$(REGsrcdir)/regcomp.c \
		$(REGsrcdir)/regexec.c \
		$(REGsrcdir)/regerror.c \
		$(REGsrcdir)/regfree.c

# these are used for testing only:
TEST_PROGS = statfs token file bsdsecurity

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

STANDARD_COMMON_STUFF_NOT_FILE = \
	alloc.$(OBJEXT) \
	debug.$(OBJEXT) \
	error.$(OBJEXT) \
	util.$(OBJEXT)

STANDARD_COMMON_STUFF = \
	$(STANDARD_COMMON_STUFF_NOT_FILE) \
	file.$(OBJEXT)

statfs_SOURCES = statfs.test.c alloc.c debug.c error.c file.c
statfs_LDADD = $(libamanda_a_LIBADD) $(STANDARD_COMMON_STUFF)

token_SOURCES = token.test.c alloc.c debug.c error.c file.c
token_LDADD = $(libamanda_a_LIBADD) $(STANDARD_COMMON_STUFF)

file_SOURCES = file.test.c alloc.c debug.c error.c
file_LDADD = $(libamanda_a_LIBADD) $(STANDARD_COMMON_STUFF_NOT_FILE)

bsdsecurity_SOURCES = bsd-security.test.c
bsdsecurity_LDADD = $(libamanda_a_LIBADD) \
		    $(STANDARD_COMMON_STUFF) \
		    dgram.$(OBJEXT) \
		    event.$(OBJEXT) \
		    packet.$(OBJEXT) \
		    security.$(OBJEXT) \
		    versuff.$(OBJEXT)

%.test.c: $(srcdir)/%.c
	echo '#define TEST' >$@
	echo '#include "$<"' >>$@
