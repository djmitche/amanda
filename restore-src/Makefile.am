# Makefile for Amanda restore programs.

INCLUDES =		-I$(top_srcdir)/common-src -I$(top_srcdir)/tape-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

LDADD = ../tape-src/libamtape.a \
	../common-src/libamanda.a ../common-src/libamnolog.a

sbin_PROGRAMS = 	@BUILD_RESTORE_PROGS_SBIN@

libexec_PROGRAMS = 	@BUILD_RESTORE_PROGS_LIBEXEC@

EXTRA_PROGRAMS = 	amrestore	amidxtaped

amidxtaped_SOURCES = 	amidxtaped.c

install-exec-hook:
	@list="$(sbin_PROGRAMS)"; for p in $$list; do \
		pa=$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done; \
	list="$(libexec_PROGRAMS)"; for p in $$list; do \
		pa=$(libexecdir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done; \
