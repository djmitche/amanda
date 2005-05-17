# Makefile for amanda man-pages

transform =	s,x,x,;

if WANT_AMPLOT
AMPLOT_MAN_PAGES = amplot.8
endif

COMMON_MAN_PAGES = amanda.8 amanda.conf.5

if WANT_SERVER
SERVER_MAN_PAGES = amadmin.8 \
		   amcheck.8 \
		   amcheckdb.8 \
		   amcleanup.8 \
		   amdd.8 \
		   amdump.8 \
		   amflush.8 \
		   amgetconf.8 \
		   amlabel.8 \
		   ammt.8 \
		   amoverview.8 \
		   amreport.8 \
		   amrmtape.8 \
		   amstatus.8 \
		   amtape.8 \
		   amtapetype.8 \
		   amtoc.8 \
		   amverify.8 \
		   amverifyrun.8
endif

if WANT_RECOVER
RECOVER_MAN_PAGES = amrecover.8
endif

if WANT_RESTORE
RESTORE_MAN_PAGES = amrestore.8
endif

# not autoconf-generated:
EXTRA_DIST = amadmin.8 amanda.8 amanda.conf.5 amcheck.8 amcheckdb.8 amcleanup.8 amdd.8 amdump.8 amflush.8 amgetconf.8 amlabel.8 ammt.8 amoverview.8 amplot.8 amreport.8 amrecover.8 amrestore.8 amrmtape.8 amstatus.8 amtape.8 amtapetype.8 amtoc.8 amverify.8 amverifyrun.8

man_MANS = $(AMPLOT_MAN_PAGES) \
	   $(COMMON_MAN_PAGES) \
	   $(SERVER_MAN_PAGES) \
	   $(RECOVER_MAN_PAGES) \
	   $(RESTORE_MAN_PAGES)

install-data-hook:
	@list="$(man_MANS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(mandir)/man8/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
