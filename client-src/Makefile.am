# Makefile for Amanda client programs.

INCLUDES =		-I$(top_srcdir)/common-src

if WANT_LIBTOOL
lib_LTLIBRARIES =	libamclient.la
LIB_EXTENSION = la
else
noinst_LIBRARIES =	libamclient.a
LIB_EXTENSION = a
endif

libexec_PROGRAMS =	amandad calcsize killpgrp rundump runtar selfcheck sendbackup sendsize versionsuffix

sbin_SCRIPTS =		@CLIENT_SCRIPTS_OPT@

libexec_SCRIPTS =	patch-system

if WANT_SAMBA
samba_sources = findpass.c
endif

libamclient_la_SOURCES=	amandates.c		getfsent.c	\
			unctime.c		$(samba_sources)

libamclient_la_LDFLAGS = -release $(VERSION)

libamclient_a_SOURCES=  $(libamclient_la_SOURCES)

LDADD =	libamclient.$(LIB_EXTENSION) \
	../common-src/libamanda.$(LIB_EXTENSION)

SUFFIXES =		.sh .pl

.pl:
			cat $< > $@
			chmod a+x $@

.sh:
			cat $< > $@
			chmod a+x $@

# these are used for testing only:
TEST_PROGS = getfsent

EXTRA_PROGRAMS =	$(TEST_PROGS)

EXTRA_SCRIPTS =		amhpfixdevs		amsinixfixdevs

DISTCLEANFILES = 	$(EXTRA_SCRIPTS)

EXTRA_DIST =		amhpfixdevs.sh		amsinixfixdevs.sh

sendbackup_SOURCES = 	sendbackup.c		sendbackup.h	\
			sendbackup-dump.c	sendbackup-gnutar.c

noinst_HEADERS	= 	amandates.h	getfsent.h	findpass.h

install-exec-hook:
	@list="$(sbin_SCRIPTS)"; \
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
	@list="$(libexecdir)/calcsize $(libexecdir)/killpgrp $(libexecdir)/rundump $(libexecdir)/runtar"; \
	for p in $$list; do \
		if echo "$(sbin_SCRIPTS) $(libexec_PROGRAMS) $(libexec_SCRIPTS)" | grep `basename $$p` >/dev/null 2>&1; then \
			pa=$(DESTDIR)`echo $$p|sed '$(transform)'`; \
			echo chown root $$pa; \
			chown root $$pa; \
			echo chmod u+s,o-rwx $$pa; \
			chmod u+s,o-rwx $$pa; \
		else true; \
		fi; \
	done

getfsent_SOURCES = getfsent.test.c

%.test.c: $(srcdir)/%.c
	echo '#define TEST' >$@
	echo '#include "$<"' >>$@
