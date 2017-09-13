# doc/meson.build

gnome = import('gnome')

HELP_ID      = 'balsa'

HELP_LINGUAS = [ 'cs', 'de', 'el', 'es', 'fr', 'sl' ]

HELP_FILES   = [
               'ab-conf.page',
               'authors.page',
               'common-tasks.page',
               'folder-config.page',
               'getting-started.page',
               'glossary.page',
               'identities.page',
               'index.page',
               'introduction.page',
               'its-not-working.page',
               'mbox-formats.page',
               'preferences-address-books.page',
               'preferences-display-options.page',
               'preferences-mail-options.page',
               'preferences-miscellaneous.page',
               'preferences.page',
 	       'preferences-spelling.page',
               'preferences-startup.page',
               'toolbar-prefs.page',
               'win-composer.page',
               'win-filters.page',
               'win-main.page',
               'win-message.page',
               'win-run-filters.page',
               'win-search.page',
               ]

HELP_EXTRA   = 'balsa.entities'

HELP_MEDIA   = [
	      'figures/address-book-ldap.png',
	      'figures/address-book-modify.png',
	      'figures/address-book.png',
	      'figures/address-book-vcard.png',
	      'figures/balsa_logo.png',
	      'figures/druid-start.png',
	      'figures/example-alias.png',
	      'figures/figuresdobaaddress-book-modify.png',
	      'figures/main-window.png',
	      'figures/main-window-toolbar.png',
	      'figures/message-window.png',
	      'figures/msg-part-select.png',
	      'figures/newmsg-window-attachments.png',
	      'figures/newmsg-window.png',
	      'figures/newmsg-window-toolbar.png',
	      'figures/preferences-window.png',
	      'figures/spell-check.png',
              ]

files = [HELP_EXTRA]
files += HELP_FILES

gnome.yelp(HELP_ID,
           sources   : files,
           media     : HELP_MEDIA,
           languages : HELP_LINGUAS)
