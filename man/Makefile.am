# Makefile for amanda man-pages

transform =	s,x,x,;

AMPLOT_MAN_PAGES = amplot.8

COMMON_MAN_PAGES = amanda.8 amanda.conf.5

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

if WANT_RECOVER
RECOVER_MAN_PAGES = amrecover.8
endif

if WANT_RESTORE
RESTORE_MAN_PAGES = amrestore.8
endif

man_MANS = $(COMMON_MAN_PAGES)

if WANT_AMPLOT
man_MANS += $(AMPLOT_MAN_PAGES)
endif

if WANT_SERVER
man_MANS += $(SERVER_MAN_PAGES)
endif

if WANT_RECOVER
man_MANS += $(RECOVER_MAN_PAGES)
endif

if WANT_RESTORE
man_MANS += $(RESTORE_MAN_PAGES)
endif

ALL_MAN_PAGES = $(AMPLOT_MAN_PAGES) \
	   $(COMMON_MAN_PAGES) \
	   $(SERVER_MAN_PAGES) \
	   $(RECOVER_MAN_PAGES) \
	   $(RESTORE_MAN_PAGES)


# Very important to have the / here.
MANPAGEDIR = xml-source/

MAN_XML = $(ALL_MAN_PAGES:%=$(MANPAGEDIR)%.xml)

EXTRA_XML = xslt/expand-sambadoc.xsl \
            xslt/global.entities \
            xslt/man.xsl \
            xslt/settings.xsl

EXTRA_DIST = $(ALL_MAN_PAGES) $(MAN_XML) $(EXTRA_XML)

if HAVE_XSLTPROC

GEN_XML = $(ALL_MAN_PAGES:%=$(MANPAGEDIR)/%.proc.xml)

MOSTLYCLEANFILES = $(GEN_XML)
MAINTAINERCLEANFILES = $(ALL_MAN_PAGES)

$(MANPAGEDIR)/%.proc.xml: $(MANPAGEDIR)/%.xml xslt/expand-sambadoc.xsl
	$(XSLTPROC) --path xslt/ --xinclude --stringparam latex.imagebasedir "$*/" --stringparam noreference 1 --output $@ expand-sambadoc.xsl $<

$(man_MANS): %: $(MANPAGEDIR)/%.proc.xml xslt/man.xsl
	$(XSLTPROC) --path xslt/ --output $@ man.xsl $<

endif
# ^- HAVE_XSLTPROC

install-data-hook:
	@list="$(man_MANS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(mandir)/man8/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done

