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
 *     along with this program; if not, see <http://www.gnu.org/licenses/>.
 */ 

/* IMAP login/authentication code */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "imap-auth.h"
#include "net-client-utils.h"

#include "imap_private.h"

#define LONG_STRING 1024

/* imap_auth_cram_md5: AUTH=CRAM-MD5 support. */
ImapResult
imap_auth_cram(ImapMboxHandle* handle)
{
  char ibuf[LONG_STRING*2];
  gchar *auth_buf;
  unsigned cmdno;
  int rc, ok;
  char *user = NULL, *pass = NULL;

  if (!imap_mbox_handle_can_do(handle, IMCAP_ACRAM_MD5))
    return IMAP_AUTH_UNAVAIL;

  ok = 0;
  if(!ok && handle->user_cb)
    handle->user_cb(IME_GET_USER_PASS, handle->user_arg, 
                    "CRAM-MD5", &user, &pass, &ok);
  if(!ok || user == NULL || pass == NULL) {
    imap_mbox_handle_set_msg(handle, "Authentication cancelled");
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
  imap_handle_flush(handle);
  do
    rc = imap_cmd_step(handle, cmdno);
  while(rc == IMR_UNTAGGED);

  if (rc != IMR_RESPOND) {
    g_warning("cram-md5: unexpected response:\n");
    return IMAP_AUTH_FAILURE;
  }
  imap_mbox_gets(handle, ibuf, sizeof(ibuf)); /* check error */
  auth_buf = net_client_cram_calc(ibuf, G_CHECKSUM_MD5, user, pass);
  g_free(user);
  memset(pass, 0, strlen(pass));
  g_free(pass);

  imap_handle_write(handle, auth_buf, strlen(auth_buf));
  imap_handle_write(handle, "\r\n", 2U);
  imap_handle_flush(handle);
  memset(auth_buf, 0, strlen(auth_buf));
  g_free(auth_buf);
  do
    rc = imap_cmd_step (handle, cmdno);
  while (rc == IMR_UNTAGGED);

  return rc == IMR_OK ? IMAP_SUCCESS : IMAP_AUTH_FAILURE;
}
