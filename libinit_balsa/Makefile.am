noinst_LIBRARIES = libinit_balsa.a

libinit_balsa_a_SOURCES = 		\
	balsa-druid-page-defclient.c	\
	balsa-druid-page-defclient.h	\
	balsa-druid-page-directory.c	\
	balsa-druid-page-directory.h	\
	balsa-druid-page-finish.c	\
	balsa-druid-page-finish.h	\
	balsa-druid-page-user.c		\
	balsa-druid-page-user.h		\
	balsa-druid-page-welcome.c	\
	balsa-druid-page-welcome.h	\
	balsa-initdruid.c		\
	balsa-initdruid.h		\
	helper.c			\
	helper.h			\
	init_balsa.c			\
	init_balsa.h

INCLUDES=-I$(top_builddir) -I$(top_srcdir) \
	-I$(top_builddir)/libmutt -I$(top_srcdir)/libmutt \
	-I$(top_srcdir)/libbalsa \
	-I$(top_srcdir)/src \
        $(BALSA_CFLAGS)

DEFS = -DG_DISABLE_DEPRECATED   \
       -DGTK_DISABLE_DEPRECATED \
       -DGNOME_DISABLE_DEPRECATED
