SGML_FILES = \
	balsa.sgml

EXTRA_DIST = \
	topic.dat \
	win-config-gen \
	$(SGML_FILES)

balsa_helpdir = $(gnomedatadir)/gnome/help/balsa/C

balsa_help_DATA = \
	topic.dat \
	index.html

NUM_PREF_PAGES = 11

GDP_STYLESHEET = \
	stylesheet/dsssl/docbook-gnome/gdp-both.dsl

MANUAL_IMAGES := $(wildcard images/*.png)

# A pattern rule for the eps files

%.eps : %.png
	img2eps $<

# Only build html by default
all: index.html

# This is where the SGML bit is cool
# older db2html don't support the stylesheets, detect it.
index.html: $(SGML_FILES)
	if test "$(HAVE_JW)" = 'yes' ; then 		\
		jw -c /etc/sgml/catalog balsa.sgml -o balsa; \
	else 						\
		db2html balsa.sgml; 	\
	fi
	-cp balsa/index.html index.html
	-cd balsa && ../win-config-gen $(NUM_PREF_PAGES)

epsimages: $(MANUAL_IMAGES)

balsa.pdf: $(SGML_FILES)
	-for file in $(srcdir)/images/*.png; do img2eps $$file; done
	-db2pdf -d $(GDP_STYLESHEET) $<

balsa.rtf: $(SGML_FILES)
	-db2rtf -d $(GDP_STYLESHEET) $<

# Dist / install hook thingies
dist-hook:
	-cp README $(distdir)
	mkdir $(distdir)/balsa
	-cp balsa/*.html $(distdir)/balsa
	mkdir $(distdir)/images
	-cp $(srcdir)/images/*.png $(distdir)/images
	mkdir $(distdir)/stylesheet-images
	-cp $(srcdir)/stylesheet-images/*.gif $(distdir)/stylesheet-images

install-data-local: index.html
	$(mkinstalldirs) $(DESTDIR)$(balsa_helpdir)/images
	$(mkinstalldirs) $(DESTDIR)$(balsa_helpdir)/stylesheet-images
	-for file in balsa/*.html; do \
	basefile=`basename $$file`; \
	$(INSTALL_DATA) $$file $(DESTDIR)$(balsa_helpdir)/$$basefile; \
	done
	-for file in $(srcdir)/images/*.png ; do \
	basefile=`basename $$file`; \
	$(INSTALL_DATA) $$file $(DESTDIR)$(balsa_helpdir)/images/$$basefile;\
	done
	-for file in $(srcdir)/stylesheet-images/*.gif ; do \
	basefile=`basename $$file`; \
	$(INSTALL_DATA) $$file $(DESTDIR)$(balsa_helpdir)/stylesheet-images/$$basefile;\
	done

clean-hook:
	-cd images && rm *.eps
