CFLAGS += -Wall -Wmissing-prototypes -Wpointer-arith

bin_PROGRAMS = balsa

GOB_OUTPUTS = \
	balsa-mblist.c \
	balsa-mblist.h

GOBFILES = \
	balsa-mblist.gob

balsa_SOURCES =	\
	address-book.c		\
	address-book.h		\
	balsa-app.c		\
	balsa-app.h		\
	balsa-icons.c		\
	balsa-icons.h		\
	balsa-index.c		\
	balsa-index.h		\
	balsa-index-page.c	\
	balsa-index-page.h	\
	balsa-message.c		\
	balsa-message.h		\
	filter-edit.h		\
	filter-edit-callbacks.c	\
	filter-edit-dialog.c	\
	local-mailbox.c		\
	local-mailbox.h		\
	mailbox-conf.c		\
	mailbox-conf.h		\
	main-window.c		\
	main-window.h		\
	main.c			\
	main.h			\
	mblist-window.c		\
	mblist-window.h		\
	message-window.c	\
	message-window.h	\
	pref-manager.c		\
	pref-manager.h		\
	print.c			\
	print.h			\
	sendmsg-window.c	\
	sendmsg-window.h	\
	threads.h		\
	cfg-balsa.c		\
	cfg-balsa.h		\
	cfg-memory-widgets.c	\
	cfg-memory-widgets.h	\
	$(GOB_OUTPUTS)

#	index-child.h		\
#	spell-check.c		\
#	spell-check.h		\
#	balsa-mblist.c		\
#	balsa-mblist.h		\

INCLUDES = \
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/libbalsa \
	-I$(top_srcdir)/idl \
	-I$(top_builddir)/libmutt \
	-I$(top_builddir)/idl \
	-I$(top_srcdir)/libmutt \
	 $(GNOME_INCLUDEDIR)

@BALSA_OFFSET_MAKERULES@

BUILT_SOURCES = cfg-offsets.h Makefile.offset $(GOB_OUTPUTS)

#libbalsa appearing twice is not a bug -- it is mutually dependent with libmutt
balsa_LDADD = \
	$(top_builddir)/libbalsa/libbalsa.a \
	$(top_builddir)/libmutt/libmutt.a \
	$(top_builddir)/libbalsa/libbalsa.a \
	$(top_builddir)/libinit_balsa/libinit_balsa.a \
	@LIBESD_LIB@	\
	$(GNOME_LIBDIR) \
	$(top_builddir)/idl/libbalsasrv.a \
	$(GNOMEGNORBA_LIBS)  \
	$(INTLLIBS)

balsa_DEPENDENCIES = \
	$(top_builddir)/libbalsa/libbalsa.a \
	$(top_builddir)/libmutt/libmutt.a \
	$(top_builddir)/libinit_balsa/libinit_balsa.a

dist-hook:
	mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.xpm $(distdir)/pixmaps

%.c %.h: %.gob
	gob --no-private-header $<

EXTRA_DIST = \
	$(GOBFILES)		\
	$(BUILT_SOURCES)

# test_cfg_SOURCES = cfg-backend.c test-cfg.c
# test_cfg_LDADD = $(GNOME_LIBDIR) $(GNOMEGNORBA_LIBS) $(INTLLIBS)
# noinst_PROGRAMS = test_cfg