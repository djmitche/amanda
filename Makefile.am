## Process this file with automake to produce Makefile.in

SUBDIRS = common-src client-src recover-src \
	  tape-src   server-src restore-src \
	  amplot changer-src man example

EXTRA_DIST = CHANGES COPYRIGHT COPYRIGHT-GNU

dist-hook:
	mkdir $(distdir)/docs
	cp -p docs/* $(distdir)/docs
	mkdir $(distdir)/patches
	cp -p patches/* $(distdir)/patches
