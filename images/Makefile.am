SUBDIRS = mimetypes 16x16 24x24

# balsa_logo.png used by About menu (small one).
# balsa-logo.png used by the init-druid
balsa_IMGS = \
	attachment.png		\
	balsa-top.png		\
	balsa_icon.png		\
	balsa_attention.png	\
	balsa_logo.png

EXTRA_DIST =	$(balsa_IMGS)	\
	meson.build

balsabitmapsdir = $(BALSA_DATA_PREFIX)/pixmaps

balsabitmaps_DATA = $(balsa_IMGS)

install-data-hook:
	if test -x "$(GTK_UPDATE_ICON_CACHE)" ; then	\
	    $(GTK_UPDATE_ICON_CACHE)			\
		--ignore-theme-index			\
		$(DESTDIR)$(BALSA_DATA_PREFIX) ;	\
	fi

uninstall-hook:
	rm $(DESTDIR)$(BALSA_DATA_PREFIX)/icon-theme.cache || true
