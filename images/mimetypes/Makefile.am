extramimetypes_IMGS = $(wildcard gnome-mime-*.png)
extramimetypes_OLD_IMGS = $(patsubst gnome-mime-%,gnome-%,$(extramimetypes_IMGS))

EXTRA_DIST = $(extramimetypes_IMGS)

if INSTALL_GNOME_ICONS
extramimetypesdir = $(GNOME_ICON_DIR)/48x48/mimetypes
extramimetypes_DATA = $(extramimetypes_IMGS)
else
extramimetypesdir = $(datadir)/balsa/pixmaps
extramimetypes_DATA = $(extramimetypes_OLD_IMGS)

clean-local:
	rm -f $(extramimetypes_OLD_IMGS)

%.png:
	ln -s -f `echo $@ | sed -e 's/^gnome-/gnome-mime-/'` $@

endif

