if BUILD_WITH_G_D_U
include $(top_srcdir)/gnome-doc-utils.make
dist-hook: doc-dist-hook
DOC_MODULE = balsa
DOC_ENTITIES =
DOC_INCLUDES =
DOC_FIGURES = figures/address-book-ldap.png			\
	      figures/address-book-modify.png			\
	      figures/address-book.png				\
	      figures/address-book-vcard.png			\
	      figures/balsa_logo.png				\
	      figures/druid-start.png				\
	      figures/example-alias.png				\
	      figures/figuresdobaaddress-book-modify.png	\
	      figures/main-window.png				\
	      figures/main-window-toolbar.png			\
	      figures/message-window.png			\
	      figures/msg-part-select.png			\
	      figures/newmsg-window-attachments.png		\
	      figures/newmsg-window.png				\
	      figures/newmsg-window-toolbar.png			\
	      figures/preferences-window.png			\
	      figures/spell-check.png
DOC_LINGUAS = de es fr sl
else
# gnome-doc-utils is not obligatory but the old documentation style
# is not supported any more.
# SUBDIRS = C de
# EXTRA_DIST = omf.make xmldocs.make

doc-dist-hook:

endif
