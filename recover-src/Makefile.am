# Makefile for Amanda file recovery programs.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

INCLUDES =		-I$(top_srcdir)/common-src -I$(top_srcdir)/client-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)
 
LDADD = ../client-src/libamclient.la ../common-src/libamanda.la \
	../common-src/libamnolog.la @LEXLIB@

EXTRA_PROGRAMS = 	amrecover

sbin_PROGRAMS =		@BUILD_RECOVER_PROGS_SBIN@

amrecover_SOURCES =	amrecover.c					\
			display_commands.c		extract_list.c	\
			help.c				set_commands.c	\
			uparse.y			uscan.l

noinst_HEADERS = 	amrecover.h uparse.h

YFLAGS =		-d

install-exec-hook:
	@list="$(sbin_PROGRAMS)"; \
	for p in $$list; do \
		pa=$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done

# so that uscan.c is never generated before uparse.h
# otherwise we might have makedepend problems
$(srcdir)/uscan.c: $(srcdir)/uparse.h
# work around a bug in automake-1.2
$(srcdir)/uparse.h: $(srcdir)/uparse.c
	touch $@
