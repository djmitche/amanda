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

sbin_PROGRAMS =		amrecover

libexec_PROGRAMS =	versionsuffix					\
			@BUILD_CLIENT_PROGS_LIBEXEC@

EXTRA_PROGRAMS =	amandad				calcsize	\
			rundump				runtar		\
			selfcheck			sendbackup-dump	\
			sendbackup-gnutar 		sendsize

sbin_SCRIPTS =		@BUILD_CLIENT_SCRIPTS_SBIN@

libexec_SCRIPTS =	@BUILD_CLIENT_SCRIPTS_LIBEXEC@

EXTRA_SCRIPTS =		amhpfixdevs			patch-system

amrecover_SOURCES =	amrecover.c			amrecover.h	\
			display_commands.c		extract_list.c	\
			help.c				set_commands.c	\
			uparse.y			uscan.l

EXTRA_DIST =		amhpfixdevs.sh uparse.h

amrecover_LDADD =	../common-src/libamanda.a @LEXLIB@
YFLAGS =		-d

sendbackup_dump_SOURCES = sendbackup-dump.c		sendbackup-common.c \
			sendbackup-common.h

sendbackup_gnutar_SOURCES = sendbackup-gnutar.c		sendbackup-common.c \
			sendbackup-common.h

install-exec-hook:
	@list="$(sbin_PROGRAMS) $(sbin_SCRIPTS)"; for p in $$list; do \
		pa=$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done; \
	list="$(libexec_PROGRAMS) $(libexec_SCRIPTS)"; for p in $$list; do \
		pa=$(libexecdir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done; \
	list="$(libexecdir)/calcsize $(libexecdir)/rundump $(libexecdir)/runtar"; for p in $$list; do \
		if echo "$(sbin_PROGRAMS) $(sbin_SCRIPTS) $(libexec_PROGRAMS) $(libexec_SCRIPTS)" | grep `basename $$p` >/dev/null 2>&1; then \
			pa=`echo $$p|sed '$(transform)'`; \
			echo chown root $$pa; \
			chown root $$pa; \
			echo chmod u+s,o-rwx $$pa; \
			chmod u+s,o-rwx $$pa; \
		fi; \
	done
