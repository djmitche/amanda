# Makefile for Amanda tape library.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

libtape_version = 0:0:0

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

lib_LTLIBRARIES = 	@BUILD_TAPE_LIB@

EXTRA_LTLIBRARIES = 	libamtape.la

libamtape_la_SOURCES = 	tapeio.c

libamtape_la_LDFLAGS =  -version-info $(libtape_version) -rpath $(libdir)

noinst_HEADERS = 	tapeio.h

