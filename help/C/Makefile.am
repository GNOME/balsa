SGML_FILES = \
	balsa.sgml

EXTRA_DIST = \
	topic.dat \
	$(SGML_FILES)

balsa_helpdir = $(datadir)/gnome/help/balsa/C

balsa_help_DATA = \
	topic.dat \
	index.html

# For now we don't use pictures but if later we use we just need to uncomment
# the comment lines and add the images dir to CVS like the images it self

all: index.html

# This is where the SGML bit is cool

index.html: $(SGML_FILES)
	-db2html $<
	-cp balsa/index.html index.html

balsa.ps: $(SGML_FILES)
	-db2ps $<

balsa.rtf: $(SGML_FILES)
	-db2rtf $<

# Dist / install hook thingies

dist-hook:
	mkdir $(distdir)/balsa
	-cp balsa/*.html $(distdir)/balsa
	mkdir $(distdir)/images
	-cp $(srcdir)/images/*.png $(distdir)/images
	mkdir $(distdir)/stylesheet-images
	-cp balsa/stylesheet-images/*.gif $(distdir)/stylesheet-images

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
	-for file in balsa/stylesheet-images/*.gif ; do \
	basefile=`basename $$file`; \
	$(INSTALL_DATA) $$file $(DESTDIR)$(balsa_helpdir)/stylesheet-images/$$basefile;\
	done