# Makefile for amanda man-pages

ACLOCAL_M4 = $(top_srcdir)/config/aclocal.m4

transform =	s,x,x,;

man_MANS =	amadmin.8	amanda.8	amcheck.8	\
		amcleanup.8	amdump.8	amflush.8	\
		amlabel.8	amplot.8	amrecover.8	\
		amrestore.8	amrmtape.8	amtape.8	\
		amtoc.8

EXTRA_DIST = amplot.8 amrecover.8 amrestore.8 amrmtape.8 amtape.8 amtoc.8

install-data-hook:
	@list="$(man_MANS)"; echo $$list; for p in $$list; do \
		pa=$(mandir)/man8/`echo $$p|sed '$(transform)'`; \
		echo chown $(CLIENT_LOGIN) $$pa; \
		chown $(CLIENT_LOGIN) $$pa; \
		echo chgrp $(SETUID_GROUP) $$pa; \
		chgrp $(SETUID_GROUP) $$pa; \
	done
