balsa_GNORBA	= balsa.gnorba
EXTRA_DIST	= $(balsa_GNORBA)

balsagnorbadir	= $(sysconfdir)/CORBA/servers
balsagnorba_DATA = $(balsa_GNORBA)


lib_LIBRARIES = libbalsasrv.a

BUILT_SOURCES = balsa-skels.c \
		balsa.h

INCLUDES=$(GNOME_INCLUDEDIR)

libbalsasrv_a_SOURCES = \
	balsa-skels.c \
	balsa-common.c     \
	balsa-impl.c \
	balsa.h

$(BUILT_SOURCES): balsa.idl
	orbit-idl balsa.idl
