#Stolen from gnorpm.
#What's up with this naming scheme???

HTML_FILES = \
	t1.htm \
	x23.htm \
	x30.htm \
	x42.htm \
	x53.htm \
	g56.htm

EXTRA_DIST = \
	balsa.sgml		\
	topic.dat		\
	$(HTML_FILES)

$(HTML_FILES): html-stamp

if HAVE_DB2HTML
html-stamp: balsa.sgml
	(cd $(srcdir) && db2html balsa.sgml) 1>/dev/null
	cd $(srcdir) && mv balsa/*.htm .
	rm -rf $(srcdir)/balsa/
	touch html-stamp
else
html-stamp: balsa.sgml
	touch html-stamp
endif

helpdir = $(gnomedatadir)/gnome/help/balsa/C

help_DATA = topic.dat $(HTML_FILES)

install-data-local:
	-rm -f $(helpdir)/index.html
	cd $(helpdir) ; $(LN_S) t1.html index.html || cp -f t1.html index.html

