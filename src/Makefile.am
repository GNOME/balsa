bin_PROGRAMS = balsa

balsa_SOURCES =	\
	balsa-app.c		\
	balsa-index.c		\
	balsa-message.c		\
	index-child.c		\
	local-mailbox.c		\
	mailbox.c		\
	mailbox-manager.c	\
	main-window.c		\
	main.c			\
	mblist-window.c		\
	message-window.c	\
	misc.c			\
	new-mailbox.c		\
	pref-manager.c		\
	save-restore.c		\
	sendmsg-window.c	\
	addrbook.c		\
	addrbook-manager.c	\
	balsa-init.c

INCLUDES=-I. -I$(srcdir)/../c-client $(GNOME_INCLUDEDIR)

balsa_LDADD = $(top_builddir)/c-client/libc-client.a \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS) \
	$(GTKXMHTML_LIBS) \
	$(INTLLIBS)
