# Makefile for Amanda tape library.

INCLUDES =		-I$(top_srcdir)/common-src

if WANT_LIBTOOL
lib_LTLIBRARIES = 	libamtape.la
LIB_EXTENSION = la
else
noinst_LIBRARIES = 	libamtape.a
LIB_EXTENSION = a
endif

libamtape_la_SOURCES = 	tapeio.c rait.c

libamtape_la_LDFLAGS =  -release $(VERSION)

libamtape_a_SOURCES =	$(libamtape_la_SOURCES)

noinst_HEADERS = 	tapeio.h

EXTRA_PROGRAMS = 	tapetype

sbin_PROGRAMS=          am-mt am-dd

tapetype_LDADD = libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)

am_mt_LDADD = libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)

am_dd_LDADD = libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)

## So that we can tell people to `cd tape-src && make tapetype':
../common-src/libamanda.$(LIB_EXTENSION):
	cd ../common-src && $(MAKE) libamanda.$(LIB_EXTENSION)
