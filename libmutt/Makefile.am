EXTRA_DIST = configure muttconfig.h.in mutt_ssl.c mutt_ssl.h

if USE_SSL
SSLSOURCES = mutt_ssl.c mutt_ssl.h
endif

if USE_SASL
SASLSOURCES = mutt_sasl.c
endif

noinst_LIBRARIES = libmutt.a


libmutt_a_SOURCES = \
	account.c	\
	ascii.c		\
	attach.c 	\
	base64.c	\
	browser.h	\
	buffy.c 	\
	charset.c 	\
	copy.c 		\
	date.c 		\
	dotlock.c	\
	flags.c 	\
	from.c 		\
	getdomain.c	\
	globals.c 	\
	handler.c 	\
	hash.c 		\
	init.c 		\
	lib.c 		\
	mbox.c		\
	md5c.c		\
	mh.c 		\
	mx.c 		\
	mutt_socket.c	\
	muttlib.c	\
	parse.c 	\
	pattern.c 	\
	recvattach.c	\
	rfc2047.c 	\
	rfc2231.c	\
	rfc822.c 	\
	sendlib.c 	\
	signal.c 	\
	sort.c 		\
	url.c		\
	account.h	\
	ascii.h		\
	attach.h	\
	buffy.h		\
	charset.h 	\
	copy.h		\
	dotlock.h	\
	functions.h	\
	globals.h	\
	hash.h		\
	history.h	\
	init.h		\
	keymap.h	\
	keymap_defs.h	\
	lib.h		\
	mailbox.h	\
	mapping.h	\
	md5.h		\
	mime.h		\
	mutt.h		\
	mutt_curses.h	\
	mutt_menu.h	\
	mutt_regex.h	\
	mutt_sasl.h	\
	mutt_socket.h	\
	mutt_tunnel.h	\
	mx.h		\
	pager.h		\
	parse.h		\
	protos.h	\
	rfc1524.h	\
	rfc2047.h	\
	rfc2231.h	\
	rfc822.h	\
	sort.h		\
	url.h		\
	$(SSLSOURCES)	$(SASLSOURCES)  $(NSSSOURCES)

SUBDIRS = imap

DEFS = -DSHAREDIR=\"$(sharedir)\" -DSYSCONFDIR=\"$(sysconfdir)\" -DLIBMUTT
INCLUDES=-I${top_srcdir} -I${top_srcdir}/imap -I${top_builddir}
