bin_PROGRAMS = balsa

balsa_SOURCES =	\
	balsa-app.c		\
	balsa-index.c		\
	balsa-init.c		\
	balsa-message.c		\
	index-child.c		\
	local-mailbox.c		\
	mailbox-conf.c		\
	main-window.c		\
	main.c			\
	mblist-window.c		\
	message-window.c	\
	pref-manager.c		\
	save-restore.c		\
	sendmsg-window.c

INCLUDES=-I. -I${top_srcdir}/libbalsa -I${top_srcdir}/libmutt $(GNOME_INCLUDEDIR)

balsa_LDADD = \
	$(top_builddir)/libbalsa/libbalsa.a \
	$(top_builddir)/libmutt/libmutt.a \
	$(top_builddir)/libbalsa/libbalsa.a \
	-lPropList	\
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS) \
	$(GTKXMHTML_LIBS) \
	$(INTLLIBS)
