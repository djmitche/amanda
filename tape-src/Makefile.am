# Makefile for Amanda tape library.

INCLUDES =		-I$(top_srcdir)/common-src

if WANT_LIBTOOL
lib_LTLIBRARIES = 	libamtape.la
LIB_EXTENSION = la
else
noinst_LIBRARIES = 	libamtape.a
LIB_EXTENSION = a
endif

libamtape_la_SOURCES = 	tapeio.c

libamtape_la_LDFLAGS =  -release $(VERSION)

libamtape_a_SOURCES =	$(libamtape_la_SOURCES)

noinst_HEADERS = 	tapeio.h

EXTRA_PROGRAMS = 	tapetype

###
# Because libamanda includes routines (e.g. regex) provided by some system
# libraries, and because of the way libtool sets up the command line, we
# need to list libamanda twice here, first to override the system library
# routines, and second to pick up any references in the other libraries.
###

tapetype_LDADD =	../common-src/libamanda.$(LIB_EXTENSION) \
			libamtape.$(LIB_EXTENSION) \
			../common-src/libamanda.$(LIB_EXTENSION)

## So that we can tell people to `cd tape-src && make tapetype':
../common-src/libamanda.$(LIB_EXTENSION):
	cd ../common-src && $(MAKE) libamanda.$(LIB_EXTENSION)
