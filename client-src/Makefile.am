# Makefile for Amanda client programs.

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

bin_PROGRAMS =		amrecover			amrestore

libexec_PROGRAMS =	versionsuffix					\
			@BUILD_CLIENT_PROGS_LIBEXEC@

EXTRA_PROGRAMS =	amandad				calcsize	\
			createindex-dump		createindex-gnutar \
			rundump				runtar		\
			selfcheck			sendbackup-dump	\
			sendbackup-gnutar 		sendindex	\
			sendsize

bin_SCRIPTS =		@BUILD_CLIENT_SCRIPTS_BIN@

libexec_SCRIPTS =	@BUILD_CLIENT_SCRIPTS_LIBEXEC@

EXTRA_SCRIPTS =		amhpfixdevs			patch-system

amrecover_SOURCES =	amrecover.c			amrecover.h	\
			display_commands.c		extract_list.c	\
			help.c				set_commands.c	\
			uparse.y			uscan.l

EXTRA_DIST =		amhpfixdevs.sh uparse.h

amrecover_LDADD =	../common-src/libamanda.a @LEXLIB@
YFLAGS =		-d

createindex_dump_SOURCES = createindex-dump.c		indexfilename.c \
			indexfilename.h

createindex_gnutar_SOURCES = createindex-gnutar.c	indexfilename.c \
			indexfilename.h

sendbackup_dump_SOURCES = sendbackup-dump.c		sendbackup-common.c \
			sendbackup-common.h

sendbackup_gnutar_SOURCES = sendbackup-gnutar.c		sendbackup-common.c \
			sendbackup-common.h

sendindex_SOURCES =	sendindex.c			indexfilename.c \
			indexfilename.h

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
	list="$(libexecdir)/calcsize $(libexecdir)/rundump $(libexecdir)/runtar"; for p in $$list; do \
		if echo "$(bin_PROGRAMS) $(bin_SCRIPTS) $(libexec_PROGRAMS) $(libexec_SCRIPTS)" | grep `basename $$p` >/dev/null 2>&1; then \
			echo chown root `echo $$p|sed '$(transform)'`; \
			chown root `echo $$p|sed '$(transform)'`; \
			echo chmod u+s,o-rwx `echo $$p|sed '$(transform)'`; \
			chmod u+s,o-rwx `echo $$p|sed '$(transform)'`; \
		fi; \
	done
