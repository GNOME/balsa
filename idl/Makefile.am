lib_LIBRARIES = libbalsasrv.a

BUILT_SOURCES = balsa-skels.c \
		balsa.h


libbalsasrv_a_SOURCES = \
	balsa-skels.c \
	balsa-common.c     \
	balsa-simple-srv.c \
	balsa.h

$(BUILT_SOURCES): balsa.idl
	orbit-idl balsa.idl
