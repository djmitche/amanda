# Makefile for Amanda tape changer programs.

INCLUDES =		-I$(top_srcdir)/common-src -I$(top_srcdir)/server-src -I$(top_srcdir)/tape-src

if WANT_LIBTOOL
LIB_EXTENSION = la
else
LIB_EXTENSION = a
endif

if WANT_CHIO_SCSI
CHIO_SCSI = chg-scsi-chio
endif

if WANT_CHG_SCSI
CHG_SCSI = chg-scsi
endif

libexec_PROGRAMS =	$(CHG_SCSI) $(CHIO_SCSI)

libexec_SCRIPTS =	chg-manual	chg-multi	chg-mtx		\
			chg-rth		chg-chs		chg-chio	\
			chg-zd-mtx

###
# Because libamanda includes routines (e.g. regex) provided by some system
# libraries, and because of the way libtool sets up the command line, we
# need to list libamanda twice here, first to override the system library
# routines, and second to pick up any references in the other libraries.
###

LDADD = ../common-src/libamanda.$(LIB_EXTENSION) \
	../server-src/libamserver.$(LIB_EXTENSION) \
	../tape-src/libamtape.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)

SUFFIXES =		.pl .sh

chg_scsi_SOURCES =	chg-scsi.c libscsi.h scsi-defs.h \
	scsi-aix.c scsi-changer-driver.c scsi-hpux_new.c scsi-irix.c \
	scsi-linux.c scsi-solaris.c scsi-bsd.c scsi-cam.c sense.c 

chg_scsi_chio_SOURCES = chg-scsi-chio.c scsi-hpux.c scsi-chio.c libscsi.h

EXTRA_DIST = scsi-proto.c

.pl:
			cat $< > $@
			chmod a+x $@

.sh:
			cat $< > $@
			chmod a+x $@

install-exec-hook:
	@list="$(sbin_PROGRAMS) $(sbin_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
	@list="$(libexec_PROGRAMS) $(libexec_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(libexecdir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
