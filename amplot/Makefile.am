# Makefile for amplot.

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

SUFFIXES =		.sh

.sh:
			cat $< > $@
			chmod a+x $@

sbin_SCRIPTS =		@BUILD_AMPLOT_SBIN@

EXTRA_SCRIPTS =		amplot

amplotdir =		$(libexecdir)
amplot_DATA =		@BUILD_AMPLOT_LIBEXEC@

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
		pa=$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done

install-data-hook:
	@list="$(amplot_DATA)"; \
	for p in $$list; do \
		pa=$(libexecdir)/$$p; \
		echo chown $(BINARY_OWNER) $$pa; \
		chown $(BINARY_OWNER) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
        done
