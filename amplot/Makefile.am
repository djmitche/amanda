# Makefile for amplot.

SUFFIXES =		.sh

.sh:
			cat $< > $@
			chmod a+x $@

bin_SCRIPTS =		@BUILD_AMPLOT_BIN@

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
	@list="$(bin_SCRIPTS)"; for p in $$list; do \
		echo chown $(CLIENT_LOGIN) $(bindir)/`echo $$p|sed '$(transform)'`; \
		chown $(CLIENT_LOGIN) $(bindir)/`echo $$p|sed '$(transform)'`; \
		echo chgrp $(SETUID_GROUP) $(bindir)/`echo $$p|sed '$(transform)'`; \
		chgrp $(SETUID_GROUP) $(bindir)/`echo $$p|sed '$(transform)'`; \
	done;

install-data-hook:
	@list="$(amplot_DATA)"; for p in $$list; do \
		echo chown $(CLIENT_LOGIN) $(libexecdir)/$$p; \
		chown $(CLIENT_LOGIN) $(libexecdir)/$$p; \
		echo chgrp $(SETUID_GROUP) $(libexecdir)/$$p; \
		chgrp $(SETUID_GROUP) $(libexecdir)/$$p; \
        done;
