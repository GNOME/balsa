#intl dir needed for tarball --disable-nls build.
DISTCHECK_CONFIGURE_FLAGS="--disable-extra-mimeicons"
SUBDIRS = . sounds po images doc libnetclient libbalsa libinit_balsa src ui
# set tar in case it is not set by automake or make

man_MANS=balsa.1

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = gnome-balsa2.png

desktopdir = $(datadir)/applications
desktop_in_files = org.desktop.Balsa.desktop.in balsa-mailto-handler.desktop.in
desktop_DATA = org.desktop.Balsa.desktop balsa-mailto-handler.desktop

# Appdata files

appdatadir = $(datarootdir)/metainfo
appdata_in_files = org.desktop.Balsa.metainfo.xml.in
dist_appdata_DATA = org.desktop.Balsa.metainfo.xml

dbusservicedir = $(datadir)/dbus-1/services
dbusservice_in_files = org.desktop.Balsa.service.in
dbusservice_DATA = org.desktop.Balsa.service

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
	$(appdata_in_files)	\
	$(dbusservice_in_files)	\
	gnome-balsa2.png	\
	meson.build	\
	meson_options.txt	\
	po/meson.build		\
	README-CardDAV.md

EXTRA_DIST =			\
	$(balsa_extra_dist)

if BUILD_WITH_GTKSOURCEVIEW
gtksourceviewdir = $(BALSA_DATA_PREFIX)/gtksourceview-3.0
gtksourceview_DATA = balsa-mail.lang		\
	balsa-mail-style.xml
endif

DISTCLEANFILES = $(desktop_DATA) $(server_DATA) $(dist_appdata_DATA) $(dbusservice_DATA)

%.desktop:	%.desktop.in
	$(AM_V_GEN)$(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

%.xml:	%.xml.in
	$(AM_V_GEN)$(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

%.service:	%.service.in
	$(AM_V_GEN)$(SED) -e 's|@BINDIR[@]|$(bindir)|g' $< > $@

dist-hook: balsa.spec
	cp balsa.spec $(distdir)

@MAINT@RPM: balsa.spec
@MAINT@	rm -f *.rpm
@MAINT@	$(MAKE) distdir="$(PACKAGE)-$(PACKAGE_VERSION)" dist
@MAINT@	cp $(top_srcdir)/rpm-po.patch $(top_builddir)/rpm-po.patch
@MAINT@	rpm -ta "./$(PACKAGE)-$(PACKAGE_VERSION).tar.gz"
@MAINT@	rm $(top_builddir)/rpm-po.patch
@MAINT@	-test -f "/usr/src/redhat/SRPMS/$(PACKAGE)-$(PACKAGE_VERSION)-@BALSA_RELEASE@.src.rpm" \
@MAINT@		&& cp -f "/usr/src/redhat/SRPMS/$(PACKAGE)-$(PACKAGE_VERSION)-@BALSA_RELEASE@.src.rpm" .
@MAINT@	-for ping in /usr/src/redhat/RPMS/* ; do \
@MAINT@		if test -d $$ping ; then \
@MAINT@			arch=`echo $$ping |sed -e 's,/.*/\([^/][^/]*\),\1,'` ; \
@MAINT@			f="$$ping/$(PACKAGE)-$(PACKAGE_VERSION)-@BALSA_RELEASE@.$$arch.rpm" ; \
@MAINT@			test -f $$f && cp -f $$f . ; \
@MAINT@		fi ; \
@MAINT@	done

@MAINT@snapshot:
@MAINT@	$(MAKE) distdir=$(PACKAGE)-`date +"%y%m%d"` dist

@MAINT@balsa-dcheck:
@MAINT@	$(MAKE) BALSA_DISTCHECK_HACK=yes distcheck

## to automatically rebuild aclocal.m4 if any of the macros in
## `macros/' change
xzdist: distdir
	@test -n "$(AMTAR)" || { echo "AMTAR undefined. Run make bzdist AMTAR=gtar"; false; }
	-chmod -R a+r $(distdir)
	$(AMTAR) choJf $(distdir).tar.xz $(distdir)
	-rm -rf $(distdir)
