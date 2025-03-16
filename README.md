# Balsa 
An E-Mail Client, version 2.6.x

See [ChangeLog](./ChangeLog) for the list of the recent changes and [NEWS](./NEWS) for highlights.

Copyright (C) 1997-2020 Stuart Parmenter and others

See [COPYING](./COPYING) for license information.

See [AUTHORS](./AUTHORS) for a list of contributors


## Authors:

See [AUTHORS](./AUTHORS)


## Website:

https://pawsa.fedorapeople.org/balsa/


## Description:

Balsa is an e-mail reader.  This client is part of the GNOME
desktop environment.  It supports local mailboxes, POP3 and IMAP.


## Configuration:

Balsa can be built using either Autotools (configure, make, and so on)
or using Meson and an appropriate backend such as Ninja. Details of the
autotools configure script follow; the corresponding Meson files, meson.build
and meson_options.txt, provide exactly the same configuration options, in a
more Mesonish way.

Balsa has a lot of options to its configure script; you
should run `./configure --help` to get an idea of them. More
complete descriptions are here.

Basically, Balsa requires
- glib-2.0 >= 2.48.0
- gtk+-3.0 >= 3.18.0
- gmime-3.0 >= 3.2.6
- gio-2.0
- gthread-2.0
- gnutls >= 3.0
- gpgme >= 1.6.0
- libical >= 2.0.0
- fribidi

`--disable-more-warnings`
	Balsa by default is very sensitive to compilation warnings
which often mean simply programming or configuration errors. If you
are sure this is not the case, or you cannot change your system setup
use this option to compile the code and hope for the best. 
(some Solaris setups require this).

`--with-gnome`
	Add "GNOME;" to Balsa's categories in the two .desktop files.

`--with-libsecret`
	Link to libsecret to store credentials in the Secret Service instead of
the obfuscated text file `~/.balsa/config-private`.  See also section
_Credentials_ below.

`--with-gss[=/usr/kerberos]`
	This enables GSSAPI Kerberos based authentication scheme. 
Specify the kerberos directory as the argument.

`--with-html-widget=(no|webkit2)`
	When using webkit2, in order to quote html-only messages
it is recommended to install a html-to-text conversion tool.  Supported
tools are python-html2text, html2markdown, html2markdown.py2,
html2markdown.py3 and html2text.  Additionally, sqlite3 is required for
managing sender-dependent HTML preferences.

`--with-spell-checker=(internal|gtkspell|gspell)`
	Select the spell checker for the message composer. The internal spell
checker depends on the enchant library (any version except 1.6.1).

`--with-ldap`
        Use ldap libraries for a read-only address book. The read/write
address book is in the works but needs some finishing touches.

`--with-gpe`
	Include support for GPE address books (requires sqlite3).

`--with-osmo`
	Enable experimental support for read-only DBus access to the Osmo
	contacts.  Note that Osmo svn rev. 1099 or later is required.

`--with-canberra`
	Use libcanberra-gtk3 for filter sounds.

`--with-compface`
	Use Compface for rendering X-Face format thumbnails of email
authors in a mail header.

`--with-gtksourceview`
	Use GtkSourceview for highlighting structured phrases in
messages, and for syntax highlighting in attachments.

`--with-gcr`
	Use the GCR library for displaying certificates and crypto UI.

`--enable-autocrypt`
	Build with Autocrypt support to simplify GnuPG key exchange
(see https://autocrypt.org/, requires sqlite3).

`--enable-systray`
	Enable Freedesktop System Tray Icon support (requires libxapp).

`--with-webdav`
	Enable limited support for CardDAV address books (see
README-CardDAV.md, requires libsoup and libxml).

`--disable-nls`
	Do not use Native Language Support (Localization).


## Libraries:

If you use the autotools build system, make sure you have libtool
installed (if you get some error messages during compilation or when
running precompiled binaries saying that libtdl is missing it means you
need to install just that package).


## Balsa GIT:

Balsa is hosted on the Gnome GitLab server. To get the latest
source, get the module 'balsa':
git clone https://gitlab.gnome.org/GNOME/balsa.git


## End-to-End Encryption (E2EE):

Balsa supports E2EE using the multipart OpenPGP (RFC 3156)
or S/MIME (RFC RFC 8551) standards as well as single-part OpenPGP
(RFC 4880).  Messages can be signed, encrypted, or both.  The GpgME
library (https://gnupg.org/software/gpgme/) must be installed.  For
the cryptographic operations, suitable backends like gnupg for the
OpenPGP protocols and/or gpgsm for S/MIME are required.
 
Optionally, Balsa can be configured to include Autocrypt support
(https://autocrypt.org/index.html).


## Specifying the SMTP Server:


Remote SMTP Server:
	Specify the domain name and optionally the port for of the SMTP
	server you use for submitting mail.  Please note that the
	default port number is 587 or 465 for SMTPS (see below).  The
	syntax is hostname[:port].  Port can be a decimal number or the
	name of the service as specified in /etc/services.  Just click
	the *probe...* button to let Balsa detect the best port and
	security (see below) combination.
	If like system is running a local MTA (e.g. Postfix or Exim),
	you can just set this to localhost:25 without encryption..

Security:
	Specify the security level.  For an ISP, this is typically "SMTP
	over SSL (SMTPS)" (default port 465) or "TLS required" (default
	587, but many ISP's listen on port 25).  If your ISP does not
	support either, choose a different ISP.  For a local connection
	(i.e. to localhost), an unencrypted connection is fine.
	Note that Balsa will not use the PLAIN or LOGIN authentication
	mechanisms if the connection is not encrypted.

User:
	If the remote SMTP server requires authentication, enter your
	user name here.  Note that the exact format depends on the MTA
	in use.  For example, some systems expect a user name, others
	may require an email address.

Pass Phrase:
	If the remote SMTP server requires authentication, enter your
	pass phrase here.  Some systems refer to the pass phrase as a
	password.  Limitations on the length of the pass phrase depend
	on the SMTP server.

Client Certificate and Pass Phrase:
	Few ISP's hand over a client certificate Balsa must present when
	connecting.  Choose the PEM-encoded certificate file and -if it
	has an encrypted private key- set the key's pass phrase.

Split large messages:
	Some ISP's impose a message size limit.  In this case, enter the
	appropriate value here.


## Credentials

Balsa uses the desktop environment's Secret Service (using the
org.freedesktop.Secret.Service D-Bus service) to safely store credentials if
support for `libsecret` has been included (see _Configuration_ above).  The
Secret Service is implemented by, inter alia, GNOME keyring, Kwallet and
KeePassXC.

Otherwise, the credentials are stored obfuscated in the file
`~/.balsa/config-private`.  **This method is not recommended, though.**

If a password cannot be loaded from the Secret Service, Balsa tries to read it
from the config file as fallback, and to store it in the Secret Service.  On
success, it is removed from the config file for security.  Note that Balsa will
never _store_ any credentials in the config file unless using the Secret Service
is explicitly disabled.

In the unlikely case of a desktop environment which does not provide any usable
Secret Service D-Bus service, using the config file can be enforced for Balsa
binaries including `libsecret` support by setting the environment variable

	BALSA_DISABLE_LIBSECRET=1


## Gtk+-3 Dialog Header Bars:

If the Gtk+ version is >= 3.12.0, Balsa uses the new Gtk header
bars instead of the traditional action areas.  As this may look ugly when
using other desktop environments than Gnome (e.g. XFCE), Balsa can be
switched to the old style by defining the environment variable

	BALSA_DIALOG_HEADERBAR=0


## Help System:

In order to compile the help files, you need to have the
Mallard documentation system. Very good documentation
can be found at: http://projectmallard.org/


## Balsa as mailto protocol handler:

Balsa can be used as mailto protocol handler; by default, a
desktop file that declares this capability is installed.


## Mailbox locking:

Balsa uses flock+dotfile for mailbox file locking. It does not
use fcntl (although it can be enabled) since this locking method is
very fragile and often not portable (see for example
https://web.pa.msu.edu/reference/pine-tech-notes/low-level.html#locking).

Make sure that your spool directory has drwxrwxrwt (01777) access
privileges. Presently, dotfile locking requires this unconditionally
In the future, we may relax this requirement and will allow you to
shoot yourself in your leg.


## POP3 mail filtering:

When the respective POP3 'mailbox' has the 'filter' box checked, the
downloaded mail is passed on to procmail which will use
~/.procmailrc file as its configuration, so you can share it between
Balsa and fetchmail and get consistent behavior no matter you use
Balsa or fetchmail for downloading.

Simple example ~/.procmailrc file:
```
--------- cut here ----------------
:0H:
* ^Subject:.*balsa
mail/balsa-related-mail
--------- cut here ----------------
```
It is recommended to read procmail(1) and procmailrc(1) for more
real-life examples and syntax explanation.


## Debugging:

Set the environment variable G_MESSAGES_DEBUG to print debugging
information to the console.  The value shall be either a space-
separated list of log domains, or the special value "all".  The
following custom domains are implemented in Balsa:
- libnetclient: low-level network IO.  Warning: the output may contain
        plain-text passwords.
- imap: IMAP server interaction.  Warning: the output may contain
        plain-text passwords.
- crypto: GnuPG and S/MIME crypto operations
- autocrypt: Autocrypt operations
- html: HTML rendering with webkit2
- address-book: address book backend operations (GPE, LDAP, …)
- icons: 
- mbox-imap: IMAP mailbox operations
- mbox-local: local mailbox operations
- mbox-maildir: Maildir mailbox operations
- mbox-mbox: MBox mailbox operations
- mbox-mh: MH mailbox operations
- mbox-pop3: POP3 mailbox operations
- send: message transmission
- spell-check: internal spell checker
- webdav: WebDAV (CardDAV) operation
- dkim: DKIM/DMARC processing (enable DKIM/DMARC checks in 
        *Preferences -> Settings -> Miscellaneous*).


## Reporting Bugs:

To report a bug, please create an issue at
https://gitlab.gnome.org/GNOME/balsa/issues.
Patches are welcome!


## Known issues:

*	When dotlocking is not possible (Wrong access privilieges for
	the mailbox file) Balsa will open mailbox for reading only.
	Verify that Balsa can create dot file in the mailbox directory.
	Recommended access privileges to /var/spool/mail are rwxrwxrwxt (01777)

