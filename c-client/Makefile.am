noinst_LIBRARIES = libc-client.a

libc_client_a_SOURCES = \
	mail.c		\
	misc.c		\
	newsrc.c	\
	smanager.c	\
	osdep.c		\
	utf8.c		\
	siglocal.c	\
	dummy.c		\
	pseudo.c	\
	netmsg.c	\
	flstring.c	\
	fdstring.c	\
	rfc822.c	\
	nntp.c		\
	smtp.c		\
	imap4r1.c	\
	pop3.c		\
	unix.c		\
	mbox.c		\
	mbx.c		\
	mmdf.c		\
	tenex.c		\
	mtx.c		\
	news.c		\
	phile.c		\
	mh.c		\
	mx.c

INCLUDES = -I.

# erk hardcoded stuff

CPPFLAGS = \
	-DNFSKLUDGE					\
	-DSTDPROTO=unixproto				\
	-DMAILSPOOL=\"/var/spool/mail\"			\
	-DANONYMOUSHOME=\"/var/spool/mail/anonymous\"	\
	-DACTIVEFILE=\"/usr/lib/news/active\"		\
	-DNEWSSPOOL=\"/var/spool/news\"			\
	-DRSHPATH=\"/usr/bin/rsh\"
