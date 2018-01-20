# $Id$

noinst_LIBRARIES = libnetclient.a
libnetclient_a_SOURCES =	\
	net-client.c			\
	net-client.h			\
	net-client-pop.c		\
	net-client-pop.h		\
	net-client-smtp.c		\
	net-client-smtp.h		\
	net-client-utils.c		\
	net-client-utils.h
CLEANFILES = doxygen.log
SUBDIRS = test
AM_CFLAGS = $(LIBNETCLIENT_CFLAGS)
AM_CPPFLAGS = -DG_LOG_DOMAIN=\"libnetclient\"

clean-local:
	-rm -rf html

doc:
	@DOXYGEN@ libnetclient.dox