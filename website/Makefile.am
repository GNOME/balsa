SUBDIRS = images screens

EXTRA_DIST =			\
	index.html.in		\
	main.html.in		\
	download.html.in	\
	faq.html.in		\
	features.html.in	\
	screenshots.html.in	\
	style.css.in

HTML_FILES = 			\
	index.html		\
	main.html		\
	download.html		\
	faq.html		\
	features.html		\
	screenshots.html

if WEBSITE_INSTALLED
websitedir = @website_prefix@
website_DATA = $(HTML_FILES) style.css
endif

%.html: $(srcdir)/%.html.in $(top_builddir)/config.status
	cd $(top_builddir) \
		&& CONFIG_FILES=$(subdir)/$@ CONFIG_HEADERS= $(SHELL) ./config.status

style.css: $(srcdir)/style.css.in $(top_builddir)/config.status
	cd $(top_builddir) \
		&& CONFIG_FILES=$(subdir)/$@ CONFIG_HEADERS= $(SHELL) ./config.status
