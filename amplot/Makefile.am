# Makefile for amplot.

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

amplot.gp:		amplot.g
			rm -f amplot.gp
			cat $(srcdir)/amplot.g > amplot.gp
			echo "pause -1;" >> amplot.gp

install-exec-hook:
	@list="$(sbin_SCRIPTS)"; for p in $$list; do \
		pa=$(sbindir)/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done

install-data-hook:
	@list="$(amplot_DATA)"; for p in $$list; do \
		pa=$(libexecdir)/$$p; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
        done
