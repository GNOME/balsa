CFLAGS += -Wall -Wmissing-prototypes -Wpointer-arith

noinst_LIBRARIES = libinit_balsa.a

libinit_balsa_a_SOURCES = 		\
	$(BUILT_SOURCES)		\
	helper.c			\
	helper.h			\
	init_balsa.c			\
	init_balsa.h

BUILT_SOURCES =				\
	balsa-initdruid.c		\
	balsa-druid-page.c		\
	balsa-druid-page-error.c	\
	balsa-druid-page-welcome.c	\
	balsa-druid-page-user.c		\
	balsa-druid-page-directory.c	\
	balsa-druid-page-finish.c	\
	balsa-initdruid.h		\
	balsa-druid-page.h		\
	balsa-druid-page-error.h	\
	balsa-druid-page-welcome.h	\
	balsa-druid-page-user.h		\
	balsa-druid-page-directory.h	\
	balsa-druid-page-finish.h	

GOBFILES =				\
	balsa-initdruid.gob		\
	balsa-druid-page.gob		\
	balsa-druid-page-error.gob	\
	balsa-druid-page-welcome.gob	\
	balsa-druid-page-user.gob	\
	balsa-druid-page-directory.gob	\
	balsa-druid-page-finish.gob

EXTRA_DIST = $(GOBFILES)

INCLUDES=-I. -I$(top_srcdir) \
	-I$(top_builddir)/libmutt \
	-I$(top_srcdir)/libmutt \
	-I$(top_srcdir)/libbalsa \
	-I$(top_srcdir)/src \
	$(GNOME_INCLUDEDIR)

dist-hook:
	test -n "$(BUILT_SOURCES)" \
		&& cp -f $(BUILT_SOURCES) $(distdir)

@MAINT@%.c %.h: %.gob
@MAINT@	gob --no-private-header $<