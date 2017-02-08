bin_PROGRAMS = balsa balsa-ab

balsa_BASE_SRCLIST =	\
	address-book-config.c	\
	address-book-config.h	\
	ab-window.c		\
	ab-window.h		\
	balsa-app.c		\
	balsa-app.h		\
	balsa-icons.c		\
	balsa-icons.h		\
	balsa-index.c		\
	balsa-index.h		\
	balsa-mblist.c		\
	balsa-mblist.h		\
	balsa-message.c		\
	balsa-message.h		\
	balsa-mime-widget.c	\
	balsa-mime-widget.h	\
	balsa-mime-widget-callbacks.c	\
	balsa-mime-widget-callbacks.h	\
	balsa-mime-widget-crypto.c	\
	balsa-mime-widget-crypto.h	\
	balsa-mime-widget-image.c	\
	balsa-mime-widget-image.h	\
	balsa-mime-widget-message.c	\
	balsa-mime-widget-message.h	\
	balsa-mime-widget-multipart.c	\
	balsa-mime-widget-multipart.h	\
	balsa-mime-widget-text.c	\
	balsa-mime-widget-text.h	\
	balsa-mime-widget-vcalendar.c	\
	balsa-mime-widget-vcalendar.h	\
	balsa-cite-bar.c	\
	balsa-cite-bar.h	\
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
	print.h			\
	quote-color.c		\
	quote-color.h		\
	save-restore.c		\
	save-restore.h		\
	sendmsg-window.c	\
	sendmsg-window.h	\
	store-address.c		\
	store-address.h		\
	threads.h		\
	toolbar-factory.c	\
	toolbar-factory.h	\
	toolbar-prefs.c		\
	toolbar-prefs.h

balsa_print_source = print-gtk.c	\
	balsa-print-object.c		\
	balsa-print-object.h		\
	balsa-print-object-decor.c	\
	balsa-print-object-decor.h	\
	balsa-print-object-default.c	\
	balsa-print-object-default.h	\
	balsa-print-object-header.c	\
	balsa-print-object-header.h	\
	balsa-print-object-image.c	\
	balsa-print-object-image.h	\
	balsa-print-object-text.c	\
	balsa-print-object-text.h

if BUILD_WITH_SPELL_CHECKER
balsa_spell_extra =
balsa_spell_extra_dist =	\
		spell-check.c		\
		spell-check.h
else
balsa_spell_extra_dist =
balsa_spell_extra =		\
		spell-check.c		\
		spell-check.h
endif

balsa_SOURCES =			\
	$(balsa_BASE_SRCLIST)	\
	$(balsa_spell_extra) \
	$(balsa_print_source)


DISTCLEANFILES = $(balsa_IDL_SRCLIST)

EXTRA_DIST = $(balsa_spell_extra_dist)

AM_CPPFLAGS = \
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(top_builddir) -I$(top_srcdir) \
	-I$(top_srcdir)/libbalsa \
	-I${top_srcdir}/libnetclient \
	$(BALSA_DEFS)

AM_CFLAGS = $(BALSA_CFLAGS)

balsa_LDADD = \
	$(top_builddir)/libinit_balsa/libinit_balsa.a \
	$(top_builddir)/libbalsa/libbalsa.a	\
	$(top_builddir)/libbalsa/imap/libimap.a	\
	$(top_builddir)/libnetclient/libnetclient.a \
	$(balsa_IDL_OBJS) \
	$(INTLLIBS) \
        $(BALSA_LIBS)



balsa_ab_SOURCES =	ab-main.c		\
			address-book-config.c	\
			address-book-config.h	

balsa_ab_LDADD = \
	$(top_builddir)/libbalsa/libbalsa.a	\
	$(BALSA_AB_LIBS)

dist-hook:
	mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.{xpm,png} $(distdir)/pixmaps

$(balsa_IDL_SRCLIST): $(srcdir)/$(idl_DATA) 
	$(ORBIT_IDL) -I $(LIBBONOBO_IDL) -I $(BONOBO_ACTIVATION_IDL) \
	$(srcdir)/$(idl_DATA)
