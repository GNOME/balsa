noinst_LIBRARIES = libinit_balsa.a

libinit_balsa_a_SOURCES = 		\
	$(BUILT_SOURCES)		\
	helper.c			\
	helper.h			\
	init_balsa.c			\
	init_balsa.h

BUILT_SOURCES =				\
	balsa-initdruid.c		\
	balsa-druid-page-welcome.c	\
	balsa-druid-page-user.c		\
	balsa-druid-page-directory.c	\
	balsa-druid-page-finish.c	\
	balsa-initdruid.h		\
	balsa-druid-page-welcome.h	\
	balsa-druid-page-user.h		\
	balsa-druid-page-directory.h	\
	balsa-druid-page-finish.h	

INCLUDES=-I. -I$(top_srcdir) \
	-I$(top_srcdir)/libmutt \
	-I$(top_srcdir)/libbalsa \
	-I$(top_srcdir)/src \
        $(BALSA_CFLAGS)
