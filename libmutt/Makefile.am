EXTRA_DIST = configure muttconfig.h.in

noinst_LIBRARIES = libmutt.a

libmutt_a_SOURCES = \
	account.c	\
	attach.c 	\
	base64.c	\
	browser.h	\
	buffy.c 	\
	charset.c 	\
	copy.c 		\
	date.c 		\
	dotlock.c	\
	filter.c 	\
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
	parse.c 	\
	pattern.c 	\
	rfc1524.c 	\
	rfc2047.c 	\
	rfc822.c 	\
	score.c 	\
	sendlib.c 	\
	signal.c 	\
	sort.c 		\
	system.c 	\
	thread.c	\
	url.c		\
	acconfig.h	\
	account.h	\
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
	mutt_ssl.h	\
	mutt_socket.h	\
	mx.h		\
	pager.h		\
	parse.h		\
	protos.h	\
	rfc1524.h	\
	rfc2047.h	\
	rfc822.h	\
	sort.h		\
	url.h

SUBDIRS = imap

DEFS = -DSHAREDIR=\"$(sharedir)\" -DSYSCONFDIR=\"$(sysconfdir)\" -DLIBMUTT
INCLUDES=-I. -I./imap
