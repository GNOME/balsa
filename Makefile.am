SUBDIRS = macros po intl sounds images help libmutt libbalsa libinit_balsa idl src @website_subdir@
EXTRA_SUBDIRS = macros po intl sounds images help libmutt libbalsa libinit_balsa idl src website

man_MANS=balsa.1

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = gnome-balsa2.png

desktopdir = $(gnomedatadir)/gnome/apps/Internet
desktop_DATA = balsa.desktop

EXTRA_DIST = \
	balsa.1.in \
	balsa.spec.in \
	balsa.desktop \
	gnome-balsa2.png

dist-hook: balsa.spec
	cp balsa.spec $(distdir)

## to automatically rebuild aclocal.m4 if any of the macros in
## `macros/' change
@MAINT@include macros/macros.dep
@MAINT@macros/macros.dep: macros/Makefile.am
@MAINT@	cd macros && $(MAKE) macros.dep
