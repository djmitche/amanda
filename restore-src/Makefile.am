# Makefile for Amanda restore programs.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

INCLUDES =		-I$(top_srcdir)/common-src -I$(top_srcdir)/tape-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

LDADD = ../tape-src/libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION) \
	../common-src/libamnolog.$(LIB_EXTENSION)

sbin_PROGRAMS = 	@BUILD_RESTORE_PROGS_SBIN@

libexec_PROGRAMS = 	@BUILD_RESTORE_PROGS_LIBEXEC@

EXTRA_PROGRAMS = 	amrestore	amidxtaped

amidxtaped_SOURCES = 	amidxtaped.c

install-exec-hook:
	@list="$(sbin_PROGRAMS)"; \
	for p in $$list; do \
		pa=$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
	@list="$(libexec_PROGRAMS)"; \
	for p in $$list; do \
		pa=$(libexecdir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
