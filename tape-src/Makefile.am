# Makefile for Amanda tape library.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

# last updated on amanda 2.4.0b5:
libtape_version = 1:0:0

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

lib_LTLIBRARIES = 	@BUILD_TAPE_LTLIB@
EXTRA_LTLIBRARIES = 	libamtape.la

noinst_LIBRARIES = 	@BUILD_TAPE_LIB@
EXTRA_LIBRARIES = 	libamtape.a

libamtape_la_SOURCES = 	tapeio.c

libamtape_la_LDFLAGS =  -version-info $(libtape_version) -rpath $(libdir)

libamtape_a_SOURCES =	$(libamtape_la_SOURCES)

noinst_HEADERS = 	tapeio.h

