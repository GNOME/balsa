SUBDIRS = macros po intl sounds images help libmutt libbalsa libinit_balsa idl src

man_MANS=balsa.1

EXTRA_DIST = \
	balsa.1.in \
	balsa.spec.in

dist-hook: balsa.spec
	cp balsa.spec $(distdir)

## to automatically rebuild aclocal.m4 if any of the macros in
## `macros/' change
@MAINT@include macros/macros.dep
@MAINT@macros/macros.dep: macros/Makefile.am
@MAINT@	cd macros && $(MAKE) macros.dep
