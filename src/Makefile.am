bin_PROGRAMS = balsa

balsa_BASE_SOURCES =	\
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
	information-dialog.h	\
	information-dialog.c	\
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
	quote-color.c		\
	quote-color.h		\
	threads.h		\
	spell-check.c		\
	spell-check.h		\
	ldap-addressbook.c	\
	ldap-addressbook.h 	
#	index-child.h		\

balsa_IDL_SOURCES = \
	balsa-common.c		\
	balsa-skels.c		\
	balsa-stubs.c		
balsa_IDL_SOURCES = 

balsa_SOURCES = $(balsa_BASE_SOURCES) $(balsa_IDL_SOURCES)

INCLUDES = \
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/libbalsa \
	-I$(top_builddir)/libmutt \
	-I$(top_srcdir)/libmutt \
	 $(GNOME_INCLUDEDIR)

CFLAGS += $(GTKHTML_CFLAGS)

balsa_LDADD = \
	$(top_builddir)/libbalsa/libbalsa.a	\
	$(top_builddir)/libmutt/imap/libimap.a	\
	$(top_builddir)/libmutt/libmutt.a	\
	$(top_builddir)/libinit_balsa/libinit_balsa.a \
	-lPropList \
	-lpspell \
	@LIBESD_LIB@ \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS) \
	$(INTLLIBS) \
	$(PTHREAD_LIB) \
	$(GTKHTML_LIBS)	

balsa_DEPENDENCIES = \
	$(top_builddir)/libbalsa/libbalsa.a	\
	$(top_builddir)/libmutt/libmutt.a	\
        $(top_builddir)/libmutt/imap/libimap.a	\
	$(top_builddir)/libmutt/libmutt.a	\
	$(top_builddir)/libinit_balsa/libinit_balsa.a

dist-hook:
	mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.xpm $(distdir)/pixmaps

idldir = $(datadir)/idl

idl_DATA = ../idl/balsa.idl

CORBA_SRCLIST = balsa-common.c balsa-skels.c balsa-stubs.c \
                balsa.h


BALSA_ORB_SRCLIST = orbit-glue.c 


$(CORBA_SRCLIST): $(idl_DATA)
		$(ORBIT_IDL) $(idl_DATA)
