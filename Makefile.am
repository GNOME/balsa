SUBDIRS = macros po intl sounds images help libmutt libbalsa libinit_balsa idl src

man_MANS=balsa.1

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = gnome-balsa2.png

desktopdir = $(gnomedatadir)/gnome/apps/Internet
desktop_DATA = balsa.desktop

EXTRA_DIST = \
	balsa.1.in \
	balsa.spec.in \
	rpm-po.patch  \
	balsa.desktop \
	gnome-balsa2.png

dist-hook: balsa.spec
	cp balsa.spec $(distdir)

@MAINT@RPM: balsa.spec
@MAINT@	$(MAKE) distdir="$(PACKAGE)-@BALSA_SHORT_VERSION@" dist
@MAINT@	rpm -ta "./$(PACKAGE)-@BALSA_SHORT_VERSION@.tar.gz"

@MAINT@snapshot:
@MAINT@	$(MAKE) distdir=$(PACKAGE)-`date +"%y%m%d"` dist

@MAINT@balsa-dcheck:
@MAINT@	$(MAKE) BALSA_DISTCHECK_HACK=yes distcheck

## to automatically rebuild aclocal.m4 if any of the macros in
## `macros/' change
@MAINT@include macros/macros.dep
@MAINT@macros/macros.dep: macros/Makefile.am
@MAINT@	cd macros && $(MAKE) macros.dep
