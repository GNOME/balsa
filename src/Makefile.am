bin_PROGRAMS = balsa

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
	balsa-mblist.c		\
	balsa-mblist.h		\
	balsa-message.c		\
	balsa-message.h		\
	expand-alias.c		\
	expand-alias.h		\
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
	save-restore.c		\
	save-restore.h		\
	sendmsg-window.c	\
	sendmsg-window.h	\
	quote-color.c	\
	quote-color.h	\
	threads.h
#	index-child.h		\
#	spell-check.c		\
#	spell-check.h		\

INCLUDES = \
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/libbalsa \
	-I$(top_builddir)/libmutt \
	-I$(top_srcdir)/libmutt \
	 $(GNOME_INCLUDEDIR)

CFLAGS += $(GTKHTML_CFLAGS)

#libbalsa appearing twice is not a bug -- it is mutually dependent with libmutt
balsa_LDADD = \
	$(top_builddir)/libbalsa/libbalsa.a	\
	$(top_builddir)/libmutt/imap/libimap.a	\
	$(top_builddir)/libmutt/libmutt.a	\
	$(top_builddir)/libmutt/imap/libimap.a	\
	$(top_builddir)/libmutt/libmutt.a	\
	$(top_builddir)/libinit_balsa/libinit_balsa.a \
	-lPropList	\
	@LIBESD_LIB@	\
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS)  \
	$(INTLLIBS) \
	$(PTHREAD_LIB) \
	$(GTKHTML_LIBS)	

balsa_DEPENDENCIES = \
	$(top_builddir)/libbalsa/libbalsa.a	\
	$(top_builddir)/libmutt/imap/libimap.a	\
	$(top_builddir)/libmutt/libmutt.a	\
        $(top_builddir)/libmutt/imap/libimap.a	\
	$(top_builddir)/libmutt/libmutt.a	\
	$(top_builddir)/libinit_balsa/libinit_balsa.a

dist-hook:
	mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.xpm $(distdir)/pixmaps
