# Makefile for Amanda server programs.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

# last updated on amanda 2.4.0b4
libserver_version = 1:0:0

INCLUDES =		-I$(top_srcdir)/common-src -I$(top_srcdir)/tape-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

LDADD =	libamserver.la ../tape-src/libamtape.la ../common-src/libamanda.la \
        libamserver.la ../common-src/libamanda.la
# libamserver.la must be duplicated in some systems

SUFFIXES =		.sh .pl

.pl:
			cat $< > $@
			chmod a+x $@

.sh:
			cat $< > $@
			chmod a+x $@

sbin_PROGRAMS =		@BUILD_SERVER_PROGS_SBIN@

libexec_PROGRAMS =	@BUILD_SERVER_PROGS_LIBEXEC@

sbin_SCRIPTS =		@BUILD_SERVER_SCRIPTS_SBIN@

EXTRA_PROGRAMS =	amadmin				amcheck		\
			amflush				amindexd	\
			amlabel				amtape		\
			amtrmidx			driver		\
			dumper				getconf		\
			planner				reporter	\
			taper

EXTRA_SCRIPTS =		amcheckdb			amcleanup	\
			amdump				amoverview	\
			amrmtape			amtoc		\
			amverify

DISTCLEANFILES = $(EXTRA_SCRIPTS)

amindexd_SOURCES =	amindexd.c			amindexd.h	\
			disk_history.c			disk_history.h	\
			list_dir.c

driver_SOURCES =	driver.c			driver.h	\
			driverio.c

lib_LTLIBRARIES = 	@BUILD_SERVER_LIB@

EXTRA_LTLIBRARIES = 	libamserver.la

libamserver_la_SOURCES=	amindex.c	changer.c	clock.c		\
			conffile.c	diskfile.c	infofile.c	\
			logfile.c	tapefile.c

libamserver_la_LDFLAGS= -version-info $(libserver_version) -rpath $(libdir)

noinst_HEADERS = 	amindex.h	changer.h	clock.h		\
			conffile.h	diskfile.h	infofile.h	\
			logfile.h	tapefile.h

install-exec-hook:
	@list="$(sbin_PROGRAMS) $(sbin_SCRIPTS)"; \
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
	@list="$(sbindir)/amcheck $(libexecdir)/dumper $(libexecdir)/planner"; \
	for p in $$list; do \
		if echo "$(sbin_PROGRAMS) $(sbin_SCRIPTS) $(libexec_PROGRAMS) $(libexec_SCRIPTS)" | grep `basename $$p` >/dev/null 2>&1; then \
			pa=`echo $$p|sed '$(transform)'`; \
			echo chown root $$pa; \
			chown root $$pa; \
			echo chmod u+s,o-rwx $$pa; \
			chmod u+s,o-rwx $$pa; \
		else true; \
		fi; \
	done

LINK_TEST=$(COMPILE) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -DTEST

diskfile: $(srcdir)/diskfile.c conffile.o clock.o $(srcdir)/diskfile.h ../common-src/libamanda.la ../common-src/libamnolog.la
	$(LINK_TEST) $(srcdir)/diskfile.c conffile.o clock.o -o diskfile ../common-src/.libs/libamanda.a ../common-src/.libs/libamnolog.a

conffile: $(srcdir)/conffile.c $(srcdir)/conffile.h clock.o ../common-src/libamanda.la ../common-src/libamnolog.la
	$(LINK_TEST) $(srcdir)/conffile.c clock.o -o conffile ../common-src/.libs/libamanda.a ../common-src/.libs/libamnolog.a

infofile: $(srcdir)/infofile.c $(srcdir)/infofile.h ../common-src/libamanda.la ../common-src/libamnolog.la
	$(LINK_TEST) $(srcdir)/infofile.c -o infofile ../common-src/.libs/libamanda.a ../common-src/.libs/libamnolog.a
