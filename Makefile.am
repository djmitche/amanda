## Process this file with automake to produce Makefile.in

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4
ACLOCAL = @ACLOCAL@ -I config @LIBTOOL_M4_MACRO_DIR@
AUTOCONF = @AUTOCONF@ --localdir=config

# order is significant, don't change it arbitrarily
SUBDIRS=config		\
	common-src	\
	client-src	\
	tape-src	\
	server-src	\
	restore-src	\
	recover-src	\
	changer-src	\
	amplot		\
	man		\
	example

EXTRA_DIST = \
	COPYRIGHT			\
	COPYRIGHT-APACHE		\
	COPYRIGHT-REGEX 		\
	docs/DUMPER-API			\
	docs/FAQ			\
	docs/INDEXING			\
	docs/INSTALL			\
	docs/INTERNALS			\
	docs/KERBEROS			\
	docs/LABEL.PRINTING		\
	docs/MULTITAPE			\
	docs/RESTORE			\
	docs/SAMBA			\
	docs/SECURITY			\
	docs/SYSTEM.NOTES		\
	docs/TAPE.CHANGERS		\
	docs/TAPETYPES			\
	docs/UPGRADE			\
	docs/WHATS.NEW			\
	docs/WISHLIST			\
	docs/YEAR2000			\
	docs/ZFTAPE			\
	patches/regex-3.6alpha.patch	\
	patches/samba-largefs.patch	\
	patches/tar-1.12.patch		\
	regex-src/COPYRIGHT		\
	regex-src/Makefile 		\
	regex-src/README		\
	regex-src/WHATSNEW 		\
	regex-src/cclass.h		\
	regex-src/cname.h		\
	regex-src/debug.c		\
	regex-src/engine.c		\
	regex-src/main.c		\
	regex-src/mkh			\
	regex-src/regcomp.c		\
	regex-src/regerror.c		\
	regex-src/regex.3		\
	regex-src/regex.7		\
	regex-src/regex2.h		\
	regex-src/regexec.c		\
	regex-src/regfree.c		\
	regex-src/split.c		\
	regex-src/tests			\
	regex-src/utils.h		\
	regex-src/fake/limits.h		\
	regex-src/fake/memmove.c	\
	regex-src/fake/stdlib.h

# even if libtool.m4 is not used at all, aclocal.m4 should be newer
# than libtool.m4 in a distribution
$(ACLOCAL_M4): $(srcdir)/config/libtool/libtool.m4

$(ACLOCAL_M4): $(srcdir)/configure.in $(srcdir)/config/acinclude.m4
	@rm -f $(srcdir)/aclocal.m4 $(ACLOCAL_M4)
	cd $(srcdir) && $(ACLOCAL) --output=$(ACLOCAL_M4)
	@if test -f $(srcdir)/aclocal.m4; then \
	    rm -f $(ACLOCAL_M4) && mv $(srcdir)/aclocal.m4 $(ACLOCAL_M4); \
	 fi
# this last line works around a bug in automake (fixed in release 1.3):
# it would always generate ./aclocal.m4, despite --output
