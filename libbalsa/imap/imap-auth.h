#ifndef __IMAP_AUTH_H__
#define __IMAP_AUTH_H__ 1

#include "imap.h"
#include "imap-handle.h"

typedef ImapResult (*ImapAuthenticator)(ImapMboxHandle* handle);
ImapResult imap_authenticate(ImapMboxHandle* handle);
ImapResult imap_auth_cram(ImapMboxHandle* handle);
ImapResult imap_auth_login(ImapMboxHandle* handle);

#endif
