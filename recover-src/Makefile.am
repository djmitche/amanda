# Makefile for Amanda file recovery programs.

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)
 
LDADD = -L../common-src -lamrecover -lamanda

EXTRA_PROGRAMS = 	amrecover			amrestore

sbin_PROGRAMS =		@BUILD_RECOVER_PROGS_SBIN@

amrecover_SOURCES =	amrecover.c			amrecover.h	\
			display_commands.c		extract_list.c	\
			help.c				set_commands.c	\
			uparse.y			uscan.l

EXTRA_DIST =		uparse.h

amrecover_LDADD =	-L../common-src -lamclient -lamanda @LEXLIB@

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
