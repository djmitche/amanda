# Makefile for Amanda server programs.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

INCLUDES =		-I$(top_srcdir)/common-src -I$(top_srcdir)/tape-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

if WANT_LIBTOOL
lib_LTLIBRARIES = 	libamserver.la
LIB_EXTENSION = la
else
noinst_LIBRARIES = 	libamserver.a
LIB_EXTENSION = a
endif

sbin_PROGRAMS =		amadmin amcheck amflush amlabel amtape amreport

libexec_PROGRAMS =	amindexd amtrmidx amtrmlog driver dumper getconf planner taper amcleanupdisk

sbin_SCRIPTS =		amcheckdb amcleanup amdump amoverview amrmtape amtoc amverify amstatus

libamserver_la_SOURCES=	amindex.c	changer.c	clock.c		\
			conffile.c	diskfile.c	driverio.c	\
			holding.c	infofile.c	logfile.c	\
			tapefile.c	find.c

libamserver_la_LDFLAGS= -release $(VERSION)

libamserver_a_SOURCES=	$(libamserver_la_SOURCES)

LDADD =	libamserver.$(LIB_EXTENSION) \
	../tape-src/libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)

SUFFIXES =		.sh .pl

.pl:
			cat $< > $@
			chmod a+x $@

.sh:
			cat $< > $@
			chmod a+x $@

# there are used for testing only:
EXTRA_PROGRAMS = diskfile conffile infofile

CLEANFILES = *.test.c

amindexd_SOURCES =	amindexd.c					\
			disk_history.c			disk_history.h	\
			list_dir.c			list_dir.h

amreport_SOURCES =	reporter.c

noinst_HEADERS = 	amindex.h	changer.h	clock.h		\
			conffile.h	diskfile.h	driverio.h	\
			holding.h	infofile.h	logfile.h	\
			tapefile.h	find.h

install-exec-hook:
	@list="$(sbin_PROGRAMS) $(sbin_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
	@list="$(libexec_PROGRAMS) $(libexec_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(libexecdir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
	@list="$(sbindir)/amcheck $(libexecdir)/dumper $(libexecdir)/planner"; \
	for p in $$list; do \
		if echo "$(sbin_PROGRAMS) $(sbin_SCRIPTS) $(libexec_PROGRAMS) $(libexec_SCRIPTS)" | grep `basename $$p` >/dev/null 2>&1; then \
			pa=$(DESTDIR)`echo $$p|sed '$(transform)'`; \
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
