/* imap_authenticate: Attempt to authenticate using either user-specified
 *   authentication method if specified, or any.
 * returns 0 on success */

#include "config.h"

#include <stdio.h>

#include "imap-handle.h"
#include "imap-auth.h"

/* ordered from strongest to weakest */
ImapAuthenticator imap_authenticators_arr[] = {
  imap_auth_cram,
  imap_auth_login,
  NULL
};

ImapResult
imap_authenticate(ImapMboxHandle* handle, const char* user, const char* pass)
{
  ImapAuthenticator* authenticator;
  ImapResult r = IMAP_AUTH_UNAVAIL;

  g_return_val_if_fail(handle, IMAP_AUTH_UNAVAIL);

  for(authenticator = imap_authenticators_arr;
      *authenticator; authenticator++) {
    if ((r = (*authenticator)(handle, user, pass)) 
        != IMAP_AUTH_UNAVAIL)
      return r;
  }
  return r;
}

/* =================================================================== */
/*                           AUTHENTICATORS                            */
/* =================================================================== */
#define SHORT_STRING 64
/* imap_auth_login: Plain LOGIN support */
ImapResult
imap_auth_login(ImapMboxHandle* handle, const char* user, const char* pass)
{
  char q_user[SHORT_STRING], q_pass[SHORT_STRING];
  char buf[2*SHORT_STRING+7];
  int rc;
  
  if (imap_mbox_handle_can_do(handle, IMCAP_LOGINDISABLED))
    return IMAP_AUTH_UNAVAIL;
  
  if(user == NULL || pass == NULL)
    return IMAP_AUTH_FAILURE;

  /* DEBUG ("Logging in..."); */

  imap_quote_string(q_user, sizeof (q_user), user);
  imap_quote_string(q_pass, sizeof (q_pass), pass);

  snprintf (buf, sizeof (buf), "LOGIN %s %s", q_user, q_pass);
  rc = imap_cmd_exec(handle, buf);
  return  (rc== IMR_OK) ?  IMAP_SUCCESS : IMAP_AUTH_FAILURE;
}

