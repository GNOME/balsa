/* libimap library.
 * Copyright (C) 2003-2004 Pawel Salek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */
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
static ImapAuthenticator imap_authenticators_arr[] = {
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
  imap_mbox_handle_set_msg(handle, "No way to authenticate is known");
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
    handle->user_cb(IME_GET_USER_PASS, handle->user_arg,
                    "LOGIN", &user, &pass, &ok);
  if(!ok || user == NULL || pass == NULL) {
    imap_mbox_handle_set_msg(handle, "Authentication cancelled");
    return IMAP_AUTH_FAILURE;
  }

  imap_quote_string(q_user, sizeof (q_user), user);
  imap_quote_string(q_pass, sizeof (q_pass), pass);
  g_free(user); g_free(pass); /* FIXME: clean passwd first */
  g_snprintf (buf, sizeof (buf), "LOGIN %s %s", q_user, q_pass);
  rc = imap_cmd_exec(handle, buf);
  return  (rc== IMR_OK) ?  IMAP_SUCCESS : IMAP_AUTH_FAILURE;
}

