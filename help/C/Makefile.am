#Stolen from gnorpm.
#What's up with this naming scheme???

HTML_FILES = \
	t1.htm \
	x24.htm \
	x35.htm \
	x188.htm \
	x244.htm \
	x277.htm \
	x324.htm \
	g327.htm

EXTRA_DIST = \
	balsa.sgml		\
	topic.dat		\
	$(HTML_FILES)

$(HTML_FILES): html-stamp
	@if test ! -f $@ ; then \
		echo Warning: documentation Makefile.am needs updating: $@ does not exist 1>&2 ; \
		if test -z `cat topic.dat |grep $@` ; then \
			echo Warning: topic.data references old file $@ 1>&2 ; \
 		fi ; \
	fi

if HAVE_DB2HTML
html-stamp: balsa.sgml
	rm -f *.htm
	(cd $(srcdir) && db2html balsa.sgml) 1>/dev/null
	cd $(srcdir) && mv balsa/*.htm .
	rm -rf $(srcdir)/balsa/
	@for f in *.htm ; do \
		if test -z "`echo "${HTML_FILES}" | grep "$$f"`" ; then \
			echo Warning: documentation Makefile.am needs updating: $$f is not listed in HTML_FILES 1>&2 ; \
		fi ; \
	done
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

