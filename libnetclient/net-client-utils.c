/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2017
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdio.h>
#include "net-client-utils.h"

gchar *
net_client_cram_calc(const gchar *base64_challenge, GChecksumType chksum_type, const gchar *user, const gchar *passwd)
{
	guchar *chal_plain;
	gsize plain_len;
	gchar *digest;
	gchar *auth_buf;
	gchar *base64_buf;

	g_return_val_if_fail((base64_challenge != NULL) && (user != NULL) && (passwd != NULL), NULL);

	chal_plain = g_base64_decode(base64_challenge, &plain_len);
	digest = g_compute_hmac_for_data(chksum_type, (const guchar *) passwd, strlen(passwd), chal_plain, plain_len);
	memset(chal_plain, 0, plain_len);
	g_free(chal_plain);

	auth_buf = g_strdup_printf("%s %s", user, digest);
	memset(digest, 0, strlen(digest));
	g_free(digest);

	base64_buf = g_base64_encode((const guchar *) auth_buf, strlen(auth_buf));
	memset(auth_buf, 0, strlen(auth_buf));
	g_free(auth_buf);

	return base64_buf;
}


const gchar *
net_client_chksum_to_str(GChecksumType chksum_type)
{
	/*lint -e{904} -e{9077} -e{9090}	(MISRA C:2012 Rules 15.5, 16.1, 16.3) */
	switch (chksum_type) {
	case G_CHECKSUM_MD5:
		return "MD5";
	case G_CHECKSUM_SHA1:
		return "SHA1";
	case G_CHECKSUM_SHA256:
		return "SHA256";
	case G_CHECKSUM_SHA512:
		return "SHA512";
	default:
		return "_UNKNOWN_";
	}
}


gchar *
net_client_auth_plain_calc(const gchar *user, const gchar *passwd)
{
	gchar *base64_buf;
	gchar *plain_buf;
	size_t user_len;
	size_t passwd_len;

	g_return_val_if_fail((user != NULL) && (passwd != NULL), NULL);

	user_len = strlen(user);
	passwd_len = strlen(passwd);
	plain_buf = g_malloc0((2U * user_len) + passwd_len + 3U);		/*lint !e9079 (MISRA C:2012 Rule 11.5) */
	strcpy(plain_buf, user);
	strcpy(&plain_buf[user_len + 1U], user);
	strcpy(&plain_buf[(2U * user_len) + 2U], passwd);
	base64_buf = g_base64_encode((const guchar *) plain_buf, (2U * user_len) + passwd_len + 2U);
	memset(plain_buf, 0, (2U * user_len) + passwd_len + 2U);
	g_free(plain_buf);

	return base64_buf;
}
