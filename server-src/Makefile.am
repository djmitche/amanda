# Makefile for Amanda server programs.

###
# Yes, I really mean ../common-src.  The genversion.h file ends up getting
# built there rather than into the source tree.
###

INCLUDES =		-I$(top_srcdir)/common-src \
			-I../common-src \
			-I../restore-src \
			-I$(top_srcdir)/tape-src

lib_LTLIBRARIES = 	libamserver.la
LIB_EXTENSION = la

sbin_PROGRAMS =		amadmin		amcheck		amflush		\
			amgetconf	amlabel		amtape		\
			amreport

libexec_PROGRAMS =	amindexd	amlogroll	amtrmidx	\
			amtrmlog	driver		dumper		\
			planner		taper		amcleanupdisk	\
			chunker

sbin_SCRIPTS =		amcheckdb	amcleanup	amdump		\
			amoverview	amrmtape	amtoc		\
			amverify	amverifyrun	amstatus	\
			amcrypt		amaespipe

libamserver_la_SOURCES=	amindex.c	changer.c			\
			conffile.c	diskfile.c	driverio.c	\
			holding.c	infofile.c	logfile.c	\
			tapefile.c	find.c		server_util.c   \
                        taperscan.c

libamserver_la_LDFLAGS= -release $(VERSION)

###
# Because libamanda includes routines (e.g. regex) provided by some system
# libraries, and because of the way libtool sets up the command line, we
# need to list libamanda twice here, first to override the system library
# routines, and second to pick up any references in the other libraries.
###

LDADD = ../common-src/libamanda.$(LIB_EXTENSION) \
	libamserver.$(LIB_EXTENSION) \
	../tape-src/libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION) \
	../tape-src/libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)

SUFFIXES =		.sh .pl

.pl:
			cat $< > $@
			chmod a+x $@
			-test -z "$(PERL)" || $(PERL) -c $@

.sh:
			cat $< > $@
			chmod a+x $@

# there are used for testing only:
TEST_PROGS = diskfile conffile infofile

EXTRA_PROGRAMS =	$(TEST_PROGS)

CLEANFILES = *.test.c $(sbin_SCRIPTS)

amindexd_SOURCES =	amindexd.c					\
			disk_history.c			disk_history.h	\
			list_dir.c			list_dir.h

amreport_SOURCES =	reporter.c

amgetconf_SOURCES =	getconf.c

noinst_HEADERS = 	amindex.h	changer.h			\
			conffile.h	diskfile.h	driverio.h	\
			holding.h	infofile.h	logfile.h	\
			tapefile.h	find.h		server_util.h	\
			taperscan.h

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
	@list="amcheck"; \
	for p in $$list; do \
		if echo "$(sbin_PROGRAMS)" | grep $$p >/dev/null 2>&1; then \
			pa=$(DESTDIR)$(sbindir)/`echo $$p|sed '$(transform)'`; \
			echo chown root $$pa; \
			chown root $$pa; \
			echo chmod u+s,o-rwx $$pa; \
			chmod u+s,o-rwx $$pa; \
		else true; \
		fi; \
	done
	@list="dumper planner"; \
	for p in $$list; do \
		if echo "$(libexec_PROGRAMS)" | grep $$p >/dev/null 2>&1; then \
			pa=$(DESTDIR)$(libexecdir)/`echo $$p|sed '$(transform)'`; \
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
