/* imap_authenticate: Attempt to authenticate using either user-specified
 *   authentication method if specified, or any.
 * returns 0 on success */

#include "config.h"

#include <stdio.h>

#include "imap-handle.h"
#include "imap-auth.h"
#include "util.h"
#include "imap_private.h"

/* ordered from strongest to weakest */
ImapAuthenticator imap_authenticators_arr[] = {
  imap_auth_cram,
  imap_auth_login,
  NULL
};

ImapResult
imap_authenticate(ImapMboxHandle* handle)
{
  ImapAuthenticator* authenticator;
  ImapResult r = IMAP_AUTH_UNAVAIL;

  g_return_val_if_fail(handle, IMAP_AUTH_UNAVAIL);

  if (imap_mbox_is_authenticated(handle) || imap_mbox_is_selected(handle))
    return IMAP_SUCCESS;

  for(authenticator = imap_authenticators_arr;
      *authenticator; authenticator++) {
    if ((r = (*authenticator)(handle)) 
        != IMAP_AUTH_UNAVAIL) {
      if (r == IMAP_SUCCESS)
	imap_mbox_handle_set_state(handle, IMHS_AUTHENTICATED);
      return r;
    }
  }
  return r;
}

/* =================================================================== */
/*                           AUTHENTICATORS                            */
/* =================================================================== */
#define SHORT_STRING 64
/* imap_auth_login: Plain LOGIN support */
ImapResult
imap_auth_login(ImapMboxHandle* handle)
{
  char q_user[SHORT_STRING], q_pass[SHORT_STRING];
  char buf[2*SHORT_STRING+7];
  char *user = NULL, *pass = NULL;
  ImapResponse rc;
  int ok;
  
  if (imap_mbox_handle_can_do(handle, IMCAP_LOGINDISABLED))
    return IMAP_AUTH_UNAVAIL;
  
  ok = 0;
  if(!ok && handle->user_cb)
    handle->user_cb(handle, IME_GET_USER_PASS, handle->user_arg,
                    "LOGIN", &user, &pass, &ok);
  if(!ok || user == NULL || pass == NULL)
    return IMAP_AUTH_FAILURE;

  imap_quote_string(q_user, sizeof (q_user), user);
  imap_quote_string(q_pass, sizeof (q_pass), pass);
  g_free(user); g_free(pass); /* FIXME: clean passwd first */
  g_snprintf (buf, sizeof (buf), "LOGIN %s %s", q_user, q_pass);
  rc = imap_cmd_exec(handle, buf);
  return  (rc== IMR_OK) ?  IMAP_SUCCESS : IMAP_AUTH_FAILURE;
}

