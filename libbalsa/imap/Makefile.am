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
	libimap-marshal.c	\
	libimap-marshal.h	\
	libimap.h	\
	pop3.c		\
	pop3.h		\
	siobuf.c	\
	siobuf.h	\
	util.c		\
	util.h

EXTRA_DIST = \
	libimap-marshal.list

AM_CPPFLAGS = -I${top_builddir} -I${top_srcdir} -I${top_srcdir}/libbalsa \
	-I${top_srcdir}/libbalsa/imap \
	$(BALSA_CFLAGS)

AM_CFLAGS   = $(LIBIMAP_CFLAGS) -ansi

#Signal marshallers
marshal_prefix = libimap
libimap-marshal.h: libimap-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=$(marshal_prefix) libimap-marshal.list --header > libimap-marshal.tmp \
	&& mv libimap-marshal.tmp libimap-marshal.h ) \
	|| ( rm -f libimap-marshal.tmp && exit 1 )
libimap-marshal.c: libimap-marshal.h
	( echo "#include \"libimap-marshal.h\"" > libimap-marshal.tmp \
	&& @GLIB_GENMARSHAL@ --prefix=$(marshal_prefix) libimap-marshal.list --body >> libimap-marshal.tmp \
	&& mv libimap-marshal.tmp libimap-marshal.c ) \
	|| ( rm -f libimap-marshal.tmp && exit 1 )

#$(libimap_la_OBJECTS): libimap-marshal.h
$(libimap_a_OBJECTS): libimap-marshal.h
