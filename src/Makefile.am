bin_PROGRAMS = balsa

balsa_BASE_SRCLIST =	\
	address-book-config.c	\
	address-book-config.h	\
	address-book.c		\
	address-book.h		\
	balsa-app.c		\
	balsa-app.h		\
	balsa-icons.c		\
	balsa-icons.h		\
	balsa-index-threading.c	\
	balsa-index-threading.h	\
	balsa-index.c		\
	balsa-index.h		\
	balsa-mblist.c		\
	balsa-mblist.h		\
	balsa-message.c		\
	balsa-message.h		\
	expand-alias.c		\
	expand-alias.h		\
	filter-edit-callbacks.c	\
	filter-edit-dialog.c	\
	filter-edit.h		\
	filter-export-callbacks.c \
	filter-export-dialog.c	\
	filter-export.h		\
	filter-run-callbacks.c	\
	filter-run-dialog.c	\
	filter-run.h		\
	folder-conf.c		\
	folder-conf.h		\
	information-dialog.c	\
	information-dialog.h	\
	mailbox-conf.c		\
	mailbox-conf.h		\
	mailbox-node.c		\
	mailbox-node.h		\
	main-window.c		\
	main-window.h		\
	main.c			\
	message-window.c	\
	message-window.h	\
	pref-manager.c		\
	pref-manager.h		\
	print.c			\
	print.h			\
	quote-color.c		\
	quote-color.h		\
	save-restore.c		\
	save-restore.h		\
	sendmsg-window.c	\
	sendmsg-window.h	\
	spell-check.c		\
	spell-check.h		\
	store-address.c		\
	store-address.h		\
	threads.h		\
	toolbar-factory.c	\
	toolbar-factory.h	\
	toolbar-prefs.c		\
	toolbar-prefs.h


balsa_IDL_SRCLIST = # balsa-common.c balsa-skels.c balsa-stubs.c		

balsa_SOURCES = $(balsa_BASE_SRCLIST) $(balsa_IDL_SRCLIST)

INCLUDES = \
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/libbalsa

balsa_LDADD = \
	$(top_builddir)/libbalsa/libbalsa.a	\
	$(top_builddir)/libmutt/imap/libimap.a	\
	$(top_builddir)/libmutt/libmutt.a	\
	$(top_builddir)/libinit_balsa/libinit_balsa.a \
	-lpspell \
	-lltdl	\
	$(GDBM_LIBS) \
	$(INTLLIBS) \
	$(PCRE_LIBS) \
	$(PTHREAD_LIB)

dist-hook:
	mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.{xpm,png} $(distdir)/pixmaps

idldir = $(datadir)/idl

idl_DATA = #../idl/balsa.idl

CORBA_SRCLIST = balsa-common.c balsa-skels.c balsa-stubs.c \
                balsa.h

BALSA_ORB_SRCLIST = orbit-glue.c 


$(CORBA_SRCLIST): $(idl_DATA)
		$(ORBIT_IDL) $(idl_DATA)
