# Makefile for Amanda tape changer programs.

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

SUFFIXES =		.pl .sh

.pl:
			cat $< > $@
			chmod a+x $@

.sh:
			cat $< > $@
			chmod a+x $@

libexec_PROGRAMS =	@BUILD_CHANGER_PROGS_LIBEXEC@

EXTRA_PROGRAMS =	seagate-changer

libexec_SCRIPTS =	@BUILD_CHANGER_SCRIPTS_LIBEXEC@

EXTRA_SCRIPTS =		chg-generic hp-changer no-changer rth-changer

install-exec-hook:
	@list="$(bin_PROGRAMS) $(bin_SCRIPTS)"; for p in $$list; do \
		pa=$(bindir)/`echo $$p|sed '$(transform)'`; \
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
	done
