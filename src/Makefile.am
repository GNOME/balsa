bin_PROGRAMS = balsa

balsa_SOURCES =	\
	balsa-app.c		\
	balsa-index.c		\
	balsa-message.c		\
	c-client.c		\
	mailbox.c		\
	mailbox-manager.c	\
	main-window.c		\
	main.c			\
	misc.c			\
	pref-manager.c		\
	save-restore.c		\
	sendmsg-window.c	\
	addrbook-manager.c

INCLUDES=-I. -I$(srcdir)/../imap/c-client $(GNOME_INCLUDEDIR)

balsa_LDADD = ../imap/c-client/c-client.a \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS) \
	$(GTKXMHTML_LIBS) \
	$(INTLLIBS)
