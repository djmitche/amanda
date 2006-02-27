# Makefile for amanda man-pages

transform =	s,x,x,;

AMPLOT_MAN8_PAGES = amplot.8

COMMON_MAN8_PAGES = amanda.8

COMMON_MAN5_PAGES = amanda.conf.5

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
		    amverifyrun.8 \
		    amfetchdump.8 \
		    amcrypt.8 \
		    amaespipe.8

if WANT_RECOVER
RECOVER_MAN8_PAGES = amrecover.8
endif

if WANT_RESTORE
RESTORE_MAN8_PAGES = amrestore.8
endif

man8_MANS = $(COMMON_MAN8_PAGES)

man5_MANS = $(COMMON_MAN5_PAGES)

if WANT_AMPLOT
man8_MANS += $(AMPLOT_MAN8_PAGES)
endif

if WANT_SERVER
man8_MANS += $(SERVER_MAN8_PAGES)
endif

if WANT_RECOVER
man8_MANS += $(RECOVER_MAN8_PAGES)
endif

if WANT_RESTORE
man8_MANS += $(RESTORE_MAN8_PAGES)
endif

ALL_MAN_PAGES = $(AMPLOT_MAN8_PAGES) \
	   $(COMMON_MAN5_PAGES) \
	   $(COMMON_MAN8_PAGES) \
	   $(SERVER_MAN8_PAGES) \
	   $(RECOVER_MAN8_PAGES) \
	   $(RESTORE_MAN8_PAGES)

man_MANS = $(man5_MANS) $(man8_MANS)

SRCMANPAGEDIR  = $(srcdir)/xml-source

MAN_XML = $(ALL_MAN_PAGES:%=xml-source/%.xml)

EXTRA_XML = xslt/expand-sambadoc.xsl \
            xslt/man.xsl \
            xslt/settings.xsl \
	    entities/global.entities \
	    entities/xinclude.dtd
	    

EXTRA_DIST = $(ALL_MAN_PAGES) $(MAN_XML) $(EXTRA_XML)

if HAVE_XSLTPROC

GEN_XML = $(ALL_MAN_PAGES:%=xml-source/%.proc.xml)

MOSTLYCLEANFILES = $(GEN_XML)
MAINTAINERCLEANFILES = $(ALL_MAN_PAGES)

xml-source/%.proc.xml: $(SRCMANPAGEDIR)/%.xml $(srcdir)/xslt/expand-sambadoc.xsl
	$(XSLTPROC) --path $(srcdir)/xslt/ --xinclude --stringparam latex.imagebasedir "$*/" --stringparam noreference 1 --output $@ $(srcdir)/xslt/expand-sambadoc.xsl $<

%: xml-source/%.proc.xml $(srcdir)/xslt/man.xsl
	$(XSLTPROC) --path $(srcdir)/xslt/ --output $@ man.xsl $<

endif
# ^- HAVE_XSLTPROC

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

