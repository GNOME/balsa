noinst_LIBRARIES = libbalsa.a

libbalsa_a_SOURCES = 		\
	filter-funcs.h		\
	filter-private.h	\
	filter.h		\
	mailbackend.h		\
	mailbox.h		\
	message.c		\
	message.h		\
	body.h			\
	body.c			\
	address.h		\
	address.c		\
	contact.h		\
	contact.c		\
	mime.h			\
	misc.h			\
	files.c			\
	files.h			\
	filter-error.c		\
	filter-file.c		\
	filter-funcs.c		\
	imapdir.h		\
	libbalsa.c		\
	libbalsa.h		\
	libbalsa_private.h	\
	misc.c			\
	mime.c			\
	mailbox.c		\
	mailbox_local.c		\
	mailbox_local.h		\
	mailbox_pop3.c		\
	mailbox_pop3.h		\
	mailbox_imap.c		\
	mailbox_imap.h		\
	send.c			\
	server.c		\
	server.h		\
	threads.h		


libbalsa_a_LIBADD = @LIBOBJS@

INCLUDES=-I. -I${top_srcdir} -I${top_builddir}/libmutt \
	$(GNOME_INCLUDEDIR)
