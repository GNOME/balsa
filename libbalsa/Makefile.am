noinst_LIBRARIES = libbalsa.a

filter_src = \
	filter-error.c		\
	filter-file.c		\
	filter-file.h		\
	filter-funcs.c		\
	filter-funcs.h		\
	filter-private.h	\
	filter.c		\
	filter.h		\
	mailbox-filter.c	\
	mailbox-filter.h

if BALSA_SHOW_ALL
extra_src  = $(filter_src)
else
extra_src  = 
endif

EXTRA_DIST = $(filter_src)

libbalsa_a_SOURCES = 		\
	abook-completion.c	\
	abook-completion.h	\
	address-book-ldap.c 	\
	address-book-ldap.h	\
	address-book-vcard.c	\
	address-book-vcard.h	\
	address-book-ldif.h	\
	address-book-ldif.c	\
	address-book-extern.h	\
	address-book-extern.c	\
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
	folder-scanners.c	\
	folder-scanners.h	\
	identity.c		\
	identity.h		\
	information.c		\
	information.h		\
	libbalsa.c		\
	libbalsa.h		\
	libbalsa_private.h	\
	mailbackend.h		\
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
	threads.h		\
	$(extra_src)


INCLUDES=-I. -I${top_srcdir} -I${top_builddir}/libmutt \
	$(PCRE_CFLAGS)

DEFS = -DLIBMUTT
