extra_appicons_IMGS = \
	balsa-crypt-check.png	\
	balsa-encrypted.png	\
	balsa-encrypt.png	\
	balsa-mark-all.png	\
	balsa-marked-new.png	\
	balsa-next-flagged.png	\
	balsa-next-part.png	\
	balsa-next.png	\
	balsa-next-unread.png	\
	balsa-postpone.png	\
	balsa-preview.png	\
	balsa-previous-part.png	\
	balsa-previous.png	\
	balsa-reply-all.png	\
	balsa-signature-bad.png	\
	balsa-signature-good.png	\
	balsa-signature-notrust.png	\
	balsa-signature-unknown.png	\
	balsa-sign.png	\
	balsa-trash-empty.png


EXTRA_DIST = $(extra_appicons_IMGS)

extra_appiconsdir = $(BALSA_DATA_PREFIX)/hicolor/24x24/apps
extra_appicons_DATA = $(extra_appicons_IMGS)


# Note: this link must be created due to a bug in gtk which searches the
# folder 22x22 instead of 24x24 for 24 pixel wide icons.
# (see http://bugzilla.gnome.org/show_bug.cgi?id=166374)
install-data-hook:
	cd $(DESTDIR)$(BALSA_DATA_PREFIX)/hicolor; \
	if ! test -e 22x22; then     \
		$(LN_S) 24x24 22x22 ;  \
	fi
