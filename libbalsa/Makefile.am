CFLAGS += -Wall -Wmissing-prototypes -Wpointer-arith

noinst_LIBRARIES = libbalsa.a

libbalsa_a_SOURCES = 		\
	filter-funcs.h		\
	filter-private.h	\
	filter.h		\
	mailbackend.h		\
	mailbox.h		\
	mime.h			\
	misc.h			\
	send.h			\
	filter-edit-callbacks.c	\
	libbalsa.c		\
	misc.c			\
	mime.c			\
	mailbox.c		\
	send.c

libbalsa_a_LIBADD = @LIBOBJS@

INCLUDES=-I. -I${top_srcdir}/src -I${top_srcdir} \
	$(GNOME_INCLUDEDIR)
