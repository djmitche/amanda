# Makefile for Amanda tape library.

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

noinst_LIBRARIES = 	@BUILD_TAPE_LIB@

EXTRA_LIBRARIES = 	libamtape.a

libamtape_a_SOURCES = 	tapeio.c

noinst_HEADERS = 	tapeio.h
