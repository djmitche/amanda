# Makefile for amanda man-pages

transform =	s,x,x,;

if WANT_AMPLOT
AMPLOT_MAN_PAGES = amplot.8
endif

COMMON_MAN8_PAGES = amanda.8
COMMON_MAN5_PAGES = amanda.conf.5

if WANT_SERVER
SERVER_MAN8_PAGES = amadmin.8 \
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
RECOVER_MAN8_PAGES = amrecover.8
endif

if WANT_RESTORE
RESTORE_MAN8_PAGES = amrestore.8
endif

# not autoconf-generated:
EXTRA_DIST = amadmin.8 amanda.8 amanda.conf.5 amcheck.8 amcheckdb.8 amcleanup.8 amdd.8 amdump.8 amflush.8 amgetconf.8 amlabel.8 ammt.8 amoverview.8 amplot.8 amreport.8 amrecover.8 amrestore.8 amrmtape.8 amstatus.8 amtape.8 amtapetype.8 amtoc.8 amverify.8 amverifyrun.8

man8_MANS = $(AMPLOT_MAN8_PAGES) \
	    $(COMMON_MAN8_PAGES) \
	    $(SERVER_MAN8_PAGES) \
	    $(RECOVER_MAN8_PAGES) \
	    $(RESTORE_MAN8_PAGES)

man5_MANS = $(COMMON_MAN5_PAGES)

install-data-hook:
	@list="$(man8_MANS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(man8dir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
	@list="$(man5_MANS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(man5dir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
