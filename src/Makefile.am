bin_PROGRAMS = balsa

filter_src = \
	filter-edit.h		\
	filter-edit-callbacks.c	\
	filter-edit-dialog.c	\
	filter-export-callbacks.c \
	filter-export-dialog.c	\
	filter-export.h		\
	filter-run.h		\
	filter-run-callbacks.c	\
	filter-run-dialog.c

if BALSA_SHOW_ALL
extra_src  = $(filter_src)
else
extra_src  = 
endif

EXTRA_DIST = $(filter_src)

balsa_BASE_SRCLIST =	\
	address-book.c		\
	address-book.h		\
	address-book-config.c	\
	address-book-config.h	\
	balsa-app.c		\
	balsa-app.h		\
	balsa-icons.c		\
	balsa-icons.h		\
	balsa-index.c		\
	balsa-index.h		\
	balsa-index-threading.c	\
	balsa-index-threading.h	\
	balsa-mblist.c		\
	balsa-mblist.h		\
	balsa-message.c		\
	balsa-message.h		\
	expand-alias.c		\
	expand-alias.h		\
	folder-conf.c		\
	folder-conf.h		\
	information-dialog.h	\
	information-dialog.c	\
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
	save-restore.c		\
	save-restore.h		\
	sendmsg-window.c	\
	sendmsg-window.h	\
	store-address.h		\
	store-address.c		\
	quote-color.c		\
	quote-color.h		\
	threads.h		\
	spell-check.c		\
	spell-check.h		\
	toolbar-prefs.c		\
	toolbar-prefs.h		\
	toolbar-factory.c	\
	toolbar-factory.h	\
	$(extra_src)

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
	-lgdbm	\
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
