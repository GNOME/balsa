noinst_LIBRARIES = libmutt.a

libmutt_a_SOURCES = \
	globals.c lib.c handler.c score.c sort.c thread.c \
	date.c getdomain.c hash.c hook.c sigblock.c filter.c from.c \
	parse.c rfc822.c rfc1524.c rfc2047.c sendlib.c \
	mx.c mbox.c mh.c

#
#	BOGUS FIX THE SHAREDIR SOMEONE
#
INCLUDES=-I. -I../src -Irx $(GNOME_INCLUDEDIR) -DSHAREDIR=\"/etc\" -DRETSIGTYPE=int -DEXECSHELL=\"/bin/sh\"

