IN_FILES = \
	download.html.in \
	faq.html.in \
	features.html.in \
	screenshots.html.in \
	bugs.html.in \
	style.css.in

HTML_FILES = \
	index.html \
	main.html \
	download.html \
	faq.html \
	features.html \
	screenshots.html \
	bugs.html

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
	# -for file in $(srcdir)/images/*.png; do \
	# 	basefile=`basename $$file`; \
	# 	$(INSTALL_DATA) $$file $(DESTDIR)$(websitedir)/images/$$basefile; \
	# done
	$(mkinstalldirs) $(DESTDIR)$(websitedir)/screens
	-for file in $(srcdir)/screens/*.jpg; do \
		basefile=`basename $$file`; \
		$(INSTALL_DATA) $$file $(DESTDIR)$(websitedir)/screens/$$basefile; \
	done
	-for file in $(srcdir)/screens/*.png; do \
		basefile=`basename $$file`; \
		$(INSTALL_DATA) $$file $(DESTDIR)$(websitedir)/screens/$$basefile; \
	done
