## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS = 1.4 foreign

ACLOCAL_M4_FROM_SRCDIR = config/aclocal.m4
ACLOCAL_M4 = $(srcdir)/$(ACLOCAL_M4_FROM_SRCDIR)
ACINCLUDE_M4 = $(srcdir)/config/acinclude.m4
ACINCLUDE_M4I = $(srcdir)/config/acinclude.m4i
LIBTOOL_M4I = $(srcdir)/config/libtool.m4i
ACINCLUDE_M4_DEPS = $(ACINCLUDE_M4I) $(LIBTOOL_M4I)

ACLOCAL = @ACLOCAL@ -I config
AUTOCONF = @AUTOCONF@ --localdir=config
AUTOHEADER = @AUTOHEADER@ --localdir=config

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
	contrib/README			\
	contrib/dbbackup.README		\
	contrib/dbbackup.ksh		\
	contrib/dbbackup.sql		\
	contrib/dbbackup.tcl		\
        sst/Makefile                    \
	sst/README                      \
	sst/sst.c                       \
	sst/sst.conf                    \
	sst/sst_def.h                   \
	sst/sstest.c                    \
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

$(ACINCLUDE_M4): $(ACINCLUDE_M4_DEPS)
	@-rm -f $@
	@cat $(ACINCLUDE_M4_DEPS) > $@

$(ACLOCAL_M4): $(srcdir)/configure.in $(ACINCLUDE_M4)
	@-rm -f $(srcdir)/aclocal.m4 $(ACLOCAL_M4)
	cd $(srcdir) && $(ACLOCAL) --output=$(ACLOCAL_M4_FROM_SRCDIR)

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck
