#intl dir needed for tarball --disable-nls build.
DISTCHECK_CONFIGURE_FLAGS="--disable-extra-mimeicons"
SUBDIRS = po sounds images doc libbalsa libinit_balsa src
# set tar in case it is not set by automake or make

man_MANS=balsa.1

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = gnome-balsa2.png

desktopdir = $(datadir)/applications
desktop_in_files = balsa.desktop.in balsa-mailto-handler.desktop.in
desktop_DATA = balsa.desktop balsa-mailto-handler.desktop
@INTLTOOL_DESKTOP_RULE@

balsa_extra_dist =		\
	HACKING			\
	balsa-mail-style.xml	\
	balsa-mail.lang		\
	balsa.1.in		\
	balsa.spec.in		\
	bootstrap.sh		\
	docs/mh-mail-HOWTO	\
	docs/pine2vcard		\
	docs/vconvert.awk	\
	$(desktop_in_files)	\
	gnome-balsa2.png	\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	mkinstalldirs

if HAVE_GNOME_DOC_UTILS
balsa_g_d_u_extra_dist = gnome-doc-utils.make
endif

EXTRA_DIST =			\
    $(balsa_extra_dist)		\
    $(balsa_g_d_u_extra_dist)

if BUILD_WITH_GTKSOURCEVIEW
gtksourceviewdir = $(BALSA_DATA_PREFIX)/gtksourceview-2.0
gtksourceview_DATA = balsa-mail.lang		\
	balsa-mail-style.xml
endif

DISTCLEANFILES = $(desktop_DATA) $(server_DATA) \
	intltool-extract intltool-merge intltool-update	\
	gnome-doc-utils.make


dist-hook: balsa.spec
	cp balsa.spec $(distdir)

@MAINT@RPM: balsa.spec
@MAINT@	rm -f *.rpm
@MAINT@	$(MAKE) distdir="$(PACKAGE)-@BALSA_VERSION@" dist
@MAINT@	cp $(top_srcdir)/rpm-po.patch $(top_builddir)/rpm-po.patch
@MAINT@	rpm -ta "./$(PACKAGE)-@BALSA_VERSION@.tar.gz"
@MAINT@	rm $(top_builddir)/rpm-po.patch
@MAINT@	-test -f "/usr/src/redhat/SRPMS/$(PACKAGE)-@VERSION@-@BALSA_RELEASE@.src.rpm" \
@MAINT@		&& cp -f "/usr/src/redhat/SRPMS/$(PACKAGE)-@VERSION@-@BALSA_RELEASE@.src.rpm" .
@MAINT@	-for ping in /usr/src/redhat/RPMS/* ; do \
@MAINT@		if test -d $$ping ; then \
@MAINT@			arch=`echo $$ping |sed -e 's,/.*/\([^/][^/]*\),\1,'` ; \
@MAINT@			f="$$ping/$(PACKAGE)-@VERSION@-@BALSA_RELEASE@.$$arch.rpm" ; \
@MAINT@			test -f $$f && cp -f $$f . ; \
@MAINT@		fi ; \
@MAINT@	done

@MAINT@snapshot:
@MAINT@	$(MAKE) distdir=$(PACKAGE)-`date +"%y%m%d"` dist

@MAINT@balsa-dcheck:
@MAINT@	$(MAKE) BALSA_DISTCHECK_HACK=yes distcheck

## to automatically rebuild aclocal.m4 if any of the macros in
## `macros/' change
bzdist: distdir
	@test -n "$(AMTAR)" || { echo "AMTAR undefined. Run make bzdist AMTAR=gtar"; false; }
	-chmod -R a+r $(distdir)
	$(AMTAR) chojf $(distdir).tar.bz2 $(distdir)
	-rm -rf $(distdir)

# macros are not used any more by current configure.in, see also
# post by Ildar Mulyukov to balsa-list, 2006.06.27
# ACLOCAL_AMFLAGS = -I macros
