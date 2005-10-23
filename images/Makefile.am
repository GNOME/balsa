SUBDIRS = mimetypes 16x16 24x24

# balsa_logo.png used by About menu (small one).
# balsa-logo.png used by the init-druid
balsa_IMGS = \
	attachment.png		\
	balsa-logo.png		\
	balsa-top.png		\
	balsa-watermark.png	\
	balsa_icon.png		\
	balsa_logo.png

EXTRA_DIST =	$(balsa_IMGS)

balsabitmapsdir = $(BALSA_DATA_PREFIX)/pixmaps

balsabitmaps_DATA = $(balsa_IMGS)

# the call to $(GTK_UPDATE_ICON_CACHE) updates the icon cache for the
# icons installed in $(SUBDIRS) (Gtk+-2.6) but this breaks lots of
# stuff: user builds, package creations, so we disable it for now.
#install-data-hook:
#	if test -x "$(GTK_UPDATE_ICON_CACHE)" ; then                    \
#		$(GTK_UPDATE_ICON_CACHE) $(DESTDIR)$(BALSA_DATA_PREFIX)/hicolor ; \
#		$(GTK_UPDATE_ICON_CACHE) $(DESTDIR)$(HICOLOR_ICON_DIR) ;          \
#	fi
