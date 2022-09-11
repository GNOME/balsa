/* libimap library.
 * Copyright (C) 2003-2016 Pawel Salek.
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
/* imap_authenticate: Attempt to authenticate using either user-specified
 *   authentication method if specified, or any.
 * returns 0 on success */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <glib/gi18n.h>

#include "imap-handle.h"
#include "imap-auth.h"
#include "util.h"
#include "imap_private.h"
#include "siobuf-nc.h"
#include "net-client-utils.h"

static ImapResult imap_auth_anonymous(ImapMboxHandle* handle);
static ImapResult imap_auth_plain(ImapMboxHandle* handle);
static ImapResult imap_auth_login(ImapMboxHandle* handle);

typedef ImapResult (*ImapAuthenticator)(ImapMboxHandle* handle);

/* User name/password methods, ordered from strongest to weakest. */
static ImapAuthenticator imap_authenticators_arr[] = {
  imap_auth_cram,
  imap_auth_plain,
  imap_auth_login, /* login is deprecated */
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

  if ((handle->auth_mode & NET_CLIENT_AUTH_KERBEROS) != 0U) {
	  r = imap_auth_gssapi(handle);
  } else if ((handle->auth_mode & NET_CLIENT_AUTH_NONE_ANON) != 0U) {
	  r = imap_auth_anonymous(handle);
  } else {
	  for (authenticator = imap_authenticators_arr; (r != IMAP_SUCCESS) && *authenticator; authenticator++) {
		  r = (*authenticator)(handle);
	  }
  }

  if (r == IMAP_SUCCESS) {
	  imap_mbox_handle_set_state(handle, IMHS_AUTHENTICATED);
  } else if (r == IMAP_AUTH_UNAVAIL) {
	  imap_mbox_handle_set_msg(handle, _("No way to authenticate is known"));
  }
  return r;
}

/* =================================================================== */
/*                           AUTHENTICATORS                            */
/* =================================================================== */
/* imap_auth_login: Plain LOGIN support */
static ImapResult
imap_auth_login(ImapMboxHandle* handle)
{
  gchar **auth_data;
  ImapResult result;
  
  if (imap_mbox_handle_can_do(handle, IMCAP_LOGINDISABLED))
    return IMAP_AUTH_UNAVAIL;
  
  g_signal_emit_by_name(handle->sio, "auth", NET_CLIENT_AUTH_USER_PASS, &auth_data);
  if((auth_data == NULL) || (auth_data[0] == NULL) || (auth_data[1] == NULL)) {
    imap_mbox_handle_set_msg(handle, _("Authentication cancelled"));
	g_strfreev(auth_data);
    return IMAP_AUTH_CANCELLED;
  }

  /* RFC 6855, Sect. 5, explicitly forbids UTF-8 usernames or passwords */
  if (!g_str_is_ascii(auth_data[0]) || !g_str_is_ascii(auth_data[1])) {
	  imap_mbox_handle_set_msg(handle, _("Cannot LOGIN with UTF-8 username or password"));
	  result = IMAP_AUTH_CANCELLED;
  } else {
	  gchar *q_user;
	  gchar *q_pass;
	  gchar *buf;
	  ImapResponse rc;

	  q_user = imap_quote_string(auth_data[0]);
	  q_pass = imap_quote_string(auth_data[1]);
	  buf = g_strjoin(" ", "LOGIN", q_user, q_pass, NULL);
	  net_client_free_authstr(q_user);
	  net_client_free_authstr(q_pass);
	  rc = imap_cmd_exec(handle, buf);
	  net_client_free_authstr(buf);

	  result = (rc == IMR_OK) ? IMAP_SUCCESS : IMAP_AUTH_FAILURE;
  }

  net_client_free_authstr(auth_data[0]);
  net_client_free_authstr(auth_data[1]);
  g_free(auth_data);
  return result;
}

/* =================================================================== */
/* SASL PLAIN RFC-2595                                                 */
/* =================================================================== */
static ImapResult
imap_auth_sasl(ImapMboxHandle* handle, ImapCapability cap,
	       const char *sasl_cmd,
	       gboolean (*getmsg)(ImapMboxHandle *h, char **msg, int *msglen))
{
  char *msg64;
  ImapResponse rc;
  int msglen;
  unsigned cmdno;
  gboolean sasl_ir;
  
  if (!imap_mbox_handle_can_do(handle, cap))
    return IMAP_AUTH_UNAVAIL;
  sasl_ir = imap_mbox_handle_can_do(handle, IMCAP_SASLIR);
  
  if(!getmsg(handle, &msg64, &msglen)) {
    imap_mbox_handle_set_msg(handle, _("Authentication cancelled"));
    return IMAP_AUTH_CANCELLED;
  }
  
  if(sasl_ir) { /* save one RTT */
    ImapCmdTag tag;
    if(IMAP_MBOX_IS_DISCONNECTED(handle))
      return IMAP_AUTH_UNAVAIL;
    cmdno = imap_make_tag(tag);
    net_client_write_line(NET_CLIENT(handle->sio), "%s %s %s",
    	NULL, tag, sasl_cmd, msg64);
  } else {
    if(imap_cmd_start(handle, sasl_cmd, &cmdno) <0) {
      net_client_free_authstr(msg64);
      return IMAP_AUTH_FAILURE;
    }
    rc = imap_cmd_process_untagged(handle, cmdno);
    
    if (rc != IMR_RESPOND) {
      g_warning("imap %s: unexpected response.", sasl_cmd);
      net_client_free_authstr(msg64);
      return IMAP_AUTH_FAILURE;
    }
    net_client_siobuf_discard_line(handle->sio, NULL);
    net_client_write_line(NET_CLIENT(handle->sio), "%s", NULL, msg64);
  }
  net_client_free_authstr(msg64);
  rc = imap_cmd_process_untagged(handle, cmdno);
  return  (rc== IMR_OK) ?  IMAP_SUCCESS : IMAP_AUTH_FAILURE;
}

static gboolean
getmsg_plain(ImapMboxHandle *h, char **retmsg, int *retmsglen)
{
	gchar **auth_data;
	gboolean result;

	g_signal_emit_by_name(h->sio, "auth", NET_CLIENT_AUTH_USER_PASS, &auth_data);
	if ((auth_data == NULL) || (auth_data[0] == NULL) || (auth_data[1] == NULL)) {
		result = FALSE;
	} else {
		*retmsg = net_client_auth_plain_calc(auth_data[0], auth_data[1]);
		*retmsglen = strlen(*retmsg);
		result = TRUE;
	}
	if (auth_data != NULL) {
		net_client_free_authstr(auth_data[0]);
		net_client_free_authstr(auth_data[1]);
		g_free(auth_data);
	}
	return result;
}

static ImapResult
imap_auth_plain(ImapMboxHandle* handle)
{
  return imap_auth_sasl(handle, IMCAP_APLAIN, "AUTHENTICATE PLAIN",
			getmsg_plain);
}


/* =================================================================== */
/* SASL ANONYMOUS RFC-4505                                             */
/* =================================================================== */
static gboolean
getmsg_anonymous(ImapMboxHandle *h, char **retmsg, int *retmsglen)
{
	*retmsg = net_client_auth_anonymous_token();
	*retmsglen = strlen(*retmsg);
	return TRUE;
}

static ImapResult
imap_auth_anonymous(ImapMboxHandle* handle)
{
  return imap_auth_sasl(handle, IMCAP_AANONYMOUS, "AUTHENTICATE ANONYMOUS",
			getmsg_anonymous);
}

