SUBDIRS = imap

noinst_LIBRARIES = libbalsa.a


libbalsa_a_SOURCES = 		\
	abook-completion.c	\
	abook-completion.h	\
	address-book-extern.c	\
	address-book-extern.h	\
	address-book-gpe.c	\
	address-book-gpe.h	\
	address-book-ldap.c 	\
	address-book-ldap.h	\
	address-book-ldif.c	\
	address-book-ldif.h	\
	address-book-osmo.c	\
	address-book-osmo.h	\
	address-book-text.c	\
	address-book-text.h	\
	address-book-vcard.c	\
	address-book-vcard.h	\
	address-book-carddav.c	\
	address-book-carddav.h	\
	address-book.c		\
	address-book.h		\
	address-view.c		\
	address-view.h		\
	address.c		\
	address.h		\
	application-helpers.c   \
	application-helpers.h   \
	autocrypt.h				\
	autocrypt.c				\
	body.c			\
	body.h			\
	completion.c            \
	completion.h            \
	dkim.c			\
	dkim.h			\
	files.c			\
	files.h			\
	filter-error.c		\
	filter-file.c		\
	filter-file.h		\
	filter-funcs.c		\
	filter-funcs.h		\
	filter-private.h	\
	filter.c		\
	filter.h		\
	folder-scanners.c	\
	folder-scanners.h	\
	gmime-application-pkcs7.h	\
	gmime-application-pkcs7.c	\
	gmime-filter-header.c      \
	gmime-filter-header.h      \
	gmime-gpgme-signature.h	\
	gmime-gpgme-signature.c	\
	gmime-multipart-crypt.h	\
	gmime-multipart-crypt.c	\
	gmime-part-rfc2440.h	\
	gmime-part-rfc2440.c	\
	html.c                  \
	html.h                  \
	html-pref-db.c		\
	html-pref-db.h		\
	identity.c		\
	identity.h		\
	imap-server.c		\
	imap-server.h		\
	information.c		\
	information.h		\
	libbalsa-conf.c		\
	libbalsa-conf.h		\
	libbalsa-gpgme.h		\
	libbalsa-gpgme.c		\
	libbalsa-gpgme-cb.h		\
	libbalsa-gpgme-cb.c		\
	libbalsa-gpgme-keys.h	\
	libbalsa-gpgme-keys.c	\
	libbalsa-gpgme-widgets.h\
	libbalsa-gpgme-widgets.c\
	libbalsa-progress.c	\
	libbalsa-progress.h	\
	missing.h		\
	missing_time.c		\
	libbalsa.c		\
	libbalsa.h		\
	libbalsa_private.h	\
	libbalsa-vfs.c		\
	libbalsa-vfs.h		\
	mailbackend.h		\
	mailbox-filter.c	\
	mailbox-filter.h	\
	mailbox.c		\
	mailbox.h		\
	mailbox_imap.c		\
	mailbox_imap.h		\
	mailbox_local.c		\
	mailbox_local.h		\
	mailbox_maildir.c	\
	mailbox_maildir.h	\
	mailbox_mbox.c		\
	mailbox_mbox.h		\
	mailbox_mh.c		\
	mailbox_mh.h		\
	mailbox_pop3.c		\
	mailbox_pop3.h		\
	mailbox_remote.c	\
	mailbox_remote.h	\
	message.c		\
	message.h		\
	mime.c			\
	mime.h			\
	mime-stream-shared.c    \
	mime-stream-shared.h    \
	misc.c			\
	misc.h			\
	rfc2445.c		\
	rfc2445.h		\
	rfc3156.c		\
	rfc3156.h		\
	rfc6350.c		\
	rfc6350.h		\
	send.c			\
	send.h			\
	server.c		\
	server.h		\
	server-config.c		\
	server-config.h		\
	smtp-server.c		\
	smtp-server.h		\
	system-tray.c		\
	system-tray.h		\
	source-viewer.c		\
	geometry-manager.c	\
	geometry-manager.h	\
	x509-cert-widget.c	\
	x509-cert-widget.h	\
	libbalsa-webdav.c	\
	libbalsa-webdav.h	\
	libbalsa-carddav.c	\
	libbalsa-carddav.h

pkglib_LTLIBRARIES = libhtmlfilter.la
libhtmlfilter_la_SOURCES = html-filter.c
libhtmlfilter_la_CFLAGS = $(BALSA_CFLAGS)
libhtmlfilter_la_LIBADD = $(BALSA_LIBS)
libhtmlfilter_la_LDFLAGS = -module -avoid-version -no-undefined

EXTRA_DIST = 				\
	meson.build	\
	padlock-keyhole.xpm

AM_CPPFLAGS = -I${top_builddir} -I${top_srcdir} -I${top_srcdir}/libbalsa \
	-I${top_srcdir}/libnetclient \
	-I${top_srcdir}/libbalsa/imap \
	$(BALSA_DEFS)

AM_CFLAGS = $(BALSA_CFLAGS)

html.o:	AM_CPPFLAGS += -DBALSA_WEB_EXT_DEVEL=\"${abs_builddir}/.libs\"
