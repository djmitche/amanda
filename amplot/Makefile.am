# Makefile for amplot.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

SUFFIXES =		.sh

.sh:
			cat $< > $@
			chmod a+x $@

if WANT_AMPLOT

sbin_SCRIPTS =		amplot

amplot_DATA =		amcat.awk amplot.awk amplot.g amplot.gp

endif

amplotdir =		$(libexecdir)

EXTRA_DATA =		amplot.awk		amplot.g
EXTRA_DIST =		$(EXTRA_DATA)

CLEANFILES = amplot.gp.new
DISTCLEANFILES = amplot.gp

amplot.gp:		amplot.g
			cat $(srcdir)/amplot.g > amplot.gp.new
			echo "pause -1;" >> amplot.gp.new
			rm -f amplot.gp
			mv amplot.gp.new amplot.gp

install-exec-hook:
	@list="$(sbin_SCRIPTS)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done

install-data-hook:
	@list="$(amplot_DATA)"; \
	for p in $$list; do \
		pa=$(DESTDIR)$(libexecdir)/$$p; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
        done
