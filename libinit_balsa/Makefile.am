noinst_LIBRARIES = libinit_balsa.a

libinit_balsa_a_SOURCES = 		\
	assistant_page_defclient.c	\
	assistant_page_defclient.h	\
	assistant_page_directory.c	\
	assistant_page_directory.h	\
	assistant_page_finish.c	\
	assistant_page_finish.h	\
	assistant_page_user.c		\
	assistant_page_user.h		\
	assistant_page_welcome.c	\
	assistant_page_welcome.h	\
	assistant_init.c		\
	assistant_init.h		\
	assistant_helper.c		\
	assistant_helper.h

AM_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) \
	-I$(top_srcdir)/libbalsa \
	-I${top_srcdir}/libnetclient \
	-I$(top_srcdir)/src \
        $(BALSA_CFLAGS)

AM_CFLAGS = $(BALSA_DEFS)
