#ifndef __IMAP_AUTH_H__
#define __IMAP_AUTH_H__ 1

#include "imap.h"
#include "imap-handle.h"

typedef ImapResult (*ImapAuthenticator)(ImapMboxHandle* handle, 
                                        const char* user, const char* pass);
ImapResult imap_authenticate(ImapMboxHandle* handle, 
                             const char* user, const char* pass);
ImapResult imap_auth_cram(ImapMboxHandle* handle, 
                          const char* user, const char* pass);
ImapResult imap_auth_login(ImapMboxHandle* handle, 
                           const char* user, const char* pass);

#endif
