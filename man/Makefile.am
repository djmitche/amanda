transform =	s,x,x,;

man_MANS =	amadmin.8	amanda.8	amcheck.8	\
		amcleanup.8	amdump.8	amflush.8	\
		amlabel.8	amplot.8	amrecover.8	\
		amrestore.8	amrmtape.8	amtape.8	\
		amtoc.8

install-data-hook:
	@list="$(man_MANS)"; echo $$list; for p in $$list; do \
		echo chown $(CLIENT_LOGIN) $(mandir)/man8/`echo $$p|sed '$(transform)'`; \
		chown $(CLIENT_LOGIN) $(mandir)/man8/`echo $$p|sed '$(transform)'`; \
		echo chgrp $(SETUID_GROUP) $(mandir)/man8/`echo $$p|sed '$(transform)'`; \
		chgrp $(SETUID_GROUP) $(mandir)/man8/`echo $$p|sed '$(transform)'`; \
	done
