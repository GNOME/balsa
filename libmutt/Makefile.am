noinst_LIBRARIES = libmutt.a

libmutt_a_SOURCES = \
	copy.c date.c filter.c flaglib.c from.c getdomain.c globals.c \
	handler.c hash.c hook.c init.c lib.c mbox.c mh.c mx.c parse.c \
	rfc1524.c rfc2047.c rfc822.c score.c sendlib.c sigblock.c \
	sort.c system.c thread.c

#
#	BOGUS FIX THE SHAREDIR SOMEONE
#
INCLUDES=-I. -I../src -Irx $(GNOME_INCLUDEDIR) -DLIBMUTT=1 -DSHAREDIR=\"/etc\" -DRETSIGTYPE=int -DEXECSHELL=\"/bin/sh\"

