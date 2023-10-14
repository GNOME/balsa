# CardDAV support in Balsa

Balsa can be configured to include limited support for remote CardDAV ([RFC 6352](https://www.rfc-editor.org/rfc/rfc6352))
address books:
* it is possible to read the address book and to *add* new items, but not to *modify* or *delete* entries;
* the remote server *must* support Basic authentication.  Note that this requirement excludes inter alia GMail
  which requires OAuth authentication.

CardDAV support in Balsa requires `libsoup` (version 2.4 or 3.0, which is also a dependency of HTML supprt)
and `libxml-2.0`.  Configure Balsa with `--with-webdav` to enable it.

## Supported features

The implementation in Balsa supports the following features:
* [RFC 6764](https://www.rfc-editor.org/rfc/rfc6764) auto-detection, not supported by all servers, sometimes broken;
* HTTP Basic authentication.  The credentials are stored in the *Secret Service* if balsa has been configured to support
  it (recommended);
* automatic detection of address books;
* reading address data using *addressbook-query* or *addressbook-multiget* reports;
* VCard 3.0 and 4.0 address book items;
* filtering *addressbook-query* for VCard's containing an email address (RFC 6352, sect. 10.5), and limitation of returned
  VCard attributes for both *addressbook-query* and *addressbook-multiget* reports (RFC 6352, sect. 10.4.2) in order to
  reduce bandwith (not supported by most servers);
* detection of remote address book changes using *sync-token* ([RFC 6578](https://www.rfc-editor.org/rfc/rfc6578)) or
  *CTag* (“[CTag in CalDAV](https://github.com/apple/ccs-calendarserver/blob/master/doc/Extensions/caldav-ctag.txt)”),
  both not supported by all servers;

## Configuration

The configuration of a CardDAV address book requires two steps, as after the first one Balsa tries to retrieve more
information from the remote server.

1. In the first step, fill in the *Domain or URL*, *User Name* and *Pass Phrase* entries.  If the remote server supports
   DNS SRV service labels (RFC 6764, sect. 3), it is sufficient to enter the domain name for the first item.  If it
   implements Well-Known URI support (RFC 6764, sect. 5), enter the `https://` URI of the CardDAV server *without a path*.
   If it doesn't support either, enter full URI of the CaddDAV service.  
   Then click *probe…* to check the settings.
2. Balsa reads the list of address books available on the remote server.  Select the proper one from the
   *CardDAV address book name* combo box.
3. As reading the addresses from the remote server may be slow and they typically change rarely, Balsa caches them locally.
   The period for re-checking for changes is configurable; if supported by the remote server, Balsa will use the *sync-token*
   or *CTag* to detect changes which is a lot cheaper than always reading the whole address book.  
   The *addressbook-query* report seems to be broken on some servers (which is a violation of RFC 6352).  If you know your
   CardDAV address book contains items, but none appears in Balsa, check *Force Multiget for non-standard server* which tries
   to work around these bugs.

## Debug output

Calling `balsa` with the environment variable `G_MESSAGES_DEBUG` including `webdav` or `all` prints debug messages about the
WebDAV/CardDAV operation.

## Tested Configurations

The following sections include a few tested configurations (mostly German providers).

### Apple iCloud

Enter `icould.com` as *Domain or URL*, the email address as *User Name*, and the app-specific password
as *Pass Phrase*.

Notes:
* DNS SRV service label auto-detection is half-broken as no context path (RFC 6764, sect. 4) is provided and the
  Well-Known URI request fails.  Balsa contains code to work around this bug.
* Although required by RFC 6352, the server does not support *addressbook-query* reports, i.e. Balsa always must use
  the more costly *addressbook-multiget* report.
* Supports *sync-token* and *CTag* synchronisation.
* VCard attribute limitation does not work.

### Posteo

Enter `posteo.de` as *Domain or URL*, the email address as *User Name*, and the usual password as *Pass Phrase*.

Notes:
* Fully functional DNS SRV service label auto-detection.
* Supports *CTag* synchronisation.
* The server is rather slow.
* *addressbook-query* filtering works, VCard attribute limitation doesn't. 

### Deutsche Telekom

Enter the URL `https://spica.t-online.de` as *Domain or URL*, the email address as *User Name*, and the usual
password as *Pass Phrase*.  As the server is broken (see below), the option *Force Multiget for non-standard server*
must be activated.

Notes:
* Well-Known URI detection is supported.
* The server claims to support *addressbook-query*, but doesn't return anything.  However, *addressbook-multiget*
  works just fine.
* Only `t-online.de` accounts have been tested, but `magenta.de` *should* also work.
* VCard attribute limitation does not work.

### Arcor/Vodafone

Enter the URL `https://webdav.vodafonemail.de/carddav` as *Domain or URL*, the email address as *User Name*, and the usual
password as *Pass Phrase*.

Notes:
* A Well-Known URI access (i.e. `https://webdav.vodafonemail.de` as *Domain or URL*) returns a `301: Moved Permanently`
  response, but without giving the real target, i.e. the implementation is broken.
* Only (very old) `arcor.de` accounts have been tested, other Vodafone ones *may* also work.
* *addressbook-query* is supported, but neither filtering nor VCard attribute limitation work.
* The server does not support *sync-token* or *CTag* synchronisation, i.e. every update reads the complete address book.

### Freenet

Enter `freenet.de` as *Domain or URL*, the email address as *User Name*, and the usual password as *Pass Phrase*.

Notes:
* Fully functional DNS SRV service label auto-detection.
* Supports *CTag* synchronisation.
* The server is rather slow.
* *addressbook-query* filtering and VCard attribute limitation work. 

