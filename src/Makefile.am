bin_PROGRAMS = balsa balsa-ab

balsa_BASE_SRCLIST =	\
	address-book-config.c	\
	address-book-config.h	\
	ab-window.c		\
	ab-window.h		\
	balsa-app.c		\
	balsa-app.h		\
	balsa-bonobo.c		\
	balsa-bonobo.h		\
	balsa-icons.c		\
	balsa-icons.h		\
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

balsa_IDL_SRCLIST = Balsa-common.c Balsa-skels.c Balsa-stubs.c Balsa.h
balsa_IDL_OBJS    = Balsa-common.o Balsa-skels.o Balsa-stubs.o

balsa_SOURCES = $(balsa_BASE_SRCLIST)

# see http://sources.redhat.com/automake/dependencies.html
BUILT_SOURCES = Balsa.h


EXTRA_DIST = \
	Balsa.idl

idldir = $(datadir)/idl
idl_DATA = Balsa.idl

INCLUDES = \
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(top_builddir) -I$(top_srcdir) \
	-I$(top_srcdir)/libbalsa \
	$(BALSA_CFLAGS)

DEFS = -DG_DISABLE_DEPRECATED 

balsa_LDADD = \
	$(top_builddir)/libinit_balsa/libinit_balsa.a \
	$(top_builddir)/libbalsa/libbalsa.a	\
	$(top_builddir)/libbalsa/imap/libimap.a	\
	$(balsa_IDL_OBJS) \
	-lltdl	\
	$(INTLLIBS) \
        $(BALSA_LIBS)



balsa_ab_SOURCES =	ab-main.c

balsa_ab_LDADD = \
	$(top_builddir)/libbalsa/libbalsa.a	\
	$(top_builddir)/libbalsa/imap/libimap.a	\
	$(BALSA_LIBS)

dist-hook:
	mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.{xpm,png} $(distdir)/pixmaps


#auto* *SHOULD* figure this out *grr*
$(srcdir)/balsa-bonobo.c: Balsa.h

$(balsa_IDL_SRCLIST): $(srcdir)/$(idl_DATA) 
	$(ORBIT_IDL) -I $(LIBBONOBO_IDL) -I $(BONOBO_ACTIVATION_IDL) \
	$(srcdir)/$(idl_DATA)
