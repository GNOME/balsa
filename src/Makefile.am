bin_PROGRAMS = balsa

balsa_SOURCES =	\
	balsa-app.c		\
	balsa-index.c		\
	balsa-init.c		\
	balsa-message.c		\
	index-child.c		\
	local-mailbox.c		\
	mailbox-manager.c	\
	main-window.c		\
	main.c			\
	mblist-window.c		\
	message-window.c	\
	new-mailbox.c		\
	pref-manager.c		\
	save-restore.c		\
	sendmsg-window.c

INCLUDES=-I. -I../libbalsa $(GNOME_INCLUDEDIR)

balsa_LDADD = \
	$(top_builddir)/libbalsa/libbalsa.a \
	$(top_builddir)/libmutt/libmutt.a \
	$(top_builddir)/libbalsa/libbalsa.a \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS) \
	$(GTKXMHTML_LIBS) \
	$(INTLLIBS)
