# Makefile for Amanada server programs.

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

LDADD =			../common-src/libamanda.a

SUFFIXES =		.sh .pl

.pl:
			cat $< > $@
			chmod a+x $@

.sh:
			cat $< > $@
			chmod a+x $@

bin_PROGRAMS =		@BUILD_SERVER_PROGS_BIN@

libexec_PROGRAMS =	@BUILD_SERVER_PROGS_LIBEXEC@

bin_SCRIPTS =		@BUILD_SERVER_SCRIPTS_BIN@

EXTRA_PROGRAMS =	amadmin				amcheck		\
			amflush				amgetidx	\
			amidxtaped			amindexd	\
			amlabel				amtape		\
			amtrmidx			driver		\
			dumper				getconf		\
			planner				reporter	\
			taper

EXTRA_SCRIPTS =		amcheckdb			amcleanup	\
			amdump				amoverview	\
			amrmtape			amtoc		\
			amverify

amindexd_SOURCES =	amindexd.c			amindexd.h	\
			disk_history.c			disk_history.h	\
			list_dir.c

driver_SOURCES =	driver.c			driver.h	\
			driverio.c

install-exec-hook:
	@list="$(bin_PROGRAMS) $(bin_SCRIPTS)"; for p in $$list; do \
		echo chown $(CLIENT_LOGIN) $(bindir)/`echo $$p|sed '$(transform)'`; \
		chown $(CLIENT_LOGIN) $(bindir)/`echo $$p|sed '$(transform)'`; \
		echo chgrp $(SETUID_GROUP) $(bindir)/`echo $$p|sed '$(transform)'`; \
		chgrp $(SETUID_GROUP) $(bindir)/`echo $$p|sed '$(transform)'`; \
	done; \
	list="$(libexec_PROGRAMS) $(libexec_SCRIPTS)"; for p in $$list; do \
		echo chown $(CLIENT_LOGIN) $(libexecdir)/`echo $$p|sed '$(transform)'`; \
		chown $(CLIENT_LOGIN) $(libexecdir)/`echo $$p|sed '$(transform)'`; \
		echo chgrp $(SETUID_GROUP) $(libexecdir)/`echo $$p|sed '$(transform)'`; \
		chgrp $(SETUID_GROUP) $(libexecdir)/`echo $$p|sed '$(transform)'`; \
	done; \
	list="$(bindir)/amcheck $(libexecdir)/amgetidx $(libexecdir)/dumper $(libexecdir)/planner"; for p in $$list; do \
		if echo "$(bin_PROGRAMS) $(bin_SCRIPTS) $(libexec_PROGRAMS) $(libexec_SCRIPTS)" | grep `basename $$p` >/dev/null 2>&1; then \
			echo chown root `echo $$p|sed '$(transform)'`; \
			chown root `echo $$p|sed '$(transform)'`; \
			echo chmod u+s,o-rwx `echo $$p|sed '$(transform)'`; \
			chmod u+s,o-rwx `echo $$p|sed '$(transform)'`; \
		fi; \
	done
