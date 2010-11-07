SUBDIRS = imap

noinst_LIBRARIES = libbalsa.a


if BUILD_WITH_GPGME
libbalsa_gpgme_extra = 		\
	gmime-gpgme-context.h	\
	gmime-gpgme-context.c	\
	gmime-part-rfc2440.h	\
	gmime-part-rfc2440.c	\
	gmime-gpgme-signature.h	\
	gmime-gpgme-signature.c
libbalsa_gpgme_extra_dist =
else
libbalsa_gpgme_extra =
libbalsa_gpgme_extra_dist =	\
	gmime-gpgme-context.h	\
	gmime-gpgme-context.c	\
	gmime-part-rfc2440.h	\
	gmime-part-rfc2440.c	\
	gmime-gpgme-signature.h	\
	gmime-gpgme-signature.c
endif

if BUILD_WITH_SMIME
libbalsa_smime_extra =			\
	gmime-application-pkcs7.h	\
	gmime-application-pkcs7.c
libbalsa_smime_extra_dist =
else
libbalsa_smime_extra =
libbalsa_smime_extra_dist =		\
	gmime-application-pkcs7.h	\
	gmime-application-pkcs7.c
endif

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
	address-book-rubrica.c	\
	address-book-rubrica.h	\
	address-book-text.c	\
	address-book-text.h	\
	address-book-vcard.c	\
	address-book-vcard.h	\
	address-book.c		\
	address-book.h		\
	address-view.c		\
	address-view.h		\
	address.c		\
	address.h		\
	body.c			\
	body.h			\
	cell-renderer-button.c  \
	cell-renderer-button.h  \
	completion.c            \
	completion.h            \
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
	gmime-stream-gio.c      \
	gmime-stream-gio.h      \
	html.c                  \
	html.h                  \
	identity.c		\
	identity.h		\
	imap-server.c		\
	imap-server.h		\
	information.c		\
	information.h		\
	libbalsa-conf.c		\
	libbalsa-conf.h		\
	libbalsa-marshal.c	\
	libbalsa-marshal.h	\
	macosx-helpers.c	\
	macosx-helpers.h	\
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
	send.c			\
	send.h			\
	server.c		\
	server.h		\
	smtp-server.c		\
	smtp-server.h		\
	source-viewer.c		\
	url.c			\
	url.h			\
	${libbalsa_gpgme_extra}	\
	${libbalsa_smime_extra}


EXTRA_DIST = 				\
	libbalsa-marshal.list		\
	padlock-keyhole.xpm		\
	${libbalsa_gpgme_extra_dist}	\
	${libbalsa_smime_extra_dist}

AM_CPPFLAGS = -I${top_builddir} -I${top_srcdir} -I${top_srcdir}/libbalsa \
	-I${top_srcdir}/libbalsa/imap \
	$(BALSA_DEFS)

AM_CFLAGS = $(BALSA_CFLAGS)

#Signal marshallers
marshal_prefix = libbalsa
libbalsa-marshal.h: libbalsa-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=$(marshal_prefix) $(srcdir)/libbalsa-marshal.list --header > libbalsa-marshal.tmp \
	&& mv libbalsa-marshal.tmp libbalsa-marshal.h ) \
	|| ( rm -f libbalsa-marshal.tmp && exit 1 )
libbalsa-marshal.c: libbalsa-marshal.h
	( echo "#include \"libbalsa-marshal.h\"" > libbalsa-marshal.tmp \
	&& @GLIB_GENMARSHAL@ --prefix=$(marshal_prefix) $(srcdir)/libbalsa-marshal.list --body >> libbalsa-marshal.tmp \
	&& mv libbalsa-marshal.tmp libbalsa-marshal.c ) \
	|| ( rm -f libbalsa-marshal.tmp && exit 1 )

$(libbalsa_a_OBJECTS): libbalsa-marshal.h
