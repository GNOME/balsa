IN_FILES =

HTML_FILES = \
	bugs.html	\
	download.html	\
	faq.html	\
	features.html	\
	index.html	\
	list.html	\
	oldnews.html	\
	screenshots.html

EXTRA_DIST = $(IN_FILES)

websitedir = @prefix@
website_DATA = $(HTML_FILES) style.css

all: $(website_DATA)

dist-hook:
	mkdir $(distdir)/images
	-cp $(srcdir)/images/*.jpg $(distdir)/images
	-cp $(srcdir)/images/*.png $(distdir)/images
	mkdir $(distdir)/screens
	-cp $(srcdir)/screens/*.jpg $(distdir)/screens
	-cp $(srcdir)/screens/*.png $(distdir)/screens

install-data-local: $(website_DATA)
	$(mkinstalldirs) $(DESTDIR)$(websitedir)/images
	-for file in $(srcdir)/images/*.jpg; do \
		basefile=`basename $$file`; \
		$(INSTALL_DATA) $$file $(DESTDIR)$(websitedir)/images/$$basefile; \
	done
#	 -for file in $(srcdir)/images/*.png; do \
#	 	basefile=`basename $$file`; \
# 	$(INSTALL_DATA) $$file $(DESTDIR)$(websitedir)/images/$$basefile; \
#	 done
	$(mkinstalldirs) $(DESTDIR)$(websitedir)/screens
	-for file in $(srcdir)/screens/*.jpg  $(srcdir)/screens/*.png; do \
		basefile=`basename $$file`; \
		$(INSTALL_DATA) $$file $(DESTDIR)$(websitedir)/screens/$$basefile; \
	done
	$(mkinstalldirs) $(DESTDIR)$(websitedir)/screens/thumbnails
	-for file in $(srcdir)/screens/thumbnails/*.png; do \
		basefile=`basename $$file`; \
		$(INSTALL_DATA) $$file $(DESTDIR)$(websitedir)/screens/thumbnails/$$basefile; \
	done

