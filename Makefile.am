SUBDIRS = macros po intl sounds libmutt libbalsa src

balsa_BITMAPS = balsa_logo.png balsa_icon.png

balsa_MANS=balsa.1

EXTRA_DIST =	balsa.1 \
		configure \
		$(balsa_BITMAPS)

balsabitmapsdir = $(datadir)/pixmaps
balsabitmaps_DATA = $(balsa_BITMAPS)

## to automatically rebuild aclocal.m4 if any of the macros in
## `macros/' change
@MAINT@include macros/macros.dep
@MAINT@macros/macros.dep: macros/Makefile.am
@MAINT@	cd macros && $(MAKE) macros.dep
