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
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

/* IMAP login/authentication code, GSSAPI method, see RFC2222 and RFC2078 */

#include "config.h"

#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <string.h>

#include "imap-auth.h"

#if defined(HAVE_GSSAPI)
#include <gssapi/gssapi.h>


#include "imap_private.h"
#include "siobuf.h"
#include "util.h"

#define LONG_STRING 2048

enum {
    GSSAPI_P_NONE      = 1 << 0,
    GSSAPI_P_INTEGRITY = 1 << 1,
    GSSAPI_P_PRIVACY   = 1 << 2
};

#define WAIT_FOR_PROMPT(rc,handle,cmdno, buf, len) \
    do (rc) = imap_cmd_step((handle), (cmdno)); while((rc) == IMR_UNTAGGED);

static gboolean ag_get_target(const char *host, gss_name_t *target_name);
static OM_uint32 ag_get_token(gss_ctx_id_t *context, gss_name_t target,
                              gss_buffer_t sec_token,
                              char *client_token, ssize_t token_sz);
static void ag_parse_request(ImapMboxHandle *handle, char *buf, ssize_t buf_sz,
                             gss_buffer_desc *request);
static gboolean ag_negotiate_parameters(ImapMboxHandle *handle,
                                        const char *user, unsigned cmdno,
                                        gss_ctx_id_t context, int *rc);

/* imap_auth_gssapi: gssapi support. */
ImapResult
imap_auth_gssapi(ImapMboxHandle* handle)
{
    static const char Gssapi_cmd[] = "AUTHENTICATE GSSAPI";
    unsigned cmdno;
    int rc, ok;
    char *user = NULL;
    OM_uint32 state, min_stat;
    gss_ctx_id_t context;
    gss_name_t target_name;
    gss_buffer_t sec_token;
    char client_token[LONG_STRING];
    char request_buf[LONG_STRING];
    gss_buffer_desc request;
    int c;
    gboolean sasl_ir;

    if (!imap_mbox_handle_can_do(handle, IMCAP_AGSSAPI))
        return IMAP_AUTH_UNAVAIL;
    sasl_ir = imap_mbox_handle_can_do(handle, IMCAP_SASLIR);

    /* Acquire initial credentials. */
    sec_token = GSS_C_NO_BUFFER;
    context   = GSS_C_NO_CONTEXT;
    
    if( !ag_get_target(handle->host, &target_name)) {
        imap_mbox_handle_set_msg(handle, "Could not get service name");
        return IMAP_AUTH_UNAVAIL;
    }

    state = ag_get_token(&context, target_name, sec_token,
                         client_token, sizeof(client_token));
    if (state != GSS_S_COMPLETE && state != GSS_S_CONTINUE_NEEDED) {
        imap_mbox_handle_set_msg(handle, "GSS ignored - no ticket.");
        printf("GSSAPI: ignored - no ticket (%d).\n", state);
        gss_release_name (&min_stat, &target_name);
        return IMAP_AUTH_UNAVAIL;
    }

    /* get user name */
    ok = 0;
    if(!ok && handle->user_cb)
        handle->user_cb(IME_GET_USER, handle->user_arg, 
                        "GASSAPI", &user, &ok);
    if(!ok || user == NULL) {
        imap_mbox_handle_set_msg(handle, "Authentication cancelled");
        return IMAP_AUTH_FAILURE;
    }
    
    /* start the negotiation */
    if(sasl_ir) { /* save one RTT */
        ImapCmdTag tag;
        if(IMAP_MBOX_IS_DISCONNECTED(handle))
            return IMAP_AUTH_UNAVAIL;
        cmdno = imap_make_tag(tag);
        sio_write(handle->sio, tag, strlen(tag));
        sio_write(handle->sio, " ", 1);
        sio_write(handle->sio, Gssapi_cmd, strlen(Gssapi_cmd));
        sio_write(handle->sio, " ", 1);
        sio_write(handle->sio, client_token, strlen(client_token));
        sio_write(handle->sio, "\r\n", 2);
    } else {
        if(imap_cmd_start(handle, Gssapi_cmd, &cmdno) <0)
            return IMAP_AUTH_FAILURE;
        imap_handle_flush(handle);
        
        WAIT_FOR_PROMPT(rc,handle,cmdno, client_token,sizeof(client_token));
        
        if (rc != IMR_RESPOND) {
            g_warning("gssapi: unexpected response.\n");
            gss_release_name (&min_stat, &target_name);
            return IMAP_AUTH_FAILURE;
        }
        while( (c=sio_getc((handle)->sio)) != EOF && c != '\n');
        
        /* now start the security context initialisation loop... */
        sio_printf(handle->sio, "%s\r\n", client_token);
    }
    imap_handle_flush(handle);
    
    /* The negotiation loop as in  RFC2222 */
    while(state == GSS_S_CONTINUE_NEEDED) {
        WAIT_FOR_PROMPT(rc,handle,cmdno,client_token,sizeof(client_token));
        if (rc != IMR_RESPOND) {
            g_warning("gssapi: unexpected response in the loop.\n");
            gss_release_name (&min_stat, &target_name);
            return IMAP_AUTH_UNAVAIL;      
        }

        ag_parse_request(handle, request_buf, sizeof(request_buf), &request);
        sec_token = &request;
        
        /* Respond to the challenge. */
        state = ag_get_token(&context, target_name, sec_token,
                             client_token, sizeof(client_token));
        if (state != GSS_S_COMPLETE && state != GSS_S_CONTINUE_NEEDED) {
            imap_mbox_handle_set_msg(handle,
                                     "Error exchanging GSS credentials");
            gss_release_name (&min_stat, &target_name);
            goto negotiation_aborted;
        }
        sio_printf(handle->sio, "%s\r\n", client_token);
        imap_handle_flush(handle);
    }
    
    gss_release_name (&min_stat, &target_name);
    
    /* get security flags and buffer size */
    WAIT_FOR_PROMPT(rc,handle,cmdno,client_token,sizeof(client_token));
    if (rc != IMR_RESPOND) return IMAP_AUTH_FAILURE;

    if(!ag_negotiate_parameters(handle, user, cmdno, context, &rc))
        goto negotiation_aborted;

    /* clean up. */
    state = gss_delete_sec_context(&min_stat, &context, &request);
    if (state != GSS_S_COMPLETE)
        g_warning("gss_delete_sec_context() failed");
    gss_release_buffer (&min_stat, &request);
    return rc == IMR_OK ? IMAP_SUCCESS : IMAP_AUTH_FAILURE;
 negotiation_aborted:
    sio_write(handle->sio, "*\r\n", 3); imap_handle_flush(handle);
    WAIT_FOR_PROMPT(rc,handle,cmdno,client_token, sizeof(client_token));
    return IMAP_AUTH_FAILURE;
}

/* ag_get_target: get an IMAP service ticket name for the server */
static gboolean
ag_get_target(const char *host, gss_name_t *target_name)
{
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc request;
    char buf[LONG_STRING];
    gss_OID mech_name;

    snprintf (buf, sizeof (buf), "imap@%s", host);
    request.value = buf;
    request.length = strlen(buf) + 1;
    maj_stat = gss_import_name(&min_stat, &request, GSS_C_NT_HOSTBASED_SERVICE,
                               target_name);
    if(maj_stat != GSS_S_COMPLETE)
        return FALSE;
    maj_stat = gss_display_name(&min_stat, *target_name, &request,
                                &mech_name);
    printf("GSSAPI: Using service name [%s]\n",(char*) request.value);
    maj_stat = gss_release_buffer (&min_stat, &request);

    return maj_stat == GSS_S_COMPLETE;
}

/* ag_get_token obtains next CLIENT_TOKEN as a response to SEC_TOKEN.
 */
static OM_uint32
ag_get_token(gss_ctx_id_t *context, gss_name_t target, gss_buffer_t sec_token,
             char *client_token, ssize_t token_sz)
{
    OM_uint32 state, min_stat;
    gss_buffer_desc send_token;
    unsigned cflags;
    
    *client_token = '\0';
    state = gss_init_sec_context
        (&min_stat, GSS_C_NO_CREDENTIAL, context,
         target, GSS_C_NO_OID, GSS_C_MUTUAL_FLAG | GSS_C_SEQUENCE_FLAG, 0, 
         GSS_C_NO_CHANNEL_BINDINGS, sec_token, NULL, &send_token,
         &cflags, NULL);

    if (state != GSS_S_COMPLETE && state != GSS_S_CONTINUE_NEEDED)
        return state;

    lit_conv_to_base64(client_token, send_token.value, send_token.length,
                       token_sz);
    gss_release_buffer(&min_stat, &send_token);
    return state;
}

static void
ag_parse_request(ImapMboxHandle *handle, char *buf, ssize_t buf_sz,
                 gss_buffer_desc *request)
{
    char line[LONG_STRING];
    sio_gets(handle->sio, line, LONG_STRING); /* FIXME: error checking */
    request->length = lit_conv_from_base64(buf, line);
    request->value = buf;
}

/* ag_negotiate_parameters() implements the final phase, establishes
   the security levels and attempts to set the user whose mailboxes
   are to be accessed.
*/
static gboolean
ag_negotiate_parameters(ImapMboxHandle *handle, const char * user,
                        unsigned cmdno, gss_ctx_id_t context, int *rc)
{
    OM_uint32 state, min_stat;
    gss_buffer_desc request_buf, send_token;
    char buf[LONG_STRING];
    gss_qop_t quality;
    unsigned cflags;
    char server_conf_flags;
    unsigned char *t;
    unsigned long buf_size;

    ag_parse_request(handle, buf, sizeof(buf), &request_buf);
    state = gss_unwrap(&min_stat, context, &request_buf, &send_token,
                       &cflags, &quality);
    if (state != GSS_S_COMPLETE) {
        imap_mbox_handle_set_msg(handle,
                                 "Could not unwrap security level data");
        gss_release_buffer (&min_stat, &send_token);
        return FALSE;
    }
    
    /* first octet is security levels supported. We want NONE */
    server_conf_flags = *((char*) send_token.value);
    if ( !(server_conf_flags & GSSAPI_P_NONE) ) {
        imap_mbox_handle_set_msg(handle,
                                 "Server requires integrity or privacy");
        gss_release_buffer (&min_stat, &send_token);
        return FALSE;
    }
    
    /* we don't care about buffer size if we don't wrap content. But
     * here it is */
    t = send_token.value;
    buf_size = (t[1] << 16) | (t[2]<<8) | t[3];
    gss_release_buffer (&min_stat, &send_token);
    printf("GSSAPI: Security level flags: %c%c%c\n",
           server_conf_flags & GSSAPI_P_NONE      ? 'N' : '-',
           server_conf_flags & GSSAPI_P_INTEGRITY ? 'I' : '-',
           server_conf_flags & GSSAPI_P_PRIVACY   ? 'P' : '-');
    printf("GSSAPI: Maximum GSS token size is %lu\n", buf_size);
    
    /* Set P_NONE and accept the buf_size. */
    buf[0] = GSSAPI_P_NONE;
    buf[1] = (buf_size >> 16) & 0xff;
    buf[2] = (buf_size >> 8)  & 0xff;
    buf[3] = (buf_size)       & 0xff;
    strncpy (buf + 4, user, sizeof(buf)-4);
    request_buf.value = buf;
    request_buf.length = 4 + strlen(user) + 1;
    state = gss_wrap(&min_stat, context, 0, GSS_C_QOP_DEFAULT,
                     &request_buf, &cflags, &send_token);
    if (state != GSS_S_COMPLETE) {
        imap_mbox_handle_set_msg(handle, "Error creating login request");
        return FALSE;
    }
    
    lit_conv_to_base64(buf, send_token.value, send_token.length,
                       sizeof(buf));
    sio_printf(handle->sio, "%s\r\n", buf); imap_handle_flush(handle);
    
    WAIT_FOR_PROMPT(*rc,handle,cmdno,buf,sizeof(buf));
    if (*rc == IMR_RESPOND)
        return FALSE;

    return TRUE; /* the negotiation was successful but if we got rc ==
                    IMR_NO, the user was not allowed to access the
                    mailbox. */
}
#else /* defined(HAVE_GSSAPI) */

ImapResult
imap_auth_gssapi(ImapMboxHandle* handle)
{ return IMAP_AUTH_UNAVAIL; }

#endif /* defined(HAVE_GSSAPI) */
