# Makefile for Amanda client programs.

libclient_version = 0:0:0

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

LDADD =	libamclient.la ../common-src/libamanda.la ../common-src/libamnolog.la

SUFFIXES =		.sh .pl

.pl:
			cat $< > $@
			chmod a+x $@

.sh:
			cat $< > $@
			chmod a+x $@

libexec_PROGRAMS =	versionsuffix					\
			@BUILD_CLIENT_PROGS_LIBEXEC@

EXTRA_PROGRAMS =	amandad				calcsize	\
			rundump				runtar		\
			selfcheck			sendbackup	\
			sendsize

sbin_SCRIPTS =		@BUILD_CLIENT_SCRIPTS_SBIN@

libexec_SCRIPTS =	@BUILD_CLIENT_SCRIPTS_LIBEXEC@

EXTRA_SCRIPTS =		amhpfixdevs			patch-system

EXTRA_DIST =		amhpfixdevs.sh

sendbackup_SOURCES = 	sendbackup.c			sendbackup.h	\
			sendbackup-dump.c		sendbackup-gnutar.c

lib_LTLIBRARIES =	@BUILD_CLIENT_LIB@

EXTRA_LTLIBRARIES = 	libamclient.la

libamclient_la_SOURCES=	amandates.c	getfsent.c	unctime.c	\
			@LIBSRCS_CLIENT@

EXTRA_libamclient_la_SOURCES = findpass.c
libamclient_la_DEPENDENCIES = @LIBOBJS_CLIENT@
libamclient_la_LIBADD =	@LIBOBJS_CLIENT@

libamclient_la_LDFLAGS = -version-info $(libclient_version) -rpath $(libdir)

noinst_HEADERS	= 	amandates.h	getfsent.h	findpass.h

install-exec-hook:
	@list="$(sbin_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done; \
	list="$(libexec_PROGRAMS) $(libexec_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(libexecdir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done; \
	list="$(libexecdir)/calcsize $(libexecdir)/rundump $(libexecdir)/runtar"; \
	for p in $$list; do \
		if echo "$(sbin_SCRIPTS) $(libexec_PROGRAMS) $(libexec_SCRIPTS)" | grep `basename $$p` >/dev/null 2>&1; then \
			pa=`echo $$p|sed '$(transform)'`; \
			echo chown root $$pa; \
			chown root $$pa; \
			echo chmod u+s,o-rwx $$pa; \
			chmod u+s,o-rwx $$pa; \
		fi; \
	done

getfsent: $(srcdir)/getfsent.c $(srcdir)/getfsent.h
	$(CC) $(LDFLAGS) -DTEST -I.. -g $(srcdir)/getfsent.c -o getfsent
