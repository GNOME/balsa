CFLAGS += -Wall -Wmissing-prototypes -Wpointer-arith

if USE_EXINIT
noinst_LIBRARIES = libinit_balsa.a

libinit_balsa_a_SOURCES = 		\
	$(BUILT_SOURCES)		\
	helper.c			\
	helper.h			\
	init_balsa.c			\
	init_balsa.h

BUILT_SOURCES =				\
	balsa-initdruid.c		\
	balsa-druid-page.c		\
	balsa-druid-page-error.c	\
	balsa-druid-page-welcome.c	\
	balsa-druid-page-user.c		\
	balsa-druid-page-finish.c	\
	balsa-initdruid.h		\
	balsa-druid-page.h		\
	balsa-druid-page-error.h	\
	balsa-druid-page-welcome.h	\
	balsa-druid-page-user.h		\
	balsa-druid-page-finish.h	
endif

GOBFILES =				\
	balsa-initdruid.gob		\
	balsa-druid-page.gob		\
	balsa-druid-page-error.gob	\
	balsa-druid-page-welcome.gob	\
	balsa-druid-page-user.gob	\
	balsa-druid-page-finish.gob

%.c: %.gob
if USE_GOB
	$(GOB) $<
else
	@echo "Info: Not using gob on $<"
endif

EXTRA_DIST = $(GOBFILES)

INCLUDES=-I. -I${top_srcdir} \
	$(GNOME_INCLUDEDIR)
