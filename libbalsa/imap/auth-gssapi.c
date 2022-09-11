/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 2004 Pawel Salek
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

/* IMAP login/authentication code, GSSAPI method, see RFC2222 and RFC2078 */

#include "config.h"
#include "imap-auth.h"
#include <glib/gi18n.h>

#if defined(HAVE_GSSAPI)
#if defined(HAVE_HEIMDAL)
#include <gssapi.h>
#else
#include <gssapi/gssapi.h>
#endif

#include "imap_private.h"
#include "siobuf-nc.h"
#include "net-client-utils.h"


static gboolean
imap_gss_auth_init(ImapMboxHandle* handle, NetClientGssCtx *gss_ctx, unsigned *cmdno, GError **error)
{
	gint state;
	gchar *output_token;
	ImapResponse rc;
	gboolean result;

	state = net_client_gss_auth_step(gss_ctx, NULL, &output_token, error);
	if (state >= 0) {
        ImapCmdTag tag;

        *cmdno = imap_make_tag(tag);
		if (imap_mbox_handle_can_do(handle, IMCAP_SASLIR)) {
			result = net_client_write_line(NET_CLIENT(handle->sio), "%s AUTHENTICATE GSSAPI %s", error, tag, output_token);
		} else {
			result = net_client_write_line(NET_CLIENT(handle->sio), "%s AUTHENTICATE GSSAPI", error, tag);
			if (result) {
				rc = imap_cmd_process_untagged(handle, *cmdno);

				if (rc == IMR_RESPOND) {
					net_client_siobuf_discard_line(handle->sio, NULL);
					result = net_client_write_line(NET_CLIENT(handle->sio), "%s", error, output_token);
				} else {
					result = FALSE;
				}
			}
		}
		g_free(output_token);
	} else {
		result = FALSE;
	}

	return result;
}

static gboolean
imap_gss_auth_loop(ImapMboxHandle* handle, NetClientGssCtx *gss_ctx, unsigned cmdno, GError **error)
{
    int rc;
	gchar *input_token;
	gint state = 0;
	gboolean result;

	do {
		rc = imap_cmd_process_untagged(handle, cmdno);
		result = FALSE;
		if (rc == IMR_RESPOND) {
			input_token = net_client_siobuf_get_line(handle->sio, error);
			if (input_token != NULL) {
				result = TRUE;
			}
		}

		if (result) {
			gchar *output_token = NULL;

			if (state == 0) {
				state = net_client_gss_auth_step(gss_ctx, input_token, &output_token, error);
			} else {
				output_token = net_client_gss_auth_finish(gss_ctx, input_token, error);
				if (output_token == NULL) {
					state = -1;
				} else {
					state = 2;
				}
			}
			g_free(input_token);
			if (state >= 0) {
				result = net_client_write_line(NET_CLIENT(handle->sio), "%s", NULL, output_token);
			}
			g_free(output_token);
		}
	} while (result && (state != 2));

	return result && (state == 2);
}

/* imap_auth_gssapi: gssapi support. */
ImapResult
imap_auth_gssapi(ImapMboxHandle* handle)
{
	gchar **auth_data;
    NetClientGssCtx *gss_ctx;
    GError *error = NULL;
    ImapResult retval;

    if (!imap_mbox_handle_can_do(handle, IMCAP_AGSSAPI)) {
        return IMAP_AUTH_UNAVAIL;
    }

    g_signal_emit_by_name(handle->sio, "auth", NET_CLIENT_AUTH_KERBEROS, &auth_data);
    if((auth_data == NULL) || (auth_data[0] == NULL)) {
    	imap_mbox_handle_set_msg(handle, _("User name required, authentication cancelled"));
    	g_strfreev(auth_data);
    	return IMAP_AUTH_CANCELLED;
    }

    /* try to create the context */
    gss_ctx = net_client_gss_ctx_new("imap", handle->host, auth_data[0], &error);
    if (gss_ctx == NULL) {
    	retval = IMAP_AUTH_UNAVAIL;
    } else {
    	gboolean result;
        unsigned cmdno;
        int rc;

    	result = imap_gss_auth_init(handle, gss_ctx, &cmdno, &error);
    	if (result) {
    		result = imap_gss_auth_loop(handle, gss_ctx, cmdno, &error);
    		if (!result) {
    			/* cancel the auth process */
    			(void) net_client_write_line(NET_CLIENT(handle->sio), "*", NULL);
    		}
    		rc = imap_cmd_process_untagged(handle, cmdno);
    	} else {
    		rc = IMR_BAD;
    	}

		net_client_gss_ctx_free(gss_ctx);
		retval = (result && (rc == IMR_OK)) ? IMAP_SUCCESS : IMAP_AUTH_UNAVAIL;
    }
	g_strfreev(auth_data);

    if (error != NULL) {
    	imap_mbox_handle_set_msg(handle, _("GSSAPI authentication failed: %s"), error->message);
    	g_error_free(error);
    }

    return retval;
}

#else /* defined(HAVE_GSSAPI) */

ImapResult
imap_auth_gssapi(ImapMboxHandle* handle)
{ return IMAP_AUTH_UNAVAIL; }

#endif /* defined(HAVE_GSSAPI) */
