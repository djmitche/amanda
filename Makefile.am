## Process this file with automake to produce Makefile.in

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4
LIBTOOL_M4 = @LIBTOOL_M4@
ACLOCAL = @ACLOCAL@ -I config @LIBTOOL_M4_MACRO_DIR@
AUTOCONF = @AUTOCONF@ --localdir=config

SUBDIRS=config		\
	common-src	client-src	tape-src	\
	server-src	restore-src	recover-src	\
	changer-src	amplot		man		\
	example

EXTRA_DIST = \
	CHANGES				COPYRIGHT			\
	COPYRIGHT-APACHE		COPYRIGHT-REGEX 		\
	docs/INDEXING			docs/INSTALL			\
	docs/INTERNALS			docs/KERBEROS			\
	docs/MULTITAPE			docs/RESTORE			\
	docs/SAMBA			docs/SUNOS4.BUG			\
	docs/SYSTEM.NOTES		docs/TAPE.CHANGERS		\
	docs/TAPETYPES			docs/UPGRADE			\
	docs/WHATS.NEW			docs/WISHLIST			\
	patches/readline-2.0-all	patches/readline-2.0-hpux	\
	patches/readline-2.0-irix4	patches/readline-2.0-more	\
	patches/regex-3.6alpha.patch	patches/samba-1.9.16p2.patch	\
	patches/samba-largefs.patch	patches/tar-1.12.patch		\
	patches/gcc-sol26.patch						\
	regex-src/COPYRIGHT		regex-src/Makefile 		\
	regex-src/README		regex-src/WHATSNEW 		\
	regex-src/cclass.h		regex-src/cname.h		\
	regex-src/debug.c		regex-src/engine.c		\
	regex-src/main.c		regex-src/mkh			\
	regex-src/regcomp.c		regex-src/regerror.c		\
	regex-src/regex.3		regex-src/regex.7		\
	regex-src/regex2.h		regex-src/regexec.c		\
	regex-src/regfree.c		regex-src/split.c		\
	regex-src/tests			regex-src/utils.h		\
	regex-src/fake/limits.h		regex-src/fake/memmove.c	\
	regex-src/fake/stdlib.h

$(ACLOCAL_M4): $(srcdir)/config/acinclude.m4 $(LIBTOOL_M4)
	rm -f $(srcdir)/aclocal.m4 $(ACLOCAL_M4)
	cd $(srcdir) && $(ACLOCAL) --output=$(ACLOCAL_M4)
	test ! -f $(srcdir)/aclocal.m4 || \
	mv $(srcdir)/aclocal.m4 $(ACLOCAL_M4)
