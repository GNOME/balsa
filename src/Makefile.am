CFLAGS += -Wall -Wmissing-prototypes -Wpointer-arith

bin_PROGRAMS = balsa

balsa_SOURCES =	\
	address-book.c		\
	balsa-app.c		\
	balsa-icons.c		\
	balsa-index.c		\
	balsa-init.c		\
	balsa-mblist.c		\
	balsa-message.c		\
	filter-edit-callbacks.c	\
	filter-edit-dialog.c	\
	balsa-index-page.c	\
	local-mailbox.c		\
	mailbox-conf.c		\
	main-window.c		\
	main.c			\
	message-window.c	\
	pref-manager.c		\
	print.c			\
	save-restore.c		\
	sendmsg-window.c	\
	address-book.h		\
	balsa-app.h		\
	balsa-icons.h		\
	balsa-index.h		\
	balsa-mblist.h		\
	balsa-init.h		\
	balsa-message.h		\
	filter-edit.h		\
	index-child.h		\
	local-mailbox.h		\
	mailbox-conf.h		\
	main-window.h		\
	main.h			\
	message-window.h	\
	pref-manager.h		\
	print.h			\
	save-restore.h		\
	sendmsg-window.h	\
	mblist-window.c		\
	mblist-window.h


INCLUDES=						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-I.						\
	-I${top_srcdir}/libbalsa			\
	-I${top_srcdir}					\
	-I$(top_srcdir)/idl				\
	-I$(top_builddir)/idl				\
	-I${top_builddir}/libmutt			\
	 $(GNOME_INCLUDEDIR)
	    

balsa_LDADD = \
	$(top_builddir)/libbalsa/libbalsa.a \
	$(top_builddir)/libmutt/libmutt.a \
	$(top_builddir)/libbalsa/libbalsa.a \
	-lPropList	\
	-lpthread  \
	-lgthread  \
	@LIBESD_LIB@	\
	$(GNOME_LIBDIR) \
	$(top_builddir)/idl/libbalsasrv.a \
	$(GNOMEGNORBA_LIBS)  \
	$(INTLLIBS)

dist-hook:
	mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.xpm $(distdir)/pixmaps












