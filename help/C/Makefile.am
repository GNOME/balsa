SGML_FILES = \
	balsa.sgml

EXTRA_DIST = \
	topic.dat \
	$(SGML_FILES)

balsa_helpdir = $(datadir)/gnome/help/balsa/C

balsa_help_DATA = \
	topic.dat \
	balsa.html

# For now we don't use pictures but if later we use we just need to uncomment
# the comment lines and add the images dir to CVS like the images it self

all: 

balsa.html: balsa/balsa.html
	-cd $(srcdir) && cp balsa/balsa.html .

balsa/balsa.html: $(SGML_FILES)
	-cd $(srcdir) && db2html balsa.sgml

dist-hook:
	mkdir $(distdir)/balsa
	-cp $(srcdir)/balsa/*.html $(distdir)/balsa
	-cp $(srcdir)/balsa/balsa.html $(distdir)
#	mkdir $(distdir)/images
#	-cp $(srcdir)/images/*.gif $(srcdir)/images/*.jpg $(distdir)/images

install-data-local: balsa.html
#	$(mkinstalldirs) $(DESTDIR)$(balsa_helpdir)/images
	-for file in $(srcdir)/balsa/*.html; do \
	basefile=`basename $$file`; \
	$(INSTALL_DATA) $$file $(DESTDIR)$(balsa_helpdir)/$$basefile; \
	done
#	-for file in $(srcdir)/images/*.jpg $(srcdir)/images/*.gif; do \
#	$(INSTALL_DATA) $$file $(DESTDIR)$(balsa_helpdir)/images;\
#	done

balsa.ps: balsa.sgml
	-db2ps $<

balsa.rtf: balsa.sgml	
	-db2rtf $<
