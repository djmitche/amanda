# Makefile for Amanda server programs.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

# last updated on amanda 2.4.0
libserver_version = 3:0:0

INCLUDES =		-I$(top_srcdir)/common-src -I$(top_srcdir)/tape-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

LDADD =	libamserver.$(LIB_EXTENSION) \
	../tape-src/libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION) \
	libamserver.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)

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

# there are used for testing only:
TEST_PROGS = diskfile conffile infofile

EXTRA_PROGRAMS =	amadmin				amcheck		\
			amflush				amindexd	\
			amlabel				amtape		\
			amtrmidx			driver		\
			dumper				getconf		\
			planner				reporter	\
			taper						\
			$(TEST_PROGS)

EXTRA_SCRIPTS =		amcheckdb			amcleanup	\
			amdump				amoverview	\
			amrmtape			amtoc		\
			amverify			amstatus

CLEANFILES = *.test.c

DISTCLEANFILES = $(EXTRA_SCRIPTS)

amindexd_SOURCES =	amindexd.c					\
			disk_history.c			disk_history.h	\
			list_dir.c			list_dir.h

driver_SOURCES =	driver.c

lib_LTLIBRARIES = 	@BUILD_SERVER_LTLIB@
EXTRA_LTLIBRARIES = 	libamserver.la

noinst_LIBRARIES = 	@BUILD_SERVER_LIB@
EXTRA_LIBRARIES = 	libamserver.a

libamserver_la_SOURCES=	amindex.c	changer.c	clock.c		\
			conffile.c	diskfile.c	driverio.c	\
			holding.c	infofile.c	logfile.c	\
			tapefile.c	find.c

libamserver_la_LDFLAGS= -version-info $(libserver_version) -rpath $(libdir)

libamserver_a_SOURCES=	$(libamserver_la_SOURCES)

noinst_HEADERS = 	amindex.h	changer.h	clock.h		\
			conffile.h	diskfile.h	driverio.h	\
			holding.h	infofile.h	logfile.h	\
			tapefile.h	find.h

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

diskfile_SOURCES = diskfile.test.c
conffile_SOURCES = conffile.test.c
infofile_SOURCES = infofile.test.c

%.test.c: $(srcdir)/%.c
	echo '#define TEST' >$@
	echo '#include "$<"' >>$@
