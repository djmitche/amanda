# Makefile for Amanda client programs.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

# last updated on amanda 2.4.0b5
libclient_version = 0:2:0

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

LDADD =	libamclient.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION) \
	../common-src/libamnolog.$(LIB_EXTENSION)

SUFFIXES =		.sh .pl

.pl:
			cat $< > $@
			chmod a+x $@

.sh:
			cat $< > $@
			chmod a+x $@

libexec_PROGRAMS =	versionsuffix		@BUILD_CLIENT_PROGS_LIBEXEC@

# these are used for testing only:
TEST_PROGS = getfsent

EXTRA_PROGRAMS =	amandad			calcsize	\
			rundump			runtar		\
			selfcheck		sendbackup	\
			sendsize				\
			$(TEST_PROGS)

sbin_SCRIPTS =		@BUILD_CLIENT_SCRIPTS_SBIN@

libexec_SCRIPTS =	@BUILD_CLIENT_SCRIPTS_LIBEXEC@

EXTRA_SCRIPTS =		amhpfixdevs		patch-system	\
			amsinixfixdevs

DISTCLEANFILES = $(EXTRA_SCRIPTS)

EXTRA_DIST =		amhpfixdevs.sh		amsinixfixdevs.sh

sendbackup_SOURCES = 	sendbackup.c		sendbackup.h	\
			sendbackup-dump.c	sendbackup-gnutar.c

lib_LTLIBRARIES =	@BUILD_CLIENT_LTLIB@
EXTRA_LTLIBRARIES = 	libamclient.la

noinst_LIBRARIES =	@BUILD_CLIENT_LIB@
EXTRA_LIBRARIES =	libamclient.a

libamclient_la_SOURCES=	amandates.c		getfsent.c	\
			unctime.c		@LIBSRCS_CLIENT@

EXTRA_libamclient_la_SOURCES = findpass.c
libamclient_la_DEPENDENCIES =	@LIBOBJS_CLIENT@
libamclient_la_LIBADD =	@LIBOBJS_CLIENT@
libamclient_la_LDFLAGS = -version-info $(libclient_version) -rpath $(libdir)

libamclient_a_SOURCES = $(libamclient_la_SOURCES)
EXTRA_libamclient_a_SOURCES = $(EXTRA_libamclient_la_SOURCES)
libamclient_a_DEPENDENCIES = $(libamclient_la_DEPENDENCIES)
libamclient_a_LIBADD = $(libamclient_la_LIBADD)

noinst_HEADERS	= 	amandates.h	getfsent.h	findpass.h

install-exec-hook:
	@list="$(sbin_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
	@list="$(libexec_PROGRAMS) $(libexec_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(libexecdir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
	@list="$(libexecdir)/calcsize $(libexecdir)/rundump $(libexecdir)/runtar"; \
	for p in $$list; do \
		if echo "$(sbin_SCRIPTS) $(libexec_PROGRAMS) $(libexec_SCRIPTS)" | grep `basename $$p` >/dev/null 2>&1; then \
			pa=`echo $$p|sed '$(transform)'`; \
			echo chown root $$pa; \
			chown root $$pa; \
			echo chmod u+s,o-rwx $$pa; \
			chmod u+s,o-rwx $$pa; \
		else true; \
		fi; \
	done

getfsent_SOURCES = getfsent.test.c

%.test.c: $(srcdir)/%.c
	echo '#define TEST' >$@
	echo '#include "$<"' >>$@
