CFLAGS += -Wall -Wmissing-prototypes -Wpointer-arith

noinst_LIBRARIES = libbalsa.a

libbalsa_a_SOURCES = \
	libbalsa.c	\
	misc.c		\
	mime.c		\
	mailbox.c	\
	send.c

libbalsa_a_LIBADD = @LIBOBJS@

INCLUDES=-I. -I${top_srcdir}/src -I${top_srcdir}/libmutt \
	$(GNOME_INCLUDEDIR)
