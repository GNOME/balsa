CFLAGS += -Wall -Wmissing-prototypes -Wpointer-arith

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
	address.h		\
	mime.h			\
	misc.h			\
	send.h			\
	filter-error.c		\
	filter-file.c		\
	filter-funcs.c		\
	libbalsa.c		\
	misc.c			\
	mime.c			\
	mailbox.c		\
	mailbox_local.c		\
	mailbox_pop3.c		\
	mailbox_imap.c		\
	send.c

libbalsa_a_LIBADD = @LIBOBJS@

INCLUDES=-I. -I${top_srcdir} -I${top_builddir}/libmutt \
	$(GNOME_INCLUDEDIR)
