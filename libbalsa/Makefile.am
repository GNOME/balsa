noinst_LIBRARIES = libbalsa.a

libbalsa_a_SOURCES = 		\
	abook-completion.c	\
	abook-completion.h	\
	address-book-extern.c	\
	address-book-extern.h	\
	address-book-ldap.c 	\
	address-book-ldap.h	\
	address-book-ldif.c	\
	address-book-ldif.h	\
	address-book-vcard.c	\
	address-book-vcard.h	\
	address-book.c		\
	address-book.h		\
	address-entry.c		\
	address-entry.h		\
	address.c		\
	address.h		\
	body.c			\
	body.h			\
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
	html.c			\
	html.h			\
	identity.c		\
	identity.h		\
	information.c		\
	information.h		\
	libbalsa.c		\
	libbalsa.h		\
	libbalsa-marshal.c	\
	libbalsa-marshal.h	\
	libbalsa_private.h	\
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
	misc.c			\
	misc.h			\
	notify.c		\
	notify.h		\
	pop3.c			\
	pop3.h			\
	send.c			\
	send.h			\
	server.c		\
	server.h		\
	source-viewer.c		\
	rfc3156.c		\
	rfc3156.h

# we include padlock-keyhole because make dist may not figure it out
# automatically.
EXTRA_DIST = \
	libbalsa-marshal.list \
	padlock-keyhole.xpm

INCLUDES= -I${top_builddir} -I${top_srcdir} -I${top_srcdir}/libbalsa \
	-I${top_srcdir}/libmutt -I${top_builddir}/libmutt \
	$(BALSA_CFLAGS)

DEFS = -DLIBMUTT                \
       -DG_DISABLE_DEPRECATED   \
       -DGTK_DISABLE_DEPRECATED \
       -DGNOME_DISABLE_DEPRECATED

#Signal marshallers
marshal_prefix = libbalsa
libbalsa-marshal.h: libbalsa-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=$(marshal_prefix) libbalsa-marshal.list --header > libbalsa-marshal.tmp \
	&& mv libbalsa-marshal.tmp libbalsa-marshal.h ) \
	|| ( rm -f libbalsa-marshal.tmp && exit 1 )
libbalsa-marshal.c: libbalsa-marshal.h
	( echo "#include \"libbalsa-marshal.h\"" > libbalsa-marshal.tmp \
	&& @GLIB_GENMARSHAL@ --prefix=$(marshal_prefix) libbalsa-marshal.list --body >> libbalsa-marshal.tmp \
	&& mv libbalsa-marshal.tmp libbalsa-marshal.c ) \
	|| ( rm -f libbalsa-marshal.tmp && exit 1 )

$(libbalsa_a_OBJECTS): libbalsa-marshal.h
