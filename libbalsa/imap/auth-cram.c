/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list
 *
 * Copyright (C) 1999-2000 Brendan Cully <brendan@kublai.com>
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
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

/* IMAP login/authentication code */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "imap-auth.h"
#include "md5-utils.h"
#include "util.h"

#include "imap_private.h"

#define LONG_STRING 1024

#define MD5_DIGEST_LEN 16

/* forward declarations */
static void hmac_md5(const char* password, char* challenge,
                     unsigned char* response);

/* imap_auth_cram_md5: AUTH=CRAM-MD5 support. */
ImapResult
imap_auth_cram(ImapMboxHandle* handle)
{
  char ibuf[LONG_STRING*2], obuf[LONG_STRING];
  unsigned char hmac_response[MD5_DIGEST_LEN];
  unsigned cmdno;
  int len, rc, ok;
  char *user = NULL, *pass = NULL;

  if (!imap_mbox_handle_can_do(handle, IMCAP_ACRAM_MD5))
    return IMAP_AUTH_UNAVAIL;

  ok = 0;
  if(!ok && handle->user_cb)
    handle->user_cb(IME_GET_USER_PASS, handle->user_arg, 
                    "CRAM-MD5", &user, &pass, &ok);
  if(!ok || user == NULL || pass == NULL) {
    imap_mbox_handle_set_msg(handle, "Authentication cancelled");
    return IMAP_AUTH_FAILURE;
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
  if ((len = lit_conv_from_base64(obuf, ibuf)) <0) {
    g_warning("Error decoding base64 response(%s), digit=%d:%d[%c]).\n", 
	      ibuf, len, ibuf[-len-1],ibuf[-len-1]);
    return IMAP_AUTH_FAILURE;
  }

  obuf[len] = '\0';

  /* The client makes note of the data and then responds with a string
   * consisting of the user name, a space, and a 'digest'. The latter is
   * computed by applying the keyed MD5 algorithm from [KEYED-MD5] where the
   * key is a shared secret and the digested text is the timestamp (including
   * angle-brackets).
   * 
   * Note: The user name shouldn't be quoted. Since the digest can't contain
   *   spaces, there is no ambiguity. Some servers get this wrong, we'll work
   *   around them when the bug report comes in. Until then, we'll remain
   *   blissfully RFC-compliant.
   */
  hmac_md5 (pass, obuf, hmac_response);
  g_snprintf (obuf, sizeof (obuf),
    "%s %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
    user,
    hmac_response[0], hmac_response[1], hmac_response[2], hmac_response[3],
    hmac_response[4], hmac_response[5], hmac_response[6], hmac_response[7],
    hmac_response[8], hmac_response[9], hmac_response[10], hmac_response[11],
    hmac_response[12], hmac_response[13], hmac_response[14], hmac_response[15]);
  /* XXX - ibuf must be long enough to store the base64 encoding of obuf, 
   * plus the additional debris
   */
  
  lit_conv_to_base64(ibuf, obuf, strlen (obuf), sizeof(ibuf)-2);
  strncat (ibuf, "\r\n", sizeof (ibuf));
  imap_handle_write(handle, ibuf, strlen(ibuf));
  imap_handle_flush(handle);
  g_free(user); g_free(pass); /* FIXME: clean passwd first */
  do
    rc = imap_cmd_step (handle, cmdno);
  while (rc == IMR_UNTAGGED);

  return rc == IMR_OK ? IMAP_SUCCESS : IMAP_AUTH_FAILURE;
}

/* hmac_md5: produce CRAM-MD5 challenge response. */
#define MD5_BLOCK_LEN  64
static void
hmac_md5 (const char* password, char* challenge,
          unsigned char* response)
{  
  MD5Context ctx;
  unsigned char ipad[MD5_BLOCK_LEN], opad[MD5_BLOCK_LEN];
  unsigned char secret[MD5_BLOCK_LEN+1];
  unsigned char hash_passwd[MD5_DIGEST_LEN];
  unsigned int secret_len, chal_len;
  int i;

  secret_len = strlen(password);
  chal_len = strlen(challenge);

  /* passwords longer than MD5_BLOCK_LEN bytes are substituted with their MD5
   * digests */
  if (secret_len > MD5_BLOCK_LEN) {
    md5_init (&ctx);
    md5_update (&ctx, (unsigned char*) password, secret_len);
    md5_final (&ctx, hash_passwd);
    strncpy ((char*) secret, (char*) hash_passwd, MD5_DIGEST_LEN);
    secret_len = MD5_DIGEST_LEN;
  }
  else
    strncpy ((char *) secret, password, sizeof (secret));

  memset (ipad, 0, sizeof(ipad));
  memset (opad, 0, sizeof(opad));
  memcpy (ipad, secret, secret_len);
  memcpy (opad, secret, secret_len);

  for (i=0; i<MD5_BLOCK_LEN; i++) {
    ipad[i] ^= 0x36;
    opad[i] ^= 0x5c;
  }

  /* inner hash: challenge and ipadded secret */
  md5_init (&ctx);
  md5_update (&ctx, ipad, MD5_BLOCK_LEN);
  md5_update (&ctx, (unsigned char*) challenge, chal_len);
  md5_final (&ctx, response);

  /* outer hash: inner hash and opadded secret */
  md5_init (&ctx);
  md5_update (&ctx, opad, MD5_BLOCK_LEN);
  md5_update (&ctx, response, MD5_DIGEST_LEN);
  md5_final (&ctx, response);
}
