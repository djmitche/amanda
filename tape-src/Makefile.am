# Makefile for Amanda tape library.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

INCLUDES =		-I$(top_srcdir)/common-src

AMANDA_CFLAGS =		@AMANDA_CFLAGS@

COMPILE = $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(AMANDA_CFLAGS)

if WANT_TAPE

if WANT_LIBTOOL
lib_LTLIBRARIES = 	libamtape.la
LIB_EXTENSION = la
else
noinst_LIBRARIES = 	libamtape.a
LIB_EXTENSION = a
endif

endif

libamtape_la_SOURCES = 	tapeio.c

libamtape_la_LDFLAGS =  -release $(VERSION)

libamtape_a_SOURCES =	$(libamtape_la_SOURCES)

noinst_HEADERS = 	tapeio.h

EXTRA_PROGRAMS = 	tapetype

tapetype_LDADD = libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)
