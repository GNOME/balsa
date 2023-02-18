/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2017 - 2020
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library. If not, see
 * <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <glib/gi18n.h>
#include "net-client.h"
#include "net-client-utils.h"

#if defined(HAVE_GSSAPI)
# if defined(HAVE_HEIMDAL)
#  include <gssapi.h>
# else
#  include <gssapi/gssapi.h>
# endif
#endif		/* HAVE_GSSAPI */


gboolean
net_client_host_reachable(const gchar *host, GError **error)
{
	GSocketConnectable *remote_address;
	GNetworkMonitor *monitor;
	gboolean success;

	g_return_val_if_fail(host != NULL, FALSE);

	remote_address = g_network_address_new(host, 1024U);
	monitor = g_network_monitor_get_default();
	success = g_network_monitor_can_reach(monitor, remote_address, NULL, error);
	g_object_unref(remote_address);

	return success;
}


#if defined(HAVE_GSSAPI)

struct _NetClientGssCtx {
	gchar *user;
	gss_ctx_id_t context;
    gss_name_t target_name;
    OM_uint32 req_flags;
};


static gchar *gss_error_string(OM_uint32 err_maj, OM_uint32 err_min)
	G_GNUC_WARN_UNUSED_RESULT;

#endif		/* HAVE_GSSAPI */


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
	net_client_free_authstr((gchar *) chal_plain);

	auth_buf = g_strdup_printf("%s %s", user, digest);
	net_client_free_authstr(digest);

	base64_buf = g_base64_encode((const guchar *) auth_buf, strlen(auth_buf));
	net_client_free_authstr(auth_buf);

	return base64_buf;
}


const gchar *
net_client_chksum_to_str(GChecksumType chksum_type)
{
	/*lint -e{904} -e{9077} -e{9090} -e{788}	(MISRA C:2012 Rules 15.5, 16.1, 16.3) */
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
	/* contains \0 chars, cannot use net_client_free_authstr */
	memset(plain_buf, 0, (2U * user_len) + passwd_len + 2U);
	g_free(plain_buf);

	return base64_buf;
}


gchar *
net_client_auth_anonymous_token(void)
{
	gchar *buffer;
	GChecksum *hash;
	const gchar *hash_str;

	buffer = g_strdup_printf("%s@%s:%ld", g_get_user_name(), g_get_host_name(), (long) time(NULL));
	hash = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(hash, (const guchar *) buffer, strlen(buffer));
	g_free(buffer);

	hash_str = g_checksum_get_string(hash);
	buffer = g_base64_encode((const guchar *) hash_str, strlen(hash_str));
	g_checksum_free(hash);
	return buffer;
}


gchar *
net_client_host_only(const gchar *host_and_port)
{
	gchar *result;
	gchar *colon;

	g_return_val_if_fail(host_and_port != NULL, NULL);
	result = g_strdup(host_and_port);
	colon = strchr(result, ':');
	if (colon != NULL) {
		colon[0] = '\0';
	}
	return result;
}


void
net_client_free_authstr(gchar *str)
{
	if (str != NULL) {
		guint n;

		for (n = 0; str[n] != '\0'; n++) {
			str[n] = (gchar) g_random_int_range(32, 128);
		}
		g_free(str);
	}
}


#if defined(HAVE_GSSAPI)

NetClientGssCtx *
net_client_gss_ctx_new(const gchar *service, const gchar *host, const gchar *user, GError **error)
{
	NetClientGssCtx *gss_ctx;
	gchar *service_str;
	gchar *colon;
    gss_buffer_desc request;
    OM_uint32 maj_stat;
    OM_uint32 min_stat;

    g_return_val_if_fail((service != NULL) && (host != NULL) && (user != NULL), NULL);

	gss_ctx = g_new0(NetClientGssCtx, 1U);
	service_str = g_strconcat(service, "@", host, NULL);
	colon = strchr(service_str, ':');		/*lint !e9034   accept char literal as int */
	if (colon != NULL) {
		colon[0] = '\0';		/* strip off any port specification */
	}
	request.value = service_str;
	request.length = strlen(service_str) + 1U;
	maj_stat = gss_import_name(&min_stat, &request, GSS_C_NT_HOSTBASED_SERVICE, &gss_ctx->target_name);
    if (GSS_ERROR(maj_stat) != 0U) {
    	gchar *gss_err = gss_error_string(maj_stat, min_stat);

    	g_debug("gss_import_name: %x:%x: %s", maj_stat, min_stat, gss_err);
    	g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GSSAPI, _("importing GSS service name %s failed: %s"),
    		service_str, gss_err);
    	g_free(gss_err);
    	g_free(gss_ctx);
    	gss_ctx = NULL;
    } else {
    	/* configure the context according to RFC 4752, Sect. 3.1 */
    	gss_ctx->req_flags = GSS_C_INTEG_FLAG + GSS_C_MUTUAL_FLAG + GSS_C_SEQUENCE_FLAG + GSS_C_CONF_FLAG;
    	gss_ctx->user = g_strdup(user);
    }

	g_free(service_str);
    return gss_ctx;
}


gint
net_client_gss_auth_step(NetClientGssCtx *gss_ctx, const gchar *in_token, gchar **out_token, GError **error)
{
    OM_uint32 maj_stat;
    OM_uint32 min_stat;
    gss_buffer_desc input_token;
	gss_buffer_desc output_token;
	gint result;

	g_return_val_if_fail((gss_ctx != NULL) && (out_token != NULL), -1);

	if (in_token != NULL) {
		gsize out_len;

		input_token.value = g_base64_decode(in_token, &out_len);
		input_token.length = out_len;
	} else {
		input_token.value = NULL;
		input_token.length = 0U;
	}

	maj_stat = gss_init_sec_context(&min_stat, GSS_C_NO_CREDENTIAL, &gss_ctx->context, gss_ctx->target_name, GSS_C_NO_OID,
		gss_ctx->req_flags, 0U, GSS_C_NO_CHANNEL_BINDINGS, &input_token, NULL, &output_token, NULL, NULL);

	if ((maj_stat != GSS_S_COMPLETE) && (maj_stat != GSS_S_CONTINUE_NEEDED)) {
    	gchar *gss_err = gss_error_string(maj_stat, min_stat);

    	g_debug("gss_init_sec_context: %x:%x: %s", maj_stat, min_stat, gss_err);
    	g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GSSAPI, _("cannot initialize GSS security context: %s"),
    		gss_err);
    	g_free(gss_err);
    	result = -1;
   	} else {
   		if (output_token.length > 0U) {
   			*out_token = g_base64_encode(output_token.value, output_token.length);		/*lint !e9079 (MISRA C:2012 Rule 11.5) */
   		} else {
   			*out_token = g_strdup("");
   		}
   		(void) gss_release_buffer(&min_stat, &output_token);
   		if (maj_stat == GSS_S_COMPLETE) {
   			result = 1;
   		} else {
   			result = 0;
   		}
   	}
	(void) gss_release_buffer(&min_stat, &input_token);

   	return result;
}


gchar *
net_client_gss_auth_finish(const NetClientGssCtx *gss_ctx, const gchar *in_token, GError **error)
{
	OM_uint32 maj_stat;
	OM_uint32 min_stat;
	gsize out_len;
    gss_buffer_desc input_token;
	gss_buffer_desc output_token;
	gchar *result = NULL;

	input_token.value = g_base64_decode(in_token, &out_len);
	input_token.length = out_len;
	maj_stat = gss_unwrap(&min_stat, gss_ctx->context, &input_token, &output_token, NULL, NULL);
	if (maj_stat != GSS_S_COMPLETE) {
    	gchar *gss_err = gss_error_string(maj_stat, min_stat);

    	g_debug("gss_unwrap: %x:%x: %s", maj_stat, min_stat, gss_err);
    	g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GSSAPI, _("malformed GSS security token: %s"), gss_err);
    	g_free(gss_err);
	} else {
		const guchar *src;

		/* RFC 4752 requires a token length of 4, and a first octet == 0x01 */
		src = (unsigned char *) output_token.value;		/*lint !e9079	(MISRA C:2012 Rule 11.5) */
		if ((output_token.length != 4U) || (src[0] != 0x01U)) {
			(void) gss_release_buffer(&min_stat, &output_token);
	    	g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GSSAPI, _("malformed GSS security token"));
		} else {
			guchar *dst;

			(void) gss_release_buffer(&min_stat, &input_token);
			input_token.length = strlen(gss_ctx->user) + 4U;
			input_token.value = g_malloc(input_token.length);
			dst = input_token.value;		/*lint !e9079	(MISRA C:2012 Rule 11.5) */
			memcpy(input_token.value, output_token.value, 4U);
			(void) gss_release_buffer(&min_stat, &output_token);
			memcpy(&dst[4], gss_ctx->user, input_token.length - 4U);

			maj_stat = gss_wrap(&min_stat, gss_ctx->context, 0, GSS_C_QOP_DEFAULT, &input_token, NULL, &output_token);
			if (maj_stat != GSS_S_COMPLETE) {
				gchar *gss_err = gss_error_string(maj_stat, min_stat);

		    	g_debug("gss_wrap: %x:%x: %s", maj_stat, min_stat, gss_err);
				g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GSSAPI, _("cannot create GSS login request: %s"),
					gss_err);
				g_free(gss_err);
			} else {
				result = g_base64_encode(output_token.value, output_token.length);		/*lint !e9079 (MISRA C:2012 Rule 11.5) */
				(void) gss_release_buffer(&min_stat, &output_token);
			}
		}
	}

	(void) gss_release_buffer(&min_stat, &input_token);
	return result;
}


void
net_client_gss_ctx_free(NetClientGssCtx *gss_ctx)
{
	if (gss_ctx != NULL) {
		OM_uint32 min_stat;

		if (gss_ctx->context != NULL) {
			(void) gss_delete_sec_context(&min_stat, &gss_ctx->context, GSS_C_NO_BUFFER);
		}
	    if (gss_ctx->target_name != NULL) {
	    	(void) gss_release_name(&min_stat, &gss_ctx->target_name);
	    }
	    g_free(gss_ctx->user);
		g_free(gss_ctx);
	}
}


static gchar *
gss_error_string(OM_uint32 err_maj, OM_uint32 err_min)
{
    OM_uint32 maj_stat;
    OM_uint32 min_stat;
    OM_uint32 msg_ctx;
    gss_buffer_desc status_string;
    GString *message = g_string_new(NULL);
    gchar *result;

    do {
    	maj_stat = gss_display_status(&min_stat, err_maj, GSS_C_GSS_CODE, GSS_C_NO_OID, &msg_ctx, &status_string);
    	if (GSS_ERROR(maj_stat) == 0U) {
    		if (message->len > 0U) {
    			message = g_string_append(message, "; ");
    		}
    		message = g_string_append(message, (const gchar *) status_string.value);	/*lint !e9079 (MISRA C:2012 Rule 11.5) */
    		(void) gss_release_buffer(&min_stat, &status_string);

    		maj_stat = gss_display_status(&min_stat, err_min, GSS_C_MECH_CODE, GSS_C_NULL_OID, &msg_ctx, &status_string);
    		if (GSS_ERROR(maj_stat) == 0U) {
    			message = g_string_append(message, ": ");
    			message = g_string_append(message, (const gchar *) status_string.value);   /*lint !e9079 (MISRA C:2012 Rule 11.5) */
    			(void) gss_release_buffer(&min_stat, &status_string);
    		}
    	}
    } while ((GSS_ERROR(maj_stat) == 0U) && (msg_ctx != 0U));

    if (message->len > 0U) {
    	result = g_string_free(message, FALSE);
    } else {
    	(void) g_string_free(message, TRUE);
    	result = g_strdup_printf(_("unknown error code %u:%u"), (unsigned) err_maj, (unsigned) err_min);
    }
	return result;
}

#endif		/* HAVE_GSSAPI */
