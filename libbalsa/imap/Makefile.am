#lib_LTLIBRARIES = libimap.la
noinst_LIBRARIES = libimap.a
noinst_PROGRAMS  = imap_tst

imap_tst_SOURCES = imap_tst.c

# We specify path to libimap.a without directories: it helps make to
# resolve dependencies and the executable is to be created in the same
# directory as the imap_tst executable. It clearly does not break the
# distcheck target either.

imap_tst_LDADD = \
	libimap.a	\
	${top_builddir}/libnetclient/libnetclient.a \
	$(BALSA_LIBS)

#libimap_la_SOURCES =  
libimap_a_SOURCES = \
	auth-cram.c	\
	auth-gssapi.c	\
	imap-auth.c	\
	imap-auth.h	\
	imap-commands.c	\
	imap-commands.h	\
	imap_compress.c	\
	imap_compress.h	\
	imap-handle.c	\
	imap-handle.h	\
	imap_search.c	\
	imap_search.h	\
	imap-tls.c	\
	imap_private.h	\
	libimap.h	\
	siobuf-nc.h	\
	util.c		\
	util.h

AM_CPPFLAGS = -I${top_builddir} -I${top_srcdir} -I${top_srcdir}/libbalsa \
	-I${top_srcdir}/libnetclient \
	-I${top_srcdir}/libbalsa/imap \
	-DG_LOG_DOMAIN=\"imap\" \
	$(BALSA_CFLAGS)

AM_CFLAGS   = $(LIBIMAP_CFLAGS)
