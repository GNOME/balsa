noinst_LIBRARIES = libinit_balsa.a

if BUILD_WITH_ASSISTANT
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
EXTRA_DIST = 				\
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
	helper.h	
else
EXTRA_DIST = 				\
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

libinit_balsa_a_SOURCES =		\
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
	init_balsa.c			\
	init_balsa.h			\
	helper.c			\
	helper.h	
endif

INCLUDES=-I$(top_builddir) -I$(top_srcdir) \
	-I$(top_srcdir)/libbalsa \
	-I$(top_srcdir)/src \
        $(BALSA_CFLAGS)

DEFS = -DG_DISABLE_DEPRECATED	\
       -DGTK_DISABLE_DEPRECATED \
       -DGNOME_DISABLE_DEPRECATED
