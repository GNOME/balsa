#intl dir needed for tarball --disable-nls build.
SUBDIRS = po sounds images doc libbalsa libinit_balsa src
# set tar in case it is not set by automake or make

man_MANS=balsa.1

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = gnome-balsa2.png

desktopdir = $(datadir)/applications
desktop_in_files = balsa.desktop.in
desktop_DATA = balsa.desktop
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = \
	balsa.1.in \
	balsa.spec.in \
	balsa.desktop.in \
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	gnome-balsa2.png \
	HACKING			\
	docs/mh-mail-HOWTO	\
	docs/vconvert.awk	\
	docs/pine2vcard		\
	GNOME_Balsa.server GNOME_Balsa.server.in

serverdir       = $(libdir)/bonobo/servers
server_in_files = GNOME_Balsa.server
server_DATA     = $(server_in_files:.server.in=.server)

$(server_in_files): $(server_in_files).in
	sed -e "s|\@bindir\@|$(bindir)|" $< > $@


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

ACLOCAL_AMFLAGS = -I m4  
