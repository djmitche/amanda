# Makefile for Amanda file recovery programs.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

INCLUDES =		-I$(top_srcdir)/common-src -I$(top_srcdir)/client-src \
			-I$(top_srcdir)/tape-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

if WANT_LIBTOOL
LIB_EXTENSION = la
else
LIB_EXTENSION = a
endif

sbin_PROGRAMS =		amrecover

LDADD = @LEXLIB@ \
	../client-src/libamclient.$(LIB_EXTENSION) \
	../tape-src/libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)

amrecover_SOURCES =	amrecover.c					\
			display_commands.c		extract_list.c	\
			help.c				set_commands.c	\
			uparse.y			uscan.l

noinst_HEADERS = 	amrecover.h uparse.h

YFLAGS =		-d

install-exec-hook:
	@list="$(sbin_PROGRAMS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
		echo chmod o-rwx $$pa; \
		chmod o-rwx $$pa; \
	done

# so that uscan.c is never generated before uparse.h
# otherwise we might have makedepend problems
$(srcdir)/uscan.c: $(srcdir)/uparse.h
# work around a bug in automake-1.2
$(srcdir)/uparse.h: $(srcdir)/uparse.c
	touch $@
