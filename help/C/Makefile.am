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
	@short=`echo $@ |sed -e "s,^.*/,,"` ; \
	if test ! -f "$(srcdir)/$${short}" ; then \
		echo Warning: documentation Makefile.am needs updating: $${short} does not exist 1>&2 ; \
		if test -z "`cat $(srcdir)/topic.dat |grep $${short}`" ; then \
			echo Warning: topic.dat references old file $short 1>&2 ; \
 		fi ; \
	fi

if HAVE_DB2HTML
html-stamp: balsa.sgml
	rm -f $(srcdir)/*.htm
	(cd $(srcdir) && db2html balsa.sgml) 1>/dev/null
	mv $(srcdir)/balsa/*.htm $(srcdir)
	rm -rf $(srcdir)/balsa/
	@for f in $(srcdir)/*.htm ; do \
		short=`echo $$f |sed -e "s,^.*/,,"` ; \
		if test -z "`echo "${HTML_FILES}" | grep "$${short}"`" ; then \
			echo Warning: documentation Makefile.am needs updating: $${short} is not listed in HTML_FILES 1>&2 ; \
		fi ; \
	done
	touch html-stamp
else
html-stamp: balsa.sgml
	touch html-stamp
endif

if sys_install
helpdir = $(gnomedatadir)/gnome/help/balsa/C
else
helpdir = $(datadir)/gnome/help/balsa/C
endif

help_DATA = topic.dat $(HTML_FILES)

install-data-local:
	-rm -f $(helpdir)/index.html
	cd $(helpdir) ; $(LN_S) t1.html index.html || cp -f t1.html index.html

