/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list
 *
 * Copyright (C) 1999-2016 Brendan Cully <brendan@kublai.com>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, see <https://www.gnu.org/licenses/>.
 */ 

/* IMAP login/authentication code */

#include <string.h>
#include <glib/gi18n.h>

#include "imap-auth.h"
#include "net-client-utils.h"

#include "imap_private.h"

/* imap_auth_cram_md5: AUTH=CRAM-MD5 support. */
ImapResult
imap_auth_cram(ImapMboxHandle* handle)
{
  gchar *challenge;
  gchar *auth_buf;
  unsigned cmdno;
  int rc;
  gchar **auth_data;

  if (!imap_mbox_handle_can_do(handle, IMCAP_ACRAM_MD5))
    return IMAP_AUTH_UNAVAIL;

  g_signal_emit_by_name(handle->sio, "auth", NET_CLIENT_AUTH_USER_PASS, &auth_data);
  if((auth_data == NULL) || (auth_data[0] == NULL) || (auth_data[1] == NULL)) {
    imap_mbox_handle_set_msg(handle, _("Authentication cancelled"));
	g_strfreev(auth_data);
    return IMAP_AUTH_CANCELLED;
  }

  /* start the interaction */
  if(imap_cmd_start(handle, "AUTHENTICATE CRAM-MD5", &cmdno) <0)
    return IMAP_AUTH_FAILURE;

  /* From RFC 2195:
   * The data encoded in the first ready response contains a presumptively
   * arbitrary string of random digits, a timestamp, and the fully-qualified
   * primary host name of the server. The syntax of the unencoded form must
   * correspond to that of an RFC 822 'msg-id' [RFC822] as described in [POP3].
   */
  rc = imap_cmd_process_untagged(handle, cmdno);

  if (rc != IMR_RESPOND) {
    g_warning("cram-md5: unexpected response: %d", rc);
    return IMAP_AUTH_FAILURE;
  }
  challenge = net_client_siobuf_get_line(handle->sio, NULL);
  if (challenge != NULL) {
	  auth_buf = net_client_cram_calc(challenge, G_CHECKSUM_MD5, auth_data[0], auth_data[1]);
  } else {
	  auth_buf = NULL;
  }
  g_free(challenge);
  net_client_free_authstr(auth_data[0]);
  net_client_free_authstr(auth_data[1]);
  g_free(auth_data);
  if (auth_buf == NULL) {
	  return IMAP_AUTH_FAILURE;
  }

  net_client_write_line(NET_CLIENT(handle->sio), "%s", NULL, auth_buf);
  net_client_free_authstr(auth_buf);
  rc = imap_cmd_process_untagged(handle, cmdno);

  return rc == IMR_OK ? IMAP_SUCCESS : IMAP_AUTH_FAILURE;
}
