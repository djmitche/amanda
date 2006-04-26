# Makefile for Amanda client programs.

INCLUDES =	-I$(top_builddir)/common-src \
		-I$(top_srcdir)/common-src

lib_LTLIBRARIES =       libamandad.la
LIB_EXTENSION = la

libexec_PROGRAMS =	amandad

if WANT_RUNTIME_PSEUDO_RELOC
AM_LDFLAGS = -Wl,-enable-runtime-pseudo-reloc
endif

libamandad_la_SOURCES= amandad_util.c
libamandad_la_LDFLAGS = -release $(VERSION)

###
# Because libamanda includes routines (e.g. regex) provided by some system
# libraries, and because of the way libtool sets up the command line, we
# need to list libamanda twice here, first to override the system library
# routines, and second to pick up any references in the other libraries.
###

LDADD = ../common-src/libamanda.$(LIB_EXTENSION) \
	libamandad.$(LIB_EXTENSION)
#if WANT_SERVER
#LDADD += ../tape-src/libamtape.$(LIB_EXTENSION)
#endif
LDADD += ../common-src/libamanda.$(LIB_EXTENSION)

install-exec-hook:
	@list="$(libexec_PROGRAMS) $(libexec_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(libexecdir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done

