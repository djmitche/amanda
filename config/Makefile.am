## Process this file with automake to produce Makefile.in

EXTRA_DIST =	acconfig.h	\
		acinclude.m4i	\
		config.guess	\
		config.sub	\
		install-sh	\
		libtool.m4i	\
		ltconfig	\
		ltmain.sh	\
		missing		\
		mkinstalldirs

$(top_srcdir)/acconfig.h: $(srcdir)/acconfig.h
	@-rm -f $@
	cd $(top_srcdir) && $(LN_S) config/acconfig.h acconfig.h
