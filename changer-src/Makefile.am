# Makefile for Amanda tape changer programs.

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

SUFFIXES =		.pl .sh

.pl:
			cat $< > $@
			chmod a+x $@

libexec_PROGRAMS =	@BUILD_CHANGER_PROGS_LIBEXEC@

EXTRA_PROGRAMS =	seagate-changer

libexec_SCRIPTS =	@BUILD_CHANGER_SCRIPTS_LIBEXEC@

EXTRA_SCRIPTS =		chg-generic hp-changer no-changer rth-changer

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
	done
