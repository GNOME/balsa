SUBDIRS = macros po intl sounds images libmutt libbalsa src

balsa_MANS=balsa.1

EXTRA_DIST =	balsa.1 \
		configure \

## to automatically rebuild aclocal.m4 if any of the macros in
## `macros/' change
@MAINT@include macros/macros.dep
@MAINT@macros/macros.dep: macros/Makefile.am
@MAINT@	cd macros && $(MAKE) macros.dep
