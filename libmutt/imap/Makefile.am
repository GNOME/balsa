## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign

if USE_GSS
GSSSOURCES = auth_gss.c
endif

if USE_SSL
SSLSOURCES = imap_ssl.c
SSLHEADERS = imap_ssl.h
endif

EXTRA_DIST = BUGS README TODO imap_ssl.c imap_ssl.h auth_gss.c browse.c

CPPFLAGS=-DSHAREDIR=\"$(sharedir)\" -DSYSCONFDIR=\"$(sysconfdir)\" -DLIBMUTT
INCLUDES = -I$(top_srcdir)

noinst_LIBRARIES = libimap.a
noinst_HEADERS = imap_private.h imap_socket.h message.h $(SSLHEADERS) \
	md5.h imap.h

libimap_a_SOURCES = auth.c command.c imap.c imap.h message.c \
	socket.c util.c $(GSSSOURCES) $(SSLSOURCES)
