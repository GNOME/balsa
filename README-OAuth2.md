# OAuth2 support in Balsa

Balsa can be configured to include support for OAuth2 authentication ([RFC 6749](https://datatracker.ietf.org/doc/html/rfc6749),
[RFC 6750](https://datatracker.ietf.org/doc/html/rfc6750)) which is used by some providers, including inter alia Microsoft, GMail
and Yahoo.  It requires `libsoup` (version 2.4 or 3.0, which is also a dependency of HTML supprt) and `libjson-glib`.  Configure
Balsa with `--with-oauth2` to enable it.

Note that OAuth2 is supported for email (i.e. POP3, IMAP and SMTP) only, *not* for CardDAV address books.

## Provider Configuration Files

For each provider, a different configuration is required which is read from configuration files named `balsa-oauth2.cfg`.  Upon
startup, Balsa loads this file from the following folders:
1. in the sub-folder `balsa` of all folders defined in the environment variable `XDG_DATA_DIRS` (i.e. typically `/usr/share/balsa`,
   `/usr/local/share/balsa`, etc.),
2. in the sub-folder `balsa` of the user's configuration folder defined in the environment variable `XDG_CONFIG_HOME` (usually
   `$HOME/.config`), and
3. in the folder from which Balsa is launched.

Note that if a configuration for the same provider is included in more than one file, the last definition read is used.

Balsa ships with a configuration file for Microsoft *outlook.com* freemail configuration.  These parameters are registered with Microsoft, but not trusted fully due to limitations of the registration process.  As a result, you will be asked every few
weeks/months to re-authorise Balsa.  Sorry for the inconvenience…

The configuration file follows the `GKeyFile` format.  Its consists of a separate section for each provider, e.g.

```
[Microsoft Freemail]
email_re	= ^.*@(outlook|hotmail)(\.com)?\.[a-z]{2,3}$
client_id	= 41c99e15-0609-40ed-90c4-64c613e19e90
# client_secret	= none, not used by Microsoft
auth_uri	= https://login.microsoftonline.com/common/oauth2/v2.0/authorize
token_uri	= https://login.microsoftonline.com/common/oauth2/v2.0/token
scope		= https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/POP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access
oob_mode	= false
```

* the section identifier **MUST** be a distinct, human-readable name identifying the provider
* `email_re` (string): A Regular Expression matching email addresses of the provider – this item is used to select the proper one
  for the user's email address.
* `client_id` (string): The Balsa Client ID assigned by the ISP.
* `client_secret` (string): The Balsa Client Secret assigned by the ISP.  As some ISP's do not use client secrets, this item is
  optional.
* `auth_uri` (string): The ISP's authentication URI.
* `token_uri` (string):  The ISP's URI to open for receiving the access token.
* `scope` (string):         The requested OAuth2 scope or space-separated scopes.  As some ISP's do not use the scope, this item is
  optional.
* `oob_mode` (boolean): True if the ISP cannot redirect to an arbitrary URI with port; optional, default False

## Debugging

Calling `balsa` with the environment variable `G_MESSAGES_DEBUG` including `oauth` or `all` prints debug messages about the OAuth2
operation.
