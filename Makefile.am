SUBDIRS = macros po intl libmutt libbalsa src

EXTRA_DIST = balsa.1 configure balsa.xpm

man_MANS=balsa.1

## to automatically rebuild aclocal.m4 if any of the macros in
## `macros/' change
@MAINT@include macros/macros.dep
@MAINT@macros/macros.dep: macros/Makefile.am
@MAINT@	cd macros && $(MAKE) macros.dep
