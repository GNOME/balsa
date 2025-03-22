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
#include "config.h"

#define _POSIX_C_SOURCE 199506L
#define _XOPEN_SOURCE 500
#define _DEFAULT_SOURCE     1

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <ctype.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <gmime/gmime-utils.h>

#include "imap-auth.h"
#include "imap-handle.h"
#include "imap-commands.h"
#include "imap_private.h"
#include "siobuf-nc.h"
#include "util.h"

#define LONG_STRING 512

#define IDLE_TIMEOUT 30

#define error_safe(e)	(((e != NULL) && ((e)->message != NULL)) ? (e)->message : _("unknown"))

enum _ImapHandleSignal {
  FETCH_RESPONSE,
  LIST_RESPONSE,
  LSUB_RESPONSE,
  EXPUNGE_NOTIFY,
  EXISTS_NOTIFY,
  LAST_SIGNAL
};
typedef enum _ImapHandleSignal ImapHandleSignal;

static guint imap_mbox_handle_signals[LAST_SIGNAL] = { 0 };
static void imap_mbox_handle_finalize(GObject* gobject);

static ImapResult imap_mbox_connect(ImapMboxHandle* handle);

static ImapResponse ir_handle_response(ImapMboxHandle *h);

static ImapAddress* imap_address_from_string(const gchar *string, gchar **n);
static gchar*       imap_address_to_string(const ImapAddress *addr);

static gboolean async_process(GSocket      *source,
			  	  	  	  	  GIOCondition  condition,
							  gpointer      data);

G_DEFINE_TYPE(ImapMboxHandle, imap_mbox_handle, G_TYPE_OBJECT)

static void
imap_mbox_handle_init(ImapMboxHandle *handle)
{
  handle->timeout = -1;
  handle->flag_cache=  g_array_new(FALSE, TRUE, sizeof(ImapFlagCache));
  handle->status_resps = g_hash_table_new_full(g_str_hash, g_str_equal,
                                               NULL, NULL);
  handle->state = IMHS_DISCONNECTED;
  handle->tls_mode = NET_CLIENT_CRYPT_STARTTLS;
  handle->auth_mode = NET_CLIENT_AUTH_USER_PASS | NET_CLIENT_AUTH_KERBEROS;
  handle->idle_state = IDLE_INACTIVE;
  handle->enable_idle = 1;

#ifdef G_OBJECT_NEEDS_TO_BE_INITIALIZED
  handle->host   = NULL;
  handle->mbox   = NULL;
  handle->has_capabilities = FALSE;
  handle->exists = 0;
  handle->recent = 0;
  handle->last_msg = NULL;
  handle->msg_cache = NULL;
  handle->doing_logout = FALSE;
  handle->cmd_info = NULL;
  handle->info_cb  = NULL;
  handle->info_arg = NULL;
  handle->auth_cb = NULL;
  handle->auth_arg = NULL;
  handle->cert_cb = NULL;
  handle->op_cancelled = 0;
  handle->enable_anonymous = 0;
  handle->enable_client_sort = 0;
  handle->enable_binary = 0;
  handle->has_rights = 0;
#endif /* G_OBJECT_NEEDS_TO_BE_INITIALIZED */

  mbox_view_init(&handle->mbox_view);

  g_mutex_init(&handle->mutex);
}

static void
imap_mbox_handle_class_init(ImapMboxHandleClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  
  imap_mbox_handle_signals[FETCH_RESPONSE] = 
    g_signal_new("fetch-response",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 0, NULL, NULL,
                 NULL, G_TYPE_NONE, 0);

  imap_mbox_handle_signals[LIST_RESPONSE] = 
    g_signal_new("list-response",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 0, NULL, NULL,
                 NULL, G_TYPE_NONE, 3,
                 G_TYPE_INT, G_TYPE_INT, G_TYPE_POINTER);

  imap_mbox_handle_signals[LSUB_RESPONSE] = 
    g_signal_new("lsub-response",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 0, NULL, NULL,
                 NULL, G_TYPE_NONE, 3,
                 G_TYPE_INT, G_TYPE_INT, G_TYPE_POINTER);

  imap_mbox_handle_signals[EXPUNGE_NOTIFY] = 
    g_signal_new("expunge-notify",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 0, NULL, NULL,
                 NULL, G_TYPE_NONE, 1,
		 G_TYPE_INT);

  imap_mbox_handle_signals[EXISTS_NOTIFY] = 
    g_signal_new("exists-notify",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 0, NULL, NULL,
                 NULL, G_TYPE_NONE, 0);

  object_class->finalize = imap_mbox_handle_finalize;
}

ImapMboxHandle*
imap_mbox_handle_new(void)
{
  return g_object_new(imap_mbox_handle_get_type(), NULL);
}

void
imap_handle_set_option(ImapMboxHandle *h, ImapOption opt, gboolean state)
{
  switch(opt) {
  case IMAP_OPT_BINARY:      h->enable_binary      = !!state; break;
  case IMAP_OPT_CLIENT_SORT: h->enable_client_sort = !!state; break;
  case IMAP_OPT_COMPRESS:    h->enable_compress    = !!state; break;
  case IMAP_OPT_IDLE:        h->enable_idle        = !!state; break;
  default: g_warning("imap_set_option: invalid option");
  }
}

void
imap_handle_set_infocb(ImapMboxHandle* h, ImapInfoCb cb, void *arg)
{
  h->info_cb  = cb;
  h->info_arg = arg;
}

void
imap_handle_set_authcb(ImapMboxHandle* h, GCallback cb, void *arg)
{
	h->auth_cb  = cb;
	h->auth_arg = arg;
}

void
imap_handle_set_certcb(ImapMboxHandle* h, GCallback cb)
{
	h->cert_cb  = cb;
}

void
imap_handle_set_flagscb(ImapMboxHandle* h, ImapFlagsCb cb, void* arg)
{
  h->flags_cb  = cb;
  h->flags_arg = arg;
}

/** CmdInfo structure stores information about asynchronously executed
    commands. */
struct CmdInfo {
  unsigned cmdno; /**< Number of the the command */
  ImapResponse rc; /**< response code to the command if it already completed */
  /** complete_cb is executed when the given command is completed.  If
      complete_cb returns true, the corresponding CmdInfo structure
      will be removed from the cmd_info hash.*/
  gboolean (*complete_cb)(ImapMboxHandle *h, void *d);
  void *cb_data; /**< data to be passed on to complete_cb */
  unsigned completed; /**< determines whether the complete_cb has been
                       * executed and rc contains meaningful value. */
};

/** cmdi_get_pending returns number of any pending command. Returns 0
    if there is none. */
static unsigned
cmdi_get_pending(GList *cmd_info)
{
  for(; cmd_info; cmd_info = cmd_info->next) {
    struct CmdInfo *ci = (struct CmdInfo*)cmd_info->data;
    if(!ci->completed)
      return ci->cmdno;
  }
  return 0;
}

static void
cmdi_add_handler(GList **cmd_info, unsigned cmdno,
                 gboolean (*handler)(ImapMboxHandle*h, void *d), void *data)
{
  struct CmdInfo *ci = g_new0(struct CmdInfo, 1);
  ci->cmdno = cmdno;
  ci->complete_cb = handler;
  ci->cb_data = data;
  *cmd_info = g_list_prepend(*cmd_info, ci);
}

static struct CmdInfo*
cmdi_find_by_no(GList *cmd_info, unsigned lastcmd)
{
  for(; cmd_info; cmd_info = cmd_info->next) {
    struct CmdInfo *ci = (struct CmdInfo*)cmd_info->data;
    if(ci->cmdno == lastcmd)
      return ci;
  }
  return NULL;
}

/** fallback handler - consider the command done and remove the
    corresponding CmdInfo structure from the list of pending
    commands. */
static gboolean
cmdi_empty(ImapMboxHandle *h, void *d)
{ return TRUE; }

/** Sets new timeout. Returns the old one. */
int
imap_handle_set_timeout(ImapMboxHandle *h, int milliseconds)
{
  int old_timeout = h->timeout;
  h->timeout = milliseconds;
  if(h->sio)
    net_client_set_timeout(NET_CLIENT(h->sio), (milliseconds + 500) / 1000);
  return old_timeout;
}

/* Called with a locked handle. */
static void
socket_source_add(ImapMboxHandle *h)
{
	if (h->sock_source == NULL) {
	    h->sock_source = g_socket_create_source(net_client_get_socket(NET_CLIENT(h->sio)), G_IO_IN|G_IO_HUP, NULL);
	    g_source_set_callback(h->sock_source, (GSourceFunc) async_process, h, NULL);
	    g_source_attach(h->sock_source, NULL);
	    g_debug("async_process() registered");
	}
}

/* Called with a locked handle. */
static void
socket_source_remove(ImapMboxHandle *h)
{
	if (h->sock_source != NULL) {
		g_source_destroy(h->sock_source);
		g_source_unref(h->sock_source);
		h->sock_source = NULL;
	    g_debug("async_process() removed");
	}
}


/** Called with a locked handle. */
static gboolean
async_process_real(ImapMboxHandle *h)
{
	ImapResponse rc = IMR_UNTAGGED;
	unsigned async_cmd;

	g_debug("%s: ENTER", __func__);
	async_cmd = cmdi_get_pending(h->cmd_info);
	g_debug("%s: enter loop, cmnd %u", __func__, async_cmd);
	while (net_client_can_read(NET_CLIENT(h->sio))) {
		rc = imap_cmd_step(h, async_cmd);
		if (h->idle_state == IDLE_RESPONSE_PENDING) {
			int c;
			if(rc != IMR_RESPOND) {
				g_debug("%s: expected IMR_RESPOND but got %d", __func__, rc);
				imap_handle_disconnect(h);
				return G_SOURCE_REMOVE;
			}
			EAT_LINE(h, c);
			if (c == '\n') {
				h->idle_state = IDLE_ACTIVE;
				g_debug("%s: IDLE is now ACTIVE", __func__);
			}
		} else if (rc == IMR_UNKNOWN ||
			rc == IMR_SEVERED || rc == IMR_BYE || rc == IMR_PROTOCOL ||
			rc  == IMR_BAD) {
			g_debug("%s: got unexpected response %i! "
				"Last message was: \"%s\" - shutting down connection.",
				__func__, rc, h->last_msg);
			imap_handle_disconnect(h);
			return G_SOURCE_REMOVE;
		}
		async_cmd = cmdi_get_pending(h->cmd_info);
		g_debug("%s: loop iteration finished, next async_cmd=%x", __func__,
			async_cmd);
	}
	g_debug("%s: loop left", __func__);
	if (h->idle_state == IDLE_INACTIVE && async_cmd == 0) {
		g_debug("%s: Last async command completed.", __func__);
		socket_source_remove(h);
		imap_handle_idle_enable(h, IDLE_TIMEOUT);
	}
	g_debug("%s: rc: %d returns %d (%d cmds in queue)", __func__,
		rc, h->idle_state == IDLE_INACTIVE && async_cmd == 0,
		g_list_length(h->cmd_info));
	g_debug("%s: DONE", __func__);
	return h->idle_state != IDLE_INACTIVE || async_cmd != 0;
}

/* imap_handle_idle_enable: enables calling IDLE command after seconds
   of inactivity. IDLE support consists of three subroutines:

1. imap_handle_idle_{enable,disable}() switch to and from the IDLE
   mode.  switching to the mode is done by registering an idle
   callback idle_start() with 30 seconds delay time.

2. idle start() sends the IDLE command and registers idle_process() to
   be notified whenever data is available on the specified descriptor.

3. async_process() processes the data sent from the server. It is used
   by IDLE and STORE commands, for example. */

static gboolean
async_process(GSocket      *source,
			  GIOCondition  condition,
			  gpointer      data)
{
	ImapMboxHandle *h = (ImapMboxHandle *) data;
	gboolean retval_async;

	g_return_val_if_fail(h, FALSE);

	g_debug("%s: ENTER", __func__);
	if (g_mutex_trylock(&h->mutex)) {
		g_debug("%s: LOCKED", __func__);
		if (h->state == IMHS_DISCONNECTED) {
			g_debug("%s: on disconnected", __func__);
			retval_async = G_SOURCE_REMOVE;
		} else if ((condition & G_IO_HUP) == G_IO_HUP) {
			g_debug("%s: hangup", __func__);
			imap_handle_disconnect(h);
			retval_async = G_SOURCE_REMOVE;
		} else {
			retval_async = async_process_real(h);
		}
		g_mutex_unlock(&h->mutex);
	} else {
		retval_async = G_SOURCE_REMOVE;		/* async data on already locked handle? Don't try again. */
	}

	g_debug("%s: DONE (keep: %d)", __func__, retval_async);
	return retval_async;
}

static gboolean
idle_start(gpointer data)
{
  ImapMboxHandle *h = (ImapMboxHandle*)data;
  ImapCmdTag tag;
  unsigned asyncno;

  g_assert(h != NULL);

  if(!g_mutex_trylock(&h->mutex))
    return TRUE;/* Don't block, just try again later. */

  /* One way or another, we are now going to return FALSE,
   * so clear the idle id: */
  h->idle_enable_id = 0;

  /* The test below can probably be weaker since it is ok for the
     channel to get disconnected before IDLE gets activated */
  IMAP_REQUIRED_STATE3(h, IMHS_CONNECTED, IMHS_AUTHENTICATED,
                       IMHS_SELECTED, FALSE);

  asyncno = imap_make_tag(tag);
  net_client_write_line(NET_CLIENT(h->sio), "%s IDLE", NULL, tag);
  cmdi_add_handler(&h->cmd_info, asyncno, cmdi_empty, NULL);
  socket_source_add(h);
  h->idle_state = IDLE_RESPONSE_PENDING;

  g_mutex_unlock(&h->mutex);
  return FALSE;
}

/** Called with handle locked. */
ImapResponse
imap_cmd_issue(ImapMboxHandle* h, const char* cmd)
{
  unsigned async_cmd;
  g_return_val_if_fail(h, IMR_BAD);
  if (h->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;

  /* create sequence for command */
  if (!imap_handle_idle_disable(h)) return IMR_SEVERED;
  if (imap_cmd_start(h, cmd, &async_cmd)<0)
    return IMR_SEVERED;  /* irrecoverable connection error. */

  g_debug("command '%s' issued.", cmd);
  cmdi_add_handler(&h->cmd_info, async_cmd, cmdi_empty, NULL);
  socket_source_add(h);

  return IMR_OK /* async_cmd */;
}

gboolean
imap_handle_idle_enable(ImapMboxHandle *h, int seconds)
{
  if( !h->enable_idle || !imap_mbox_handle_can_do(h, IMCAP_IDLE))
    return FALSE;
  if(h->idle_state != IDLE_INACTIVE) {
    g_warning("IDLE already enabled");
    return FALSE;
  }
  if(!h->idle_enable_id)
    h->idle_enable_id = g_timeout_add_seconds(seconds, idle_start, h);
  return TRUE;
}

gboolean
imap_handle_idle_disable(ImapMboxHandle *h)
{
  if(h->idle_enable_id) {
    g_source_remove(h->idle_enable_id);
    h->idle_enable_id = 0;
  }
  if(h->sock_source != NULL) {
	socket_source_remove(h);
    if(h->sio && h->idle_state == IDLE_RESPONSE_PENDING) {
      int c;
      ImapResponse rc;
      unsigned async_cmd = cmdi_get_pending(h->cmd_info);

      rc = imap_cmd_process_untagged(h, async_cmd);
      if(rc != IMR_RESPOND) {
	imap_handle_disconnect(h);
	return FALSE;
      }
      EAT_LINE(h, c);
      if(c == -1) {
	imap_handle_disconnect(h);
	return FALSE;
      }
      h->idle_state = IDLE_ACTIVE;
    }
    if (h->sio &&  h->idle_state == IDLE_ACTIVE) {
      /* we might have been disconnected before */
      net_client_write_line(NET_CLIENT(h->sio), "DONE", NULL);
      h->idle_state = IDLE_INACTIVE;
    }
  }
  return TRUE;
}

gboolean
imap_handle_op_cancelled(ImapMboxHandle *h)
{
  return h->op_cancelled;
}

/** Called with handle locked. */
void
imap_handle_disconnect(ImapMboxHandle *h)
{
  gboolean G_GNUC_UNUSED dummy;
  dummy = imap_handle_idle_disable(h);
  if(h->sio) {
    g_object_unref(h->sio); h->sio = NULL;
  }
  socket_source_remove(h);
  h->state = IMHS_DISCONNECTED;
}

int imap_mbox_is_disconnected (ImapMboxHandle *h)
{ return IMAP_MBOX_IS_DISCONNECTED(h); }
int imap_mbox_is_connected    (ImapMboxHandle *h)
{ return IMAP_MBOX_IS_CONNECTED(h); }
int imap_mbox_is_authenticated(ImapMboxHandle *h)
{ return IMAP_MBOX_IS_AUTHENTICATED(h); }
int imap_mbox_is_selected     (ImapMboxHandle *h)
{ return IMAP_MBOX_IS_SELECTED(h); }

ImapResult
imap_mbox_handle_connect(ImapMboxHandle* ret, const char *host)
{
  ImapResult rc;

  g_return_val_if_fail(imap_mbox_is_disconnected(ret), IMAP_CONNECT_FAILED);

  g_mutex_lock(&ret->mutex);

  g_free(ret->host);   ret->host   = g_strdup(host);

  if( (rc=imap_mbox_connect(ret)) == IMAP_SUCCESS) {
    rc = imap_authenticate(ret);
    if (rc == IMAP_SUCCESS) {
      ImapResponse response = imap_compress(ret);
      if ( !(response == IMR_NO || response == IMR_OK))
        rc = IMAP_PROTOCOL_ERROR;
    }
  }

  g_mutex_unlock(&ret->mutex);

  return rc;
}

void
imap_mbox_resize_cache(ImapMboxHandle *h, unsigned new_size)
{
  unsigned i;
  if(new_size<h->exists) { /* shrink msg_cache */
    for(i=new_size; i<h->exists; i++) {
      if(h->msg_cache[i])
        imap_message_free(h->msg_cache[i]);
    }
  }
  h->msg_cache = g_realloc(h->msg_cache, new_size*sizeof(ImapMessage*));
  g_array_set_size(h->flag_cache, new_size);
  for(i=h->exists; i<new_size; i++) 
    h->msg_cache[i] = NULL;
  h->exists = new_size;
}

/* imap_mbox_handle_reconnect:
   invalidate cache as late as possible.
*/
ImapResult
imap_mbox_handle_reconnect(ImapMboxHandle* h, gboolean *readonly)
{
  ImapResult rc;
  
  g_mutex_lock(&h->mutex);

  if( (rc=imap_mbox_connect(h)) == IMAP_SUCCESS) {
    if( (rc = imap_authenticate(h)) == IMAP_SUCCESS) {
      ImapResponse response;
      imap_mbox_resize_cache(h, 0); /* invalidate cache */
      mbox_view_dispose(&h->mbox_view); /* FIXME: recreate it here? */

      response = imap_compress(h);
      if (response == IMR_OK || response == IMR_NO) {
        rc = IMAP_SUCCESS;
        if(h->mbox && 
           imap_mbox_select_unlocked(h, h->mbox, readonly) != IMR_OK) {
          rc = IMAP_SELECT_FAILED;
        }

      } else {
        /* compression was apparently attempted but failed. */
        rc = IMAP_PROTOCOL_ERROR;
      }

    }
  }
  g_mutex_unlock(&h->mutex);
  return rc;
}

/** Drops the connection without waiting for response.  This can be
    called when eg a signal from NetworkManager arrives. */
void
imap_handle_force_disconnect(ImapMboxHandle *h)
{
  g_mutex_lock(&h->mutex);
  imap_handle_disconnect(h);
  g_mutex_unlock(&h->mutex);
}

NetClientCryptMode
imap_handle_set_tls_mode(ImapMboxHandle* h, NetClientCryptMode state)
{
  NetClientCryptMode res;
  g_return_val_if_fail(h,0);
  res = h->tls_mode;
  h->tls_mode = state;
  return res;
}

NetClientAuthMode
imap_handle_set_auth_mode(ImapMboxHandle *h, NetClientAuthMode mode)
{
	NetClientAuthMode res;

	g_return_val_if_fail(h != NULL, 0U);
	res = h->auth_mode;
	h->auth_mode = mode;
	return res;
}

const char* imap_msg_flags[6] = { 
  "seen", "answered", "flagged", "deleted", "draft", "recent"
};

struct ListData { 
  ImapListCb cb;
  void * cb_data;
};

#if 0
static int
imap_timeout_cb(void *arg)
{
  ImapMboxHandle *h = (ImapMboxHandle*)arg;
  int ok = 1;

  /* No reason to lock the handle here: if we get here, we have
     already been performing some operation and keep the handle
     locked. */
  if(h->user_cb) {
    h->user_cb(IME_TIMEOUT, h->user_arg, &ok);
    if(ok) {
      h->op_cancelled = TRUE;
      imap_handle_disconnect(h);
    }
  }

  return ok;
}
#endif

static ImapResult
imap_mbox_connect(ImapMboxHandle* handle)
{
  ImapResult retval;
  ImapResponse resp;
  GError *error = NULL;

  /* reset some handle status */
  handle->op_cancelled = FALSE;
  handle->has_capabilities = FALSE;
  handle->can_fetch_body = TRUE;
  handle->idle_state = IDLE_INACTIVE;
  if(handle->sio) {
    g_object_unref(handle->sio); handle->sio = NULL;
  }

  handle->sio = net_client_siobuf_new(handle->host,
	  handle->tls_mode == NET_CLIENT_CRYPT_ENCRYPTED ? 993 : 143);
  g_signal_connect(handle->sio, "auth", handle->auth_cb, handle->auth_arg);
  g_signal_connect(handle->sio, "cert-check", handle->cert_cb, handle->sio);
  /* FIXME - client certificate? */
  if (!net_client_connect(NET_CLIENT(handle->sio), &error)) {
	imap_mbox_handle_set_msg(handle, _("Connecting %s failed: %s"), handle->host, error_safe(error));
	g_clear_error(&error);
	return IMAP_CONNECT_FAILED;
  }
  
#if 0
  if(handle->timeout>0) {
    sio_set_timeout(handle->sio, handle->timeout);
    sio_set_timeoutcb(handle->sio, imap_timeout_cb, handle);
  }
#endif
  if (handle->tls_mode == NET_CLIENT_CRYPT_ENCRYPTED) {
    if (!net_client_start_tls(NET_CLIENT(handle->sio), &error)) {
      imap_mbox_handle_set_msg(handle, _("TLS negotiation failed: %s"), error_safe(error));
	  g_clear_error(&error);
      return IMAP_UNSECURE;
    }
  }

  handle->state = IMHS_CONNECTED;
  if ( (resp=imap_cmd_step(handle, 0)) != IMR_UNTAGGED) {
    g_debug("imap_mbox_connect:unexpected initial response(%d): %s",
	      resp, handle->last_msg);
    imap_handle_disconnect(handle);
    return IMAP_PROTOCOL_ERROR;
  }
  handle->can_fetch_body = (handle->last_msg != NULL) &&
    (strncmp(handle->last_msg, "Microsoft Exchange", 18) != 0);
  if((handle->tls_mode == NET_CLIENT_CRYPT_ENCRYPTED) ||
	 (handle->tls_mode == NET_CLIENT_CRYPT_NONE)) {
    retval = IMAP_SUCCESS; /* secured already with SSL, or no encryption requested */
  } else if(imap_mbox_handle_can_do(handle, IMCAP_STARTTLS)) {
    if( imap_handle_starttls(handle, &error) != IMR_OK) {
      imap_mbox_handle_set_msg(handle, _("TLS negotiation failed: %s"), error_safe(error));
      retval = IMAP_UNSECURE; /* TLS negotiation error */
    } else {
      retval = IMAP_SUCCESS; /* secured with TLS */
    }
  } else {
	imap_mbox_handle_set_msg(handle, _("TLS required but not available"));
    retval = IMAP_AUTH_UNAVAIL; /* TLS unavailable */
  }

  return retval;
}

unsigned
imap_make_tag(ImapCmdTag tag)
{
  static unsigned no = 0; /* MT-locking here */
  sprintf(tag, "%x", ++no);
  return no;
}

static int
imap_get_atom(NetClientSioBuf *sio, char* atom, size_t len)
{
  unsigned i;
  int c = 0;
  for(i=0; i<len-1 && (c=sio_getc(sio)) >=0 && IS_ATOM_CHAR(c); i++)
    atom[i] = c;

  atom[i] = '\0';
  return c;
}

#define IS_FLAG_CHAR(c) (strchr("(){ %*\"]",(c))==NULL&&(c)>0x1f&&(c)!=0x7f)
static int
imap_get_flag(NetClientSioBuf *sio, char* flag, size_t len)
{
  unsigned i;
  int c = 0;
  for(i=0; i<len-1 && (c=sio_getc(sio)) >=0 && IS_FLAG_CHAR(c); i++) {
    flag[i] = c;
  }
  flag[i] = '\0';
  return c;
}

/* we include '+' in TAG_CHAR because we want to treat forced responses
   in same code. This may be wrong. Reconsider.
*/
#define IS_TAG_CHAR(c) (strchr("(){ %\"\\]",(c))==NULL&&(c)>0x1f&&(c)!=0x7f)
static int
imap_cmd_get_tag(NetClientSioBuf *sio, char* tag, size_t len)
{
  unsigned i;
  int c = 0;
  for(i=0; i<len-1 && (c=sio_getc(sio)) >=0 && IS_TAG_CHAR(c); i++) {
    tag[i] = c;
  }
  tag[i] = '\0';
  return c;
}

  
ImapConnectionState
imap_mbox_handle_get_state(ImapMboxHandle *h)
{
  return h->state; 
}

void
imap_mbox_handle_set_state(ImapMboxHandle *h, ImapConnectionState newstate)
{
  h->state = newstate;
}


unsigned
imap_mbox_handle_get_exists(ImapMboxHandle* handle)
{
  g_return_val_if_fail(handle, 0);
  return mbox_view_is_active(&handle->mbox_view) 
    ? mbox_view_cnt(&handle->mbox_view) : handle->exists;
}

unsigned
imap_mbox_handle_get_validity(ImapMboxHandle* handle)
{
  return handle->uidval;
}
unsigned
imap_mbox_handle_get_uidnext(ImapMboxHandle* handle)
{
  return handle->uidnext;
}

static void
get_delim(ImapMboxHandle* handle, int delim, ImapMboxFlags flags,
          char *folder, int *my_delim)
{
  *my_delim = delim;
}

int
imap_mbox_handle_get_delim(ImapMboxHandle* handle,
                           const char *namespace)
{
  int delim;
  guint handler_id;
  gchar * cmd, *mbx7;

  g_mutex_lock(&handle->mutex);
  /* FIXME: block other list response signals here? */
  handler_id = g_signal_connect(handle, "list-response",
				G_CALLBACK(get_delim),
				&delim);

  mbx7 = imap_utf8_to_mailbox(namespace);
  cmd = g_strdup_printf("LIST \"%s\" \"\"", mbx7);
  g_free(mbx7);
  imap_cmd_exec(handle, cmd); /* ignore return code.. */
  g_free(cmd);
  g_signal_handler_disconnect(handle, handler_id);
  g_mutex_unlock(&handle->mutex);
  return delim;

}

void
imap_mbox_handle_set_msg(ImapMboxHandle *handle, const gchar *fmt, ...)
{
	va_list va_args;

	g_free(handle->last_msg);
    va_start(va_args, fmt);
    handle->last_msg = g_strstrip(g_strdup_vprintf(fmt, va_args));
    va_end(va_args);
}

char*
imap_mbox_handle_get_last_msg(ImapMboxHandle *handle)
{
  return g_strdup(handle->state == IMHS_DISCONNECTED
		  ? "Connection severed"
		  : (handle->last_msg ? handle->last_msg : "") );
}

void
imap_mbox_handle_connect_notify(ImapMboxHandle* handle,
                                ImapMboxNotifyCb cb, void *data)
{
}

static void
imap_mbox_handle_finalize(GObject* gobject)
{
  ImapMboxHandle* handle = IMAP_MBOX_HANDLE(gobject);

  g_mutex_lock(&handle->mutex);
  if (handle->state != IMHS_DISCONNECTED) {
    handle->doing_logout = TRUE;
    imap_cmd_exec(handle, "LOGOUT");
  }
  imap_handle_disconnect(handle);
  g_free(handle->host);
  g_free(handle->mbox);
  g_free(handle->last_msg);

  g_list_foreach(handle->cmd_info, (GFunc)g_free, NULL);
  g_list_free(handle->cmd_info);
  g_hash_table_destroy(handle->status_resps);

  if (handle->thread_root != NULL)
      g_node_destroy(handle->thread_root);

  mbox_view_dispose(&handle->mbox_view);
  imap_mbox_resize_cache(handle, 0);
  g_free(handle->msg_cache);
  g_array_free(handle->flag_cache, TRUE);
  g_list_foreach(handle->acls, (GFunc)imap_user_acl_free, NULL);
  g_list_free(handle->acls);
  g_free(handle->quota_root);

  g_mutex_unlock(&handle->mutex);
  g_mutex_clear(&handle->mutex);

  G_OBJECT_CLASS(imap_mbox_handle_parent_class)->finalize(gobject);
}

typedef void (*ImapTasklet)(ImapMboxHandle*, void*);
struct tasklet {
  ImapTasklet task;
  void *data;
};
static void
imap_handle_add_task(ImapMboxHandle* handle, ImapTasklet task, void* data)
{
  struct tasklet * t = g_new(struct tasklet, 1);
  t->task = task;
  t->data = data;
  handle->tasks = g_slist_prepend(handle->tasks, t);
}

/* care needs to be taken: tasklet can trigger an IMAP command, which
   in turn would call imap_handle_process_tasks. We need to steal the
   pointer to the list.
*/

static void
imap_handle_process_tasks(ImapMboxHandle* handle)
{
  GSList *begin = handle->tasks, *l;

  handle->tasks = NULL;
  for(l = begin; l; l = l->next) {
    struct tasklet *t = (struct tasklet*)l->data;
    t->task(handle, t->data);
  }
  g_slist_foreach(begin, (GFunc)g_free, NULL);
  g_slist_free(begin);
}
    
ImapResponse
imap_mbox_handle_fetch_unlocked(ImapMboxHandle* handle, const gchar *seq, 
                       const gchar* headers[])
{
  char* cmd;
  int i;
  GString* hdr;
  ImapResponse rc;
  
  IMAP_REQUIRED_STATE1_U(handle, IMHS_SELECTED, IMR_BAD);
  hdr = g_string_new(headers[0]);
  for(i=1; headers[i]; i++) {
    if (hdr->str[hdr->len - 1] != '(' && headers[i][0] != ')')
      g_string_append_c(hdr, ' ');
    g_string_append(hdr, headers[i]);
  }
  cmd = g_strdup_printf("FETCH %s (%s)", seq, hdr->str);
  g_string_free(hdr, TRUE);
  rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
}

ImapResponse
imap_mbox_handle_fetch_env(ImapMboxHandle* handle, const gchar *seq)
{
  char* cmd;
  ImapResponse rc;
  
  IMAP_REQUIRED_STATE1_U(handle, IMHS_SELECTED, IMR_BAD);
  cmd = g_strdup_printf("FETCH %s (ENVELOPE FLAGS UID)", seq);
  rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
}


ImapMessage*
imap_mbox_handle_get_msg(ImapMboxHandle* h, unsigned seqno)
{
  g_return_val_if_fail(h, 0);
  g_return_val_if_fail(seqno-1<h->exists, NULL);
  return h->msg_cache[seqno-1];
}

const char*
imap_mbox_get_filter(ImapMboxHandle *h)
{
  return h->mbox_view.filter_str ? h->mbox_view.filter_str : "ALL";
}

/* imap_mbox_set_sort:
 */
unsigned
imap_mbox_set_sort(ImapMboxHandle *h, ImapSortKey isr, int ascending)
{
  gchar *cmd;
  const char *field;
  switch(isr) {
  default:
  case IMSO_ARRIVAL: return 1;
  case IMSO_SUBJECT: field = "SUBJECT"; break;
  case IMSO_DATE   : field = "DATE";    break;
  case IMSO_FROM   : field = "FROM";    break;
  }
  cmd= g_strdup_printf("SORT (%s%s) UTF-8 %s", field,
                       ascending ? "" : " REVERSE",
                       imap_mbox_get_filter(h));
  h->mbox_view.entries = 0; /* FIXME: I do not like this! 
                             * we should not be doing such 
                             * low level manipulations here */
  imap_cmd_exec(h, cmd);
  g_free(cmd);
  return 1;
}

/** Parse given source string expected to contain a quoted
    string. Returns the extracted, allocated string. */
static gchar*
get_quoted_string(const gchar *source, gchar const **endpos)
{
  GString *s;
  if(*source == '\0') {
    *endpos = source;
    return NULL;
  }
  if(*source != '"') { /* *source == 'N' */
    *endpos = source+1;
    return NULL;
  }
  source++;
  s = g_string_new("");
  for(;*source && *source != '"'; source++) {
    if(source[0] == '\\' && source[1])
      source++;
    g_string_append_c(s, *source);
  }
  *endpos = *source ? source+1 : source;
  return g_string_free(s, FALSE);
}

/** Appends a source string to res, quoting it with quote characters
    and prefixing any necessary characters in it with backslashes. */
static void
append_quoted_string(GString *res, const gchar *source)
{
  const gchar *p;
  if(source) {
    g_string_append_c(res, '"');
    for(p=source; *p; p++) {
      if(*p == '\\' || *p == '"') g_string_append_c(res, '\\');
      g_string_append_c(res, *p);
    }
    g_string_append_c(res, '"');
  } else {
    g_string_append_c(res, 'N'); /* N for NULL */
  }
}

/* =================================================================== */
/*            IMAP ENVELOPE HANDLING CODE                              */
/* =================================================================== */

ImapEnvelope*
imap_envelope_new()
{
  return g_malloc0(sizeof(ImapEnvelope));
}

static void
imap_envelope_free_data(ImapEnvelope* env)
{
  if(env->subject)     g_free(env->subject);
  if(env->from)        imap_address_free(env->from);
  if(env->sender)      imap_address_free(env->sender);
  if(env->replyto)     imap_address_free(env->replyto);
  if(env->to)          imap_address_free(env->to);
  if(env->cc)          imap_address_free(env->cc);
  if(env->bcc)         imap_address_free(env->bcc);
  if(env->in_reply_to) g_free(env->in_reply_to);
  if(env->message_id)  g_free(env->message_id);
}

void
imap_envelope_free(ImapEnvelope *env)
{
  g_return_if_fail(env);
  imap_envelope_free_data(env);
  g_free(env);
}

static void
concat_str(GString *s, gchar *t)
{
  g_string_append(s, t); g_free(t);
  g_string_append_c(s, ';');
}

gchar*
imap_envelope_to_string(const ImapEnvelope* env)
{
  GString *res;
  gchar *t;

  if(!env)
    return NULL;

  res = g_string_new("");
  g_string_printf(res, "%lu;", (unsigned long)env->date);
  append_quoted_string(res, env->subject);  g_string_append_c(res, ';');
  t = imap_address_to_string(env->from);    concat_str(res, t);
  t = imap_address_to_string(env->sender);  concat_str(res, t);
  t = imap_address_to_string(env->replyto); concat_str(res, t);
  t = imap_address_to_string(env->to);      concat_str(res, t);
  t = imap_address_to_string(env->cc);      concat_str(res, t);
  t = imap_address_to_string(env->bcc);     concat_str(res, t);
  append_quoted_string(res, env->in_reply_to);  g_string_append_c(res, ';');
  append_quoted_string(res, env->message_id);
  return g_string_free(res, FALSE);
}

static ImapEnvelope*
imap_envelope_from_stringi(const gchar *s, gchar const **end)
{
  ImapEnvelope *env;
  gchar *n;
  const gchar *nc = NULL;
  if(!s || !*s)
    return NULL;

  env = imap_envelope_new();
  env->date = strtol(s, &n, 10);
  s = n; if( *s++ != ';') goto done;
  env->subject = get_quoted_string(s, &nc);
  s = nc; if( *s++ != ';') goto done;
  env->from    = imap_address_from_string(s, &n);
  s = n; if( *s++ != ';') goto done;
  env->sender  = imap_address_from_string(s, &n);
  s = n; if( *s++ != ';') goto done;
  env->replyto = imap_address_from_string(s, &n);
  s = n; if( *s++ != ';') goto done;
  env->to      = imap_address_from_string(s, &n);
  s = n; if( *s++ != ';') goto done;
  env->cc      = imap_address_from_string(s, &n);
  s = n; if( *s++ != ';') goto done;
  env->bcc     = imap_address_from_string(s, &n);
  s = n; if( *s++ != ';') goto done;
  env->in_reply_to = get_quoted_string(s, &nc);
  s = nc; if( *s++ != ';') goto done;
  env->message_id  = get_quoted_string(s, &nc);

 done:
  if(end)
    *end = nc;

  return env;
}

ImapEnvelope*
imap_envelope_from_string(const gchar *s)
{
  return imap_envelope_from_stringi(s, NULL);
}

/* ================ BEGIN OF BODY STRUCTURE FUNCTIONS ================== */
ImapBody*
imap_body_new(void)
{
  ImapBody *body = g_malloc0(sizeof(ImapBody));
  body->params = g_hash_table_new_full(g_str_hash, g_str_equal, 
                                       g_free, g_free);
  return body;
}

static void
imap_body_ext_mpart_free (ImapBodyExtMPart * mpart)
{
  /* if (mpart->params) g_hash_table_destroy (mpart->params); */
  g_slist_foreach (mpart->lang, (GFunc) g_free, NULL);
  g_slist_free (mpart->lang);
}

void
imap_body_free(ImapBody* body)
{
  g_free(body->media_basic_name);
  g_free(body->media_subtype);
  g_hash_table_destroy(body->params);
  g_free(body->content_id);
  g_free(body->desc);

  if(body->envelope)
    imap_envelope_free(body->envelope);
  if (body->dsp_params)
    g_hash_table_destroy (body->dsp_params);

  g_free(body->content_dsp_other);
  g_free(body->content_uri);

  /* Ext */
  if(body->media_basic == IMBMEDIA_MULTIPART)
    imap_body_ext_mpart_free(&body->ext.mpart);
  else
    g_free(body->ext.onepart.md5);
  if(body->child) imap_body_free(body->child);
  if(body->next) imap_body_free(body->next);
  g_free(body);
}

void
imap_body_set_desc(ImapBody* body, char* str)
{
  g_free(body->desc);
  body->desc = str;
}

void
imap_body_add_param(ImapBody *body, char *key, char *val)
{
  int c;
  for(c=0; key[c]; c++)
    key[c] = tolower(key[c]);
  g_hash_table_insert(body->params, key, val);
}

const gchar*
imap_body_get_param(ImapBody *body, const gchar *key)
{
  return g_hash_table_lookup(body->params, key);
}

const gchar*
imap_body_get_dsp_param(ImapBody *body, const gchar *key)
{
  return body->dsp_params ? g_hash_table_lookup(body->dsp_params, key) : NULL;
}

gchar*
imap_body_get_mime_type(ImapBody *body)
{
  const gchar* type = NULL;
  gchar *res;
  int i;
  /* We could be returning media_basic_name but we canonize the common
     names ... */
  switch(body->media_basic) {
  case IMBMEDIA_MULTIPART:      type = "multipart"; break;
  case IMBMEDIA_APPLICATION:    type = "application"; break;
  case IMBMEDIA_AUDIO:          type = "audio"; break;
  case IMBMEDIA_IMAGE:          type = "image"; break;
  case IMBMEDIA_MESSAGE_RFC822: return g_strdup("message/rfc822"); break;
  case IMBMEDIA_MESSAGE_OTHER:  type = "message"; break;
  case IMBMEDIA_TEXT:           type = "text"; break;
  case IMBMEDIA_OTHER:          type = body->media_basic_name; break;
  }
  res = g_strconcat(type, "/", body->media_subtype, NULL);
  for(i=0; res[i]; i++)
        res[i] = tolower(res[i]);
  return res;
}

/* imap_body_get_content_type returns entire content-type line
 * available at the time. Observe that since the information is returned in
 * a transformed, canonical form since it is constructed
 * from the FETCH ENVELOPE response. */
static void
append_body_param(gpointer key, gpointer value, gpointer user_data)
{
  GString *r = (GString*)user_data;
  g_string_append(r, "; ");
  g_string_append(r, (char*)key);
  g_string_append(r, "=\"");
  g_string_append(r, (char*)value); /* FIXME: quote differently? */
  g_string_append_c(r, '"');
}

gchar*
imap_body_get_content_type(ImapBody *body)
{
  gchar *mime_type = imap_body_get_mime_type(body);
  GString *res = g_string_new(mime_type);
  g_free(mime_type);
  g_hash_table_foreach(body->params, append_body_param, res);
  return g_string_free(res, FALSE);
}

#if 0
static void
do_indent(int indent)
{ int i; for(i=0; i<indent; i++) putchar(' '); }
static void
print_body_structure(ImapBody *body, int indent)
{
  while(body) {
    gchar *type = imap_body_get_mime_type(body);
    do_indent(indent); printf("%s\n", type);
    g_free(type);
    if(body->child)
      print_body_structure(body->child, indent+3);
    body = body->next;
  }
}
#endif

static ImapBody*
get_body_from_section(ImapBody *body, const char *section)
{
  char * dot;
  int is_parent_a_message = 1;
  do {
    int no = strtol(section, NULL, 10);

    /* printf("Section: %s\n", section); print_body_structure(body, 0); */
    if(body &&
       (body->media_basic == IMBMEDIA_MULTIPART && is_parent_a_message))
      body = body->child;
    while(--no && body)
      body = body->next;
    
    if(!body) return NULL; /* non-existing section */
    dot = strchr(section, '.');
    if(dot) { 
      section = dot+1;
      is_parent_a_message =
        (body->media_basic == IMBMEDIA_MESSAGE_RFC822 ||
         body->media_basic == IMBMEDIA_MESSAGE_OTHER);
      body = body->child;
    }
  } while(dot);
  return body;
}

ImapBody*
imap_message_get_body_from_section(ImapMessage *msg,
                                   const char *section)
{
  /* FIXME: check call arguments! */
  g_return_val_if_fail(section, NULL);
  return get_body_from_section(msg->body, section);
}


unsigned
imap_sequence_length(ImapSequence *i_seq)
{
  unsigned length = 0;
  GList *l;
  for(l = i_seq->ranges; l; l = l->next) {
    ImapUidRange *r = (ImapUidRange*)l->data;
    length += r->hi - r->lo +1;
  }
  return length;
}

unsigned
imap_sequence_nth(ImapSequence *i_seq, unsigned nth)
{
  GList *l;

  g_return_val_if_fail(i_seq->ranges, 0);

  for(l = i_seq->ranges; l; l = l->next) {
    ImapUidRange *r = (ImapUidRange*)l->data;
    unsigned range_length = r->hi - r->lo +1;
    if( nth < range_length)
      return r->lo + nth;
    nth -= range_length;
  }
  g_warning("imap_sequence_nth: too large parameter; returning bogus data");
  return 0;
}

void
imap_sequence_foreach(ImapSequence *i_seq,
		      void(*cb)(unsigned uid, void *arg), void *cb_arg)
{
  GList *l;
  for(l = i_seq->ranges; l; l = l->next) {
    unsigned uid;
    ImapUidRange *iur = (ImapUidRange*)l->data;
    for(uid=iur->lo; uid<=iur->hi; uid++)
      cb(uid, cb_arg);
  }
}


void
imap_sequence_release(ImapSequence *i_seq)
{
  g_list_foreach(i_seq->ranges, (GFunc)g_free, NULL);
  i_seq->ranges = NULL;
}

void
imap_body_append_part(ImapBody* body, ImapBody* sibling)
{
  while(body->next)
    body = body->next;
  body->next = sibling;
}

void
imap_body_append_child(ImapBody* body, ImapBody* child)
{
  if(body->child) 
    imap_body_append_part(body->child, child);
  else
    body->child = child;
}
void
imap_body_set_id(ImapBody *body, char *id)
{
  body->content_id = id;
}

static void
append_pair(gpointer key, gpointer value, gpointer user_data)
{
  append_quoted_string(user_data, key);
  append_quoted_string(user_data, value);
}

static void
append_hash(GString *res, GHashTable *hash)
{
  g_string_append_c(res, '{');
  if(hash)
    g_hash_table_foreach(hash, append_pair, res);
  g_string_append_c(res, '}');
}

static GHashTable*
get_hash(const char *s, gchar const **end)
{
  GHashTable *hash = NULL;

  if(!s || *s != '{')
    return NULL;
  s++;
  while(*s && *s != '}') {
    gchar *key, *val;
    key = get_quoted_string(s, &s);
    if(!key)
      break;
    val = get_quoted_string(s, &s);
    if(!hash)
      hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_insert(hash, key, val);
  }
  if(*s == '}')
    s++;

  if(end)
    *end = s;

  return hash;
}

static void
append_onepart(GString *res, const ImapBodyExt1Part *onepart)
{
  append_quoted_string(res, onepart->md5);
}

static void
append_mpart(GString *res, const ImapBodyExtMPart *mpart)
{
  GSList *l;
  /* append_list */
  g_string_append_c(res, '(');
  for(l=mpart->lang; l; l = l->next)
    append_quoted_string(res, l->data);
  g_string_append_c(res, ')');
}

static GSList*
get_slist(const gchar *s, gchar const **end)
{
  GSList *res = NULL;
  if(!s || *s != '(')
    return NULL;
  s++;
  while( *s && *s != ')') {
    gchar *str = get_quoted_string(s, &s);
    res = g_slist_append(res, str);
  }
  if(*s == ')')
    s++;
  if(end)
    *end = s;
  return res;
}

static void
append_body(GString *res, const ImapBody *body)
{

  g_string_append_printf(res, "(%d %d", body->encoding, body->media_basic);
  append_quoted_string(res, body->media_basic_name);
  append_quoted_string(res, body->media_subtype);
  append_hash(res, body->params);
  g_string_append_printf(res, "%u %u", body->octets, body->lines);
  append_quoted_string(res, body->content_id);
  append_quoted_string(res, body->desc);

  if(body->envelope) {
    gchar *env = imap_envelope_to_string(body->envelope);
    g_string_append_c(res, '(');
    g_string_append(res, env);
    g_free(env);
    g_string_append_c(res, ')');
  } else g_string_append_c(res, 'X');

  g_string_append_printf(res, " %d", body->content_dsp);
  append_hash(res, body->dsp_params);
  append_quoted_string(res, body->content_dsp_other);
  append_quoted_string(res, body->content_uri);
  
  /* Ext */
  if(body->media_basic != IMBMEDIA_MULTIPART)
    append_onepart(res, &body->ext.onepart);
  else
    append_mpart(res, &body->ext.mpart);

  if(body->child) {
    g_string_append_c(res, '(');
    append_body(res, body->child);
    g_string_append_c(res, ')');
  }
  g_string_append_c(res, ')');
  if(body->next) {
    g_string_append_c(res, '+');
    append_body(res, body->next);
  }
}

gchar*
imap_body_to_string(const ImapBody *body)
{
  GString *res;

  if(!body)
    return NULL;

  res = g_string_new("");
  append_body(res, body);
  /* printf("Body converted to : '%s'\n", res->str); */
  return g_string_free(res, FALSE);
}

static ImapBody*
imap_body_from_stringi(const gchar *s, gchar const** end)
{
  ImapBody *body = NULL;
  GHashTable *hash;
  gchar *w;

  if (!s || *s != '(') /* Syntax error */
    goto done;

  s++;
  body = imap_body_new();

  body->encoding    = strtol(s, &w, 10); s = w;
  body->media_basic = strtol(s, &w, 10); s = w;

  body->media_basic_name = get_quoted_string(s, &s);
  body->media_subtype    = get_quoted_string(s, &s);

  hash = get_hash(s, &s);
  if(hash) {
    g_hash_table_destroy(body->params);
    body->params = hash;
  }

  body->octets = strtol(s, &w, 10); s = w;
  body->lines  = strtol(s, &w, 10); s = w;
  body->content_id = get_quoted_string(s, &s);
  body->desc = get_quoted_string(s, &s);
  if(*s == '(') {
    s++;
    body->envelope = imap_envelope_from_stringi(s+1, &s);
    if(s == NULL || *s != ')')
      goto done;
    s++;
  } else s++; /* assuming it points to 'X' */

  body->content_dsp = strtol(s+1, &w, 10); s = w;
  body->dsp_params = get_hash(s, &s);

  body->content_dsp_other = get_quoted_string(s, &s);
  body->content_uri = get_quoted_string(s, &s);
 
  /* Ext */
  if(body->media_basic != IMBMEDIA_MULTIPART)
    body->ext.onepart.md5 = get_quoted_string(s, &s);
  else {
    body->ext.mpart.lang = get_slist(s, &s);
  }

  if(*s == '(') {
    s++;
    body->child = imap_body_from_stringi(s, &s);
    if(*s != ')')
      goto done;
    s++;
  }
  s++; /* Skip trailing ')' of itself */
  if(*s == '+') {
    s++;
    body->next = imap_body_from_stringi(s, &s);
  }

 done:
  if(end)
    *end = s;
  return body;
}

ImapBody*
imap_body_from_string(const gchar *s)
{
  ImapBody *res = imap_body_from_stringi(s, NULL);
#if 0
  gchar *s1 = imap_body_to_string(res);
  if(s && *s)
    printf("Creating body from: '%s'\n"
           "New one is        : '%s'\n",
           s, s1);
  g_free(s1);
#endif
  return res;
}

/* ================ END OF BODY STRUCTURE FUNCTIONS ==================== */


/* =================================================================== */
/*             IMAP MESSAGE HANDLING CODE                              */
/* =================================================================== */

ImapMessage*
imap_message_new(void)
{
  ImapMessage * msg=g_malloc0(sizeof(ImapMessage));
  msg->rfc822size = -1;
  return msg;
}

void
imap_message_free(ImapMessage *msg)
{
  g_return_if_fail(msg);
  if(msg->envelope) imap_envelope_free(msg->envelope);
  if(msg->body)     imap_body_free    (msg->body);
  g_free(msg->fetched_header_fields);
  g_free(msg);
}

void
imap_mbox_handle_msg_deserialize(ImapMboxHandle *h, unsigned msgno,
                                 void *data)
{
  if(msgno>=1 && msgno <=h->exists && !h->msg_cache[msgno-1])
    h->msg_cache[msgno-1] = imap_message_deserialize(data);
}
/* Serialize message itself and the envelope, and the body structure
   if available. */
struct ImapMsgSerialized {
  ssize_t total_size; /* for checksumming */
  /* Message */
  ImapUID      uid;
  ImapMsgFlags flags;
  ImapDate     internal_date; /* delivery date */
  int rfc822size;
  ImapFetchType available_headers;
  gchar fetched_headers_data[];
};

void*
imap_message_serialize(ImapMessage *imsg)
{
  ssize_t tot_size;
  gchar *ptr;
  gchar *strings[3];
  size_t lengths[3];
  struct ImapMsgSerialized *imes;
  int i;

  if(!imsg->envelope) /* envelope is required */
    return NULL; 
  strings[0] = imsg->fetched_header_fields;
  strings[1] = imap_envelope_to_string(imsg->envelope);
  strings[2] = imap_body_to_string(imsg->body);
  tot_size = sizeof(struct ImapMsgSerialized);
  for(i=0; i<3; i++) {
    lengths[i] = strings[i]  ? strlen(strings[i])  : 0;
    tot_size += lengths[i] + 1;
  }

  imes = g_malloc(tot_size);
  imes->total_size = tot_size;
  /* Message */
  imes->uid           = imsg->uid;
  imes->flags         = imsg->flags;
  imes->internal_date = imsg->internal_date; /* delivery date */
  imes->rfc822size    = imsg->rfc822size;
  imes->available_headers = imsg->available_headers;

  ptr = imes->fetched_headers_data;
  for(i=0; i<3; i++) {
    if(strings[i])
      strcpy(ptr, strings[i]);
    ptr += lengths[i];  *ptr++ = '\0';
  }
  g_free(strings[1]);
  g_free(strings[2]);
  /* printf("Serialization offset: %d (tot size %d may include alignment)\n",
     ptr-(gchar*)imes, tot_size); */
  return imes;
}

/** Convert given blob to an ImapMessage structure, with properly set
    envelope and body structure fields as well. */
ImapMessage*
imap_message_deserialize(void *data)
{
  struct ImapMsgSerialized *imes = (struct ImapMsgSerialized*)data;
  ImapMessage* imsg = imap_message_new();
  gchar *ptr;

  imsg->uid = imes->uid;
  imsg->flags = imes->flags;
  imsg->internal_date = imes->internal_date; /* delivery date */
  imsg->rfc822size = imes->rfc822size;
  imsg->available_headers = imes->available_headers;
  /* Envelope */
  ptr = imes->fetched_headers_data;
  imsg->fetched_header_fields = *ptr ? g_strdup(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope = imap_envelope_from_string(ptr);
  ptr += strlen(ptr) + 1;
  imsg->body = imap_body_from_string(ptr);
  return imsg;
}

size_t
imap_serialized_message_size(void *data)
{
  struct ImapMsgSerialized *imes = (struct ImapMsgSerialized*)data;
  return imes->total_size;
}

/* =================================================================== */
/*                Imap command processing routines                     */
/* =================================================================== */
#if 0
static ImapResponse
imap_code(const gchar* resp)
{
  if(strncmp(resp+sizeof(ImapCmdTag)+1,"OK", 2) ==0)
    return IMR_OK;
  else if(strncmp(resp+sizeof(ImapCmdTag)+1,"NO", 2) ==0)
    return IMR_NO;
  else  if(strncmp(resp+sizeof(ImapCmdTag)+1,"BAD", 3) ==0) {
    g_warning("Protocol error:\n");
    return IMR_BAD;
  } else return IMR_BAD; /* nothing known */
}
#endif

/* imap_cmd_start sends the command to the server. We do it carefully
 * without using printf because our cmds can be pretty long.
 */
int
imap_cmd_start(ImapMboxHandle* handle, const char* cmd, unsigned *cmdno)
{
  ImapCmdTag tag;
  g_return_val_if_fail(handle, -1);
  
  if(IMAP_MBOX_IS_DISCONNECTED(handle))
    return -1;

  *cmdno = imap_make_tag(tag);
  net_client_write_line(NET_CLIENT(handle->sio), "%s %s", NULL, tag, cmd);
  return 1;
}

/* imap_cmd_step:
 * Reads server responses from an IMAP command, detects
 * tagged completion response, handles untagged messages.
 * Reads only as much as needed using buffered input.
 */
ImapResponse
imap_cmd_step(ImapMboxHandle* handle, unsigned lastcmd)
{
  ImapCmdTag tag;
  ImapResponse rc;
  unsigned cmdno;
  struct CmdInfo *ci;

  /* FIXME: sanity test */
  g_return_val_if_fail(handle, IMR_BAD);
  g_return_val_if_fail(handle->state != IMHS_DISCONNECTED, IMR_BAD);

  ci = cmdi_find_by_no(handle->cmd_info, lastcmd);
  if(ci && ci->completed) {
    /* The response to this command has been encountered earlier,
       send it. */
    g_debug("Sending stored response to %x and removing info.",  lastcmd);
    rc = ci->rc;
    handle->cmd_info = g_list_remove(handle->cmd_info, ci);
    g_free(ci);
    return rc;
  }

  if( imap_cmd_get_tag(handle->sio, tag, sizeof(tag))<0) {
    g_debug("IMAP connection to %s severed.", handle->host);
    imap_handle_disconnect(handle);
    return IMR_SEVERED;
  }
  /* handle untagged messages. The caller still gets its shot afterwards. */
  if (strcmp(tag, "*") == 0) {
    rc = ir_handle_response(handle);
    if(rc == IMR_BYE) {
      return handle->doing_logout ? IMR_UNTAGGED : IMR_BYE;
    }
    if (rc != IMR_OK) {
      g_warning("Encountered protocol error while handling untagged response.");
      imap_handle_disconnect(handle);
      return IMR_BAD;
    }
    return IMR_UNTAGGED;
  }

  /* server demands a continuation response from us */
  if (strcmp(tag, "+") == 0)
    return IMR_RESPOND;

  /* tagged completion code is the only alternative. */
  /* our command tags are hexadecimal numbers, at most 7 chars */
  if(sscanf(tag, "%7x", &cmdno) != 1) {
    g_warning("scanning '%s' for tag number failed. Cannot recover.", tag);
    imap_handle_disconnect(handle);
    return IMR_BAD;
  }

  rc = ir_handle_response(handle);
  /* We check whether we encountered an response to a another,
       possibly asynchronous command, not the one we are currently
       executing. We store the response in the hash table so that we
       can provide a proper response when somebody asks. */
  ci = cmdi_find_by_no(handle->cmd_info, cmdno);
  if(lastcmd != cmdno)
    g_debug("Looking for %x and encountered response to %x (%p)",
           lastcmd, cmdno, ci);
  if(ci) {
    if(ci->complete_cb && !ci->complete_cb(handle, ci->cb_data)) {
      ci->rc = rc;
      ci->completed = 1;
      g_debug("Cmd %x marked as completed with rc=%d", cmdno, rc);
    } else {
      g_debug("CmdInfo for cmd %x removed", cmdno);
      handle->cmd_info = g_list_remove(handle->cmd_info, ci);
      g_free(ci);
    }
  }
  if (handle->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;
  else
    return lastcmd == cmdno ? rc : IMR_UNTAGGED;
}

ImapResponse
imap_cmd_process_untagged(ImapMboxHandle* handle, unsigned cmdno)
{
	ImapResponse rc;

	do {
		rc = imap_cmd_step(handle, cmdno);
	} while (rc == IMR_UNTAGGED);
	return rc;
}

/**executes a command, and wait for the response from the server.
 * Also, handle untagged responses.
 * Returns ImapResponse.
 */
ImapResponse
imap_cmd_exec_cmdno(ImapMboxHandle* handle, const char* cmd,
		    unsigned *ret_cmdno)
{
  unsigned cmdno;
  ImapResponse rc;

  if(ret_cmdno) *ret_cmdno = 0;

  g_return_val_if_fail(handle, IMR_BAD);
  if (handle->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;

  /* create sequence for command */
  if (!imap_handle_idle_disable(handle)) return IMR_SEVERED;
  if (imap_cmd_start(handle, cmd, &cmdno)<0)
    return IMR_SEVERED;  /* irrecoverable connection error. */

  if(ret_cmdno) *ret_cmdno = cmdno;
  g_return_val_if_fail(handle->state != IMHS_DISCONNECTED && 1, IMR_BAD);
  if(handle->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;

  rc = imap_cmd_process_untagged(handle, cmdno);

  imap_handle_idle_enable(handle, IDLE_TIMEOUT);

  return rc;
}

/** Executes a set of commands, and wait for the response from the
 * server.  Handles all untagged responses that arrive in meantime.
 * Returns ImapResponse.
 * @param handle the IMAP connection handle
 * @param cmds the NULL-terminated vector of IMAP commands.
 * @param rc_to_return the 0-based number of the "important" IMAP
 * command in the sequence that we want to have the return code for.
 */
ImapResponse
imap_cmd_exec_cmds(ImapMboxHandle* handle, const char** cmds,
		   unsigned rc_to_return)
{
  unsigned cmd_count;
  ImapResponse rc = IMR_OK, ret_rc = IMR_OK;
  unsigned *cmdnos;

  g_return_val_if_fail(handle, IMR_BAD);
  if (handle->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;

  if (!imap_handle_idle_disable(handle)) return IMR_SEVERED;

  for (cmd_count=0; cmds[cmd_count]; ++cmd_count)
    ;
  cmdnos = g_malloc(cmd_count*sizeof(unsigned));
  
  for (cmd_count=0; cmds[cmd_count]; ++cmd_count) {
    if (imap_cmd_start(handle, cmds[cmd_count], &cmdnos[cmd_count])<0) {
      rc = IMR_SEVERED;   /* irrecoverable connection error. */
      break;
    }
  }
  if (rc == IMR_OK) {
    g_return_val_if_fail(handle->state != IMHS_DISCONNECTED && 1, IMR_BAD);
    if(handle->state == IMHS_DISCONNECTED)
      ret_rc = IMR_SEVERED;
    else {
      for (cmd_count=0; cmds[cmd_count]; ++cmd_count) {
    	  rc = imap_cmd_process_untagged(handle, cmdnos[cmd_count]);

	if ( !(rc == IMR_OK || rc == IMR_NO || rc == IMR_BAD) ) {
	  ret_rc = rc;
	  break;
	}

	if (cmd_count == rc_to_return)
	  ret_rc = rc;

      }
    }
  }
  g_free(cmdnos);
      
  imap_handle_idle_enable(handle, IDLE_TIMEOUT);

  return ret_rc;
}

static GString*
imap_get_string_with_lookahead(NetClientSioBuf *sio, int c)
{ /* string */  
  GString *res = NULL;
  if(c=='"') { /* quoted */
    res = g_string_new("");
    while( (c=sio_getc(sio)) != '"' && c != EOF) {
      if(c== '\\')
        c = sio_getc(sio);
      g_string_append_c(res, c);
    }
  } else { /* this MUST be literal */
    char buf[15];
    int len;
    if(c=='~') /* BINARY extension literal8 indicator */
      c = sio_getc(sio);
    if(c!='{') {
      return NULL; /* ERROR */
    }

    c = imap_get_atom(sio, buf, sizeof(buf));
    len = strlen(buf); 
    if(len==0 || buf[len-1] != '}') return NULL;
    buf[len-1] = '\0';
    len = strtol(buf, NULL, 10);
    if( c != 0x0d) { g_debug("lit1:%d",c); return NULL;}
    if( (c=sio_getc(sio)) != 0x0a) { g_debug("lit1:%d",c); return NULL;}
    res = g_string_sized_new(len+1);
    if(len>0) sio_read(sio, res->str, len);
    res->len = len;
    res->str[len] = '\0';
  }
  return res;
}

/* see the spec for the definition of string */
static char*
imap_get_string(NetClientSioBuf *sio)
{
  GString * s = imap_get_string_with_lookahead(sio, sio_getc(sio));
  return s ? g_string_free(s, FALSE) : NULL;
}

static gboolean
imap_is_nil (NetClientSioBuf *sio, int c)
{
  return g_ascii_toupper (c) == 'N' && g_ascii_toupper (sio_getc (sio)) == 'I'
    && g_ascii_toupper (sio_getc (sio)) == 'L';
}

/* see the spec for the definition of nstring */
static char*
imap_get_nstring(NetClientSioBuf *sio)
{
  int c = sio_getc(sio);
  if(toupper(c)=='N') { /* nil */
    sio_getc(sio); sio_getc(sio); /* ignore i and l */
    return NULL;
  } else {
    GString *s = imap_get_string_with_lookahead(sio, c);
    return s ? g_string_free(s, FALSE) : NULL;
  }
}

/* see the spec for the definition of astring */
#define IS_ASTRING_CHAR(c) (strchr("(){ %*\"\\", (c))==0&&(c)>0x1F&&(c)!=0x7F)
static char*
imap_get_astring(NetClientSioBuf *sio, int* lookahead)
{
  char* res;
  int c = sio_getc(sio);

  if(IS_ASTRING_CHAR(c)) {
    GString *str = g_string_new("");
    do {
      g_string_append_c(str, c);
      c = sio_getc(sio);
    } while(IS_ASTRING_CHAR(c));
    res = g_string_free(str, FALSE);
    *lookahead = c;
  } else {
    res = g_string_free(imap_get_string_with_lookahead(sio, c), FALSE);
    *lookahead = sio_getc(sio);
  }
  return res;
}

/* nstring / literal8 as in the BINARY extension */
static GString*
imap_get_binary_string(NetClientSioBuf *sio)
{
  int c = sio_getc(sio);
  if(toupper(c)=='N') { /* nil */
    sio_getc(sio); sio_getc(sio); /* ignore i and l */
    return g_string_new("");
  } else
    return imap_get_string_with_lookahead(sio, c);
}

/* this file contains all the response handlers as defined in
   draft-crspin-imapv-20.txt. 
  
   According to section 7 of this draft, "the client MUST be prepared
   to accept any response at all times".

   The code is closely based on sectin 9 - formal syntax.
*/

#include "imap-handle.h"

static int
ignore_bad_charset(NetClientSioBuf *sio, int c)
{
  while(c==' ') {
    gchar * astring = imap_get_astring(sio, &c); 
    g_free(astring);
  }
  if(c != ')')
    g_warning("ignore_bad_charset: expected ')' got '%c'", c);
  else c = sio_getc(sio);
  return c;
}

static int
ir_permanent_flags(ImapMboxHandle *h)
{
  int c;
  while( (c=sio_getc(h->sio)) != EOF && c != ']')
    ;
  return c;
}

static int
ir_capability_data(ImapMboxHandle *handle)
{
  /* ordered identically as ImapCapability constants */
  static const char* capabilities[] = {
    "IMAP4", "IMAP4rev1", "STATUS",
    "AUTH=ANONYMOUS", "AUTH=CRAM-MD5", "AUTH=GSSAPI", "AUTH=PLAIN",
    "ACL", "RIGHTS=", "BINARY", "CHILDREN",
    "COMPRESS=DEFLATE",
    "ESEARCH", "IDLE", "LITERAL+",
    "LOGINDISABLED", "MULTIAPPEND", "NAMESPACE", "QUOTA", "SASL-IR",
    "SCAN", "STARTTLS",
    "SORT", "THREAD=ORDEREDSUBJECT", "THREAD=REFERENCES",
    "UIDPLUS", "UNSELECT"
  };
  unsigned x;
  int c;
  char atom[LONG_STRING];

  memset (handle->capabilities, 0, sizeof (handle->capabilities));
  
  do {
    c = imap_get_atom(handle->sio, atom, sizeof(atom));
    for (x=0; x<G_N_ELEMENTS(capabilities); x++)
      if (g_ascii_strncasecmp(atom, capabilities[x],
                              strlen(capabilities[x])) == 0) {
	handle->capabilities[x] = 1;
	break;
      }
  } while(c==' ');
  handle->has_capabilities = TRUE;
  return c;
}

typedef void (*ImapUidRangeCb)(ImapUidRange *iur, void *arg);

static ImapResponse
imap_get_sequence(ImapMboxHandle *h, ImapUidRangeCb seq_cb, void *seq_arg)
{
  char value[30];
  gchar *p;
  int offset = 0;
  int c = ' ';

  do {
    size_t value_len;
    if(c != '\r' && c != '\n') /* Dont try to read beyond the line. */
      c = imap_get_atom(h->sio, value + offset, sizeof(value)-offset);
    value_len = strlen(value);

    if(seq_cb) { /* makes sense to parse it ... */
      static const unsigned LENGTH_OF_LARGEST_UNSIGNED = 10;
      ImapUidRange seq;

      for(p=value; *p &&
	    (unsigned)(p-value) <=
	    sizeof(value)-(2*LENGTH_OF_LARGEST_UNSIGNED+1); ) {
	if(sscanf(p, "%30u", &seq.lo) != 1)
	  return IMR_PROTOCOL;
	while(*p && isdigit(*p)) p++;
	if( *p == ':') {
	  p++;
	  if(sscanf(p, "%30u", &seq.hi) != 1)
	    return c;
	} else seq.hi = seq.lo;

	seq_cb(&seq, seq_arg);

	while(*p && isdigit(*p)) p++;
	if(*p == ',') p++;
      } /* End of for */
	
      /* Reuse what's left. */
      if(*p) {
	offset = value_len - (p-value);
	memmove(value, p, offset+1);
      } else offset = 0;

    } /* End of if(search_cb) */

  }  while (isdigit(c)|| c == ':' || c == ',' || offset);

  if(c != EOF)
    sio_ungetc(h->sio);
  return IMR_OK;
}

static void
append_uid_range(ImapUidRange *iur, GList **dst)
{
  ImapUidRange *iur_copy = g_new(ImapUidRange, 1);
  /* printf("Prepending %u:%u\n", iur->lo, iur->hi); */
  iur_copy->lo = iur->lo;
  iur_copy->hi = iur->hi;
  *dst = g_list_prepend(*dst, iur_copy);
}

static ImapResponse
ir_get_append_copy_uids(ImapMboxHandle *h, gboolean append_only)
{
  int c;
  char buf[12];

  if( (c=imap_get_atom(h->sio, buf, sizeof(buf))) == EOF)
    return IMR_PROTOCOL;
  h->uidplus.dst_uid_validity = strtol(buf, NULL, 10);

  if(c != ' ')
    return IMR_PROTOCOL;

  if(!append_only) {
    ImapResponse rc;

    if( (rc = imap_get_sequence(h, NULL, NULL)) != IMR_OK)
      return rc;
    if( (c=sio_getc(h->sio)) != ' ') {
      g_debug("Expected ' ' found '%c'", c);
      return IMR_PROTOCOL;
    }
  }
  return imap_get_sequence(h, (ImapUidRangeCb)append_uid_range,
			   &h->uidplus.dst);
}

static ImapResponse
ir_resp_text_code(ImapMboxHandle *h)
{
  static const char* resp_text_code[] = {
    "ALERT", "BADCHARSET", "CAPABILITY","PARSE", "PERMANENTFLAGS",
    "READ-ONLY", "READ-WRITE", "TRYCREATE", "UIDNEXT", "UIDVALIDITY",
    "UNSEEN", "APPENDUID", "COPYUID"
  };
  unsigned o;
  char buf[128];
  int c = imap_get_atom(h->sio, buf, sizeof(buf));
  ImapResponse rc = IMR_OK;

  for(o=0; o<G_N_ELEMENTS(resp_text_code); o++)
    if(g_ascii_strcasecmp(buf, resp_text_code[o]) == 0) break;

  switch(o) {
  case 0: rc = IMR_ALERT;        break;
  case 1: c = ignore_bad_charset(h->sio, c); break;
  case 2: c = ir_capability_data(h); break;
  case 3: rc = IMR_PARSE;        break;
  case 4: c = ir_permanent_flags(h); break;
  case 5: h->readonly_mbox = TRUE;  /* read-only */; break;
  case 6: h->readonly_mbox = FALSE; /* read-write */; break;
  case 7: /* ignore try-create */; break;
  case 8:
    c = imap_get_atom(h->sio, buf, sizeof(buf));
    h->uidnext = strtol(buf, NULL, 10);
    break;
  case 9:
    c = imap_get_atom(h->sio, buf, sizeof(buf));
    h->uidval = strtol(buf, NULL, 10);
    break;
  case 10:
    c = imap_get_atom(h->sio, buf, sizeof(buf));
    h->unseen =strtol(buf, NULL, 10);
    break;
  case 11: /* APPENDUID */
    if( (rc=ir_get_append_copy_uids(h, TRUE)) != IMR_OK)
      return rc;
    /* printf("APPENDUID: uid_validity=\n"); */
    c = sio_getc(h->sio);
    break;
  case 12: /* COPYUID */
    /* printf("Copyuid\n"); */
    if( (rc=ir_get_append_copy_uids(h, FALSE)) != IMR_OK)
      return rc;
    c = sio_getc(h->sio);
    break;
  default: while( c != ']' && (c=sio_getc(h->sio)) != EOF) ; break;
  }
  if(c != ']')
    g_debug("ir_resp_text_code, on exit c=%c", c);
  return c == ']' ? rc : IMR_PROTOCOL;
}

static ImapResponse
ir_ok(ImapMboxHandle *h)
{
  ImapResponse rc;
  char line[2048];
  int l, c = sio_getc(h->sio);

  if(c == '[') {
    /* look for information response codes here: section 7.1 of the draft */
    rc = ir_resp_text_code(h);
    if(sio_getc(h->sio) != ' ') rc = IMR_PROTOCOL;
    if (sio_gets(h->sio, line, sizeof(line)) == NULL)
      rc = IMR_SEVERED;
  } else {
    line[0] = c;
    if (sio_gets(h->sio, line+1, sizeof(line)-1) == NULL)
      rc = IMR_SEVERED;
    else 
      rc = IMR_OK;
  }
  if(rc == IMR_PARSE)
    rc = IMR_OK;
  else if (rc != IMR_SEVERED && (l=strlen(line))>0 ) {
    l = MAX(l, 2);
    line[l-2] = '\0'; 
    imap_mbox_handle_set_msg(h, _("IMAP response: %s"), line);
    if(h->info_cb)
      h->info_cb(h, rc, line, h->info_arg);
    else
      g_debug("INFO : '%s'", line);
    rc = IMR_OK; /* in case it was IMR_ALERT */
  }
  return rc;
}

static ImapResponse
ir_no(ImapMboxHandle *h)
{
  char line[LONG_STRING];

  sio_gets(h->sio, line, sizeof(line));
  /* look for information response codes here: section 7.1 of the draft */
  if( strlen(line)>2) {
    imap_mbox_handle_set_msg(h, _("IMAP response: %s"), line);
    if(h->info_cb)
      h->info_cb(h, IMR_NO, line, h->info_arg);
    else
      g_debug("WARN : '%s'", line);
  }
  return IMR_NO;
}

static ImapResponse
ir_bad(ImapMboxHandle *h)
{
  char line[LONG_STRING];
  sio_gets(h->sio, line, sizeof(line));
  /* look for information response codes here: section 7.1 of the draft */
  if( strlen(line)>2) {
    imap_mbox_handle_set_msg(h, _("IMAP response: %s"), line);
    if(h->info_cb)
      h->info_cb(h, IMR_BAD, line, h->info_arg);
    else
      g_debug("ERROR: %s", line);
  }
  return IMR_BAD;
}

static ImapResponse
ir_preauth(ImapMboxHandle *h)
{
	ImapResponse resp;

	resp = ir_ok(h);
	if ((resp == IMR_OK) && (imap_mbox_handle_get_state(h) == IMHS_CONNECTED)) {
		imap_mbox_handle_set_state(h, IMHS_AUTHENTICATED);
	}
	return resp;
}

/* ir_bye:
   NOTE: we do not invalidate cache here, it may have use in spite of
   the closed connection.
*/
static ImapResponse
ir_bye(ImapMboxHandle *h)
{
  char line[LONG_STRING];
  sio_gets(h->sio, line, sizeof(line));
  if(!h->doing_logout) {/* it is not we, so it must be the server */
    imap_mbox_handle_set_msg(h, _("IMAP response: %s"), line);
    imap_mbox_handle_set_state(h, IMHS_DISCONNECTED);
    /* we close the connection here unless we are doing logout. */
    if(h->sio) {
      g_object_unref(h->sio); h->sio = NULL;
    }
  }
  return IMR_BYE;
}

static ImapResponse
ir_check_crlf(ImapMboxHandle *h, int c)
{
  if( c != 0x0d) {
    g_debug("CR:%d",c);
    return IMR_PROTOCOL;
  }
  if( (c=sio_getc(h->sio)) != 0x0a) {
    g_debug("LF:%d",c);
    return IMR_PROTOCOL;
  }
  return IMR_OK;
}

static ImapResponse
ir_capability(ImapMboxHandle *handle)
{
  int c = ir_capability_data(handle);
  return ir_check_crlf(handle, c);
}
/* follow mailbox-list syntax (See rfc) */
static ImapResponse
ir_list_lsub(ImapMboxHandle *h, ImapHandleSignal signal)
{
  const char* mbx_flags[] = {
    "Marked", "Unmarked", "Noselect", "Noinferiors",
    "HasChildren", "HasNoChildren"
  };
  ImapMboxFlags flags = 0;
  char buf[LONG_STRING], *s, *mbx;
  int c, delim;
  ImapResponse rc;

  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;

  /* [mbx-list-flags] */
  c=sio_getc(h->sio);
  while(c != ')') {
    unsigned i;
    if(c!= '\\') return IMR_PROTOCOL;
    c = imap_get_atom(h->sio, buf, sizeof(buf));
    for(i=0; i< G_N_ELEMENTS(mbx_flags); i++) {
      if(g_ascii_strcasecmp(buf, mbx_flags[i]) ==0) {
        IMAP_MBOX_SET_FLAG(flags, i);
        break;
      }
    }
    if( c != ' ' && c != ')') return IMR_PROTOCOL;
    if(c==' ') c = sio_getc(h->sio);
  }
  if(sio_getc(h->sio) != ' ') return IMR_PROTOCOL;
  if( (delim=sio_getc(h->sio)) == '"') 
    { delim=sio_getc(h->sio); while(sio_getc(h->sio)!= '"'); }
  else {
    if(delim            != 'N' ||
       sio_getc(h->sio) != 'I' ||
       sio_getc(h->sio) != 'L') return IMR_PROTOCOL;
    delim = '\0'; /* NIL */
  }
  if(sio_getc(h->sio) != ' ') return IMR_PROTOCOL;
  /* mailbox */
  s = imap_get_astring(h->sio, &c);
  mbx = imap_mailbox_to_utf8(s);
  rc = ir_check_crlf(h, c);
  g_signal_emit(h, imap_mbox_handle_signals[signal],
                0, delim, flags, mbx);
  g_free(s);
  g_free(mbx);
  return rc;
}

static ImapResponse
ir_list(ImapMboxHandle *h)
{
  return ir_list_lsub(h, LIST_RESPONSE);
}

static ImapResponse
ir_lsub(ImapMboxHandle *h)
{
  return ir_list_lsub(h, LSUB_RESPONSE);
}

/* 7.2.4 STATUS Response */
const char* imap_status_item_names[5] = {
  "MESSAGES", "RECENT", "UIDNEXT", "UIDVALIDITY", "UNSEEN" };
static ImapResponse
ir_status(ImapMboxHandle *h)
{
  int c;
  char *name;
  struct ImapStatusResult *resp;

  name = imap_get_astring(h->sio, &c);
  resp = g_hash_table_lookup(h->status_resps, name);
  if(c                != ' ') {g_free(name); return IMR_PROTOCOL;}
  if(sio_getc(h->sio) != '(') {g_free(name); return IMR_PROTOCOL;}
  do {
    char item[13], count[13]; /* longest than UIDVALIDITY */
    c = imap_get_atom(h->sio, item, sizeof(item));
    if(c == ')') break;
    if(c != ' ') {g_free(name); return IMR_PROTOCOL;}
    c = imap_get_atom(h->sio, count, sizeof(count));
    /* FIXME: process the response */
    if(resp) {
      unsigned idx, i;
      for(idx=0; idx<G_N_ELEMENTS(imap_status_item_names); idx++)
        if(g_ascii_strcasecmp(item, imap_status_item_names[idx]) == 0)
          break;
      for(i= 0; resp[i].item != IMSTAT_NONE; i++) {
        if(resp[i].item == idx) {
          if (sscanf(count, "%13u", &resp[i].result) != 1) {
            g_free(name);
            return IMR_PROTOCOL;
          }
          break;
        }
      }
    }
  } while(c == ' ');
  g_free(name);
  /* g_return_val_if-fail(c == ')', IMR_BAD) */
  return ir_check_crlf(h, sio_getc(h->sio));
}

static void
esearch_cb(ImapUidRange *iur, void *arg)
{
  ImapMboxHandle *h = (ImapMboxHandle*)arg;
  unsigned i;
  for(i=iur->lo; i<= iur->hi; i++)
    h->search_cb(h, i, h->search_arg);
}

/** Process ESEARCH response. Consult RFC4466 and RFC4731 before
   modification.  */
static ImapResponse
ir_esearch(ImapMboxHandle *h)
{
  char atom[LONG_STRING];
  int c = sio_getc(h->sio);
  if(c == '(') { /* search correlator */
    gchar *str;
    c = imap_get_atom(h->sio, atom, sizeof(atom));
    if(c == EOF) return IMR_SEVERED;
    if(g_ascii_strcasecmp(atom, "TAG")) { /* TAG is the only acceptable response here! */
      g_debug("ESearch expected TAG encountered %s", atom);
      return IMR_PROTOCOL; 
    }
    if(c != ' ')
      return IMR_PROTOCOL;
    str = imap_get_string(h->sio);
    /* printf("ESearch response for tag %s\n", str); */
    g_free(str);
    if( (c = sio_getc(h->sio)) != ')') {
      return c == EOF ? IMR_SEVERED : IMR_PROTOCOL;
    }
    c = sio_getc(h->sio);
  }
  if(c == EOF) return IMR_SEVERED;  
  if (c == '\r' || c == '\n')
    return ir_check_crlf(h, c);
  /* Now, an atom has to follow */
  c = imap_get_atom(h->sio, atom, sizeof(atom));

  if(g_ascii_strcasecmp(atom, "UID") == 0) {
    c = imap_get_atom(h->sio, atom, sizeof(atom));
  }
  if(c == EOF) return IMR_SEVERED;

  while(c == ' ') { /* search-return-data in rfc4466 speak */
    ImapResponse rc;
    /* atom contains search-modifier-name, time to fetch
       search-return-value. In ESEARCH, it is always an
       tagged-ext-simple=sequence-set/number, which are an atoms, so
       we cut the corners here. We get values in chunks.  The chunk
       size is pretty arbitrary as long as it can fit two largest
       possible 32-bit unsigned numbers and a colon. */
    if ( (rc=imap_get_sequence(h, esearch_cb, h)) != IMR_OK)
      return rc;

    if( (c=sio_getc(h->sio)) == ' ')
      c = imap_get_atom(h->sio, atom, sizeof(atom));
  }

  return ir_check_crlf(h, c);
}

static ImapResponse
ir_search(ImapMboxHandle *h)
{
  int c;
  char seq[12];

  while ((c=imap_get_atom(h->sio, seq, sizeof(seq))), seq[0]) {
    if(h->search_cb)
      h->search_cb(h, strtol(seq, NULL, 10), h->search_arg);
    if(c == '\r') break;
  }
  return ir_check_crlf(h, c);
}

/* ir_sort: sort response handler. clears current view and creates a
   new one.
*/
/* draft-ietf-imapext-sort-13.txt:
 * sort-data = "SORT" *(SP nz-number) */
static ImapResponse
ir_sort(ImapMboxHandle *h)
{
  int c;
  char seq[12];
  while ((c=imap_get_atom(h->sio, seq, sizeof(seq))), seq[0]) {
    mbox_view_append_no(&h->mbox_view, strtol(seq, NULL, 10));
    if(c == '\r') break;
  }
  return ir_check_crlf(h, c);
}

static ImapResponse
ir_flags(ImapMboxHandle *h)
{
  /* FIXME: implement! */
  net_client_siobuf_discard_line(h->sio, NULL);
  return IMR_OK;
}

static ImapResponse
ir_exists(ImapMboxHandle *h, unsigned seqno)
{
  unsigned old_exists = h->exists;
  ImapResponse rc;
  if ((h->state != IMHS_AUTHENTICATED) && (h->state != IMHS_SELECTED)) {
    /* bad state for a response to SELECT or EXAMINE, see RFC 3501, Sect. 6.4. and 7.3.1. */
    g_info("received EXISTS response in bad state %d", h->state);
    return IMR_PROTOCOL;
  }

  rc = ir_check_crlf(h, sio_getc(h->sio));
  imap_mbox_resize_cache(h, seqno);
  mbox_view_resize(&h->mbox_view, old_exists, seqno);

  g_signal_emit(h, imap_mbox_handle_signals[EXISTS_NOTIFY], 0);
                
  return rc;
}

static ImapResponse
ir_recent(ImapMboxHandle *h, unsigned seqno)
{
  if ((h->state != IMHS_AUTHENTICATED) && (h->state != IMHS_SELECTED)) {
    /* bad state for a response to SELECT or EXAMINE, see RFC 3501, Sect. 6.4. and 7.3.2. */
    g_info("received RECENT response in bad state %d", h->state);
    return IMR_PROTOCOL;
  }

  h->recent = seqno;
  /* FIXME: send a signal here! */
  return ir_check_crlf(h, sio_getc(h->sio));
}

static ImapResponse
ir_expunge(ImapMboxHandle *h, unsigned seqno)
{
  ImapResponse rc;

  if (h->state != IMHS_SELECTED) {
    /* does not make sense in any other state, see RFC 3501, Sect. 6.4. and 7.4.1. */
    g_info("received EXPUNGE response in bad state %d", h->state);
    return IMR_PROTOCOL;
  }

  if (seqno > h->exists) {
    g_info("received EXPUNGE %u response with only %u messages", seqno, h->exists);
    return IMR_PROTOCOL;
  }

  rc = ir_check_crlf(h, sio_getc(h->sio));
  g_signal_emit(h, imap_mbox_handle_signals[EXPUNGE_NOTIFY],
		0, seqno);
  
  /* Current code guarantees that h->flag_cache->len == h->exists, so
   * the above guard means that it is safe to remove seqno - 1 from
   * h->flag_cache; we will assert that, to catch any changes in the
   * future: */
  g_assert(h->flag_cache->len == h->exists);
  g_array_remove_index(h->flag_cache, seqno-1);

  /* Similarly, current code guarantees that h->msg_cache is allocated
   * at least h->exists elements, so it is safe to dereference
   * h->msg_cache[seqno - 1]; however, it is a plain C array, so we
   * cannot check that here, just keep our fingers crossed: */
  if(h->msg_cache[seqno-1] != NULL)
    imap_message_free(h->msg_cache[seqno-1]);
  while(seqno<h->exists) {
    h->msg_cache[seqno-1] = h->msg_cache[seqno];
    seqno++;
  }
  h->exists--;
  mbox_view_expunge(&h->mbox_view, seqno);
  return rc;
}

static void
flags_tasklet(ImapMboxHandle *h, void *data)
{
  unsigned seqno = GPOINTER_TO_UINT(data);
  if(h->flags_cb)
    h->flags_cb(1, &seqno, h->flags_arg);
}

#define CREATE_IMSG_IF_NEEDED(h,seqno) \
  if((h)->msg_cache[seqno-1] == NULL) \
     (h)->msg_cache[(seqno)-1] = imap_message_new();

static ImapResponse
ir_msg_att_flags(ImapMboxHandle *h, int c, unsigned seqno)
{
  unsigned i;
  ImapMessage *msg;
  ImapFlagCache *flags;

  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;
  CREATE_IMSG_IF_NEEDED(h, seqno);
  msg = h->msg_cache[seqno-1];
  msg->flags = 0;

  do {
    char buf[LONG_STRING];
    c = imap_get_flag(h->sio, buf, sizeof(buf));
    for(i=0; i<G_N_ELEMENTS(imap_msg_flags); i++)
      if(buf[0] == '\\' && g_ascii_strcasecmp(imap_msg_flags[i], buf+1) == 0) {
        msg->flags |= 1<<i;
        break;
      }
  } while(c!=-1 && c != ')');

  flags = &g_array_index(h->flag_cache, ImapFlagCache, seqno-1);
  flags->flag_values = msg->flags;
  flags->known_flags = ~0; /* all of them are known */

  if(h->flags_cb)
    imap_handle_add_task(h, flags_tasklet, GUINT_TO_POINTER(seqno));
  return IMR_OK;
}

/* RFC 2087, sect. 5.2: "<mailbox> [<quota root> [<quota root> ...}}" */
static ImapResponse
ir_quotaroot(ImapMboxHandle *h)
{
  int eol;
  char *mbox;
  ImapResponse retval = IMR_NO;
  char *mbx7 = imap_utf8_to_mailbox(h->mbox);

  free(h->quota_root);
  h->quota_root = NULL;

  /* get the mailbox and the first quota root */
  mbox = imap_get_astring(h->sio, &eol);
  if (mbox) {
    if (strcmp(mbox, mbx7))
      g_debug("expected QUOTAROOT for %s, not for %s", mbx7, mbox);
    else {
      if (eol == ' ')
        h->quota_root = imap_get_astring(h->sio, &eol);
      if (eol != '\n')
        EAT_LINE(h, eol);
      retval = IMR_OK;
    }
  }
  free(mbx7);
  free(mbox);
  return retval;
}

/* RFC 2087, sect. 5.1: "<mailbox> [<resource> [<resource> ...}}" */
static ImapResponse
ir_quota(ImapMboxHandle *h)
{
  int c;
  char *root;
  ImapResponse retval = IMR_NO;

  /* get the root */
  root = imap_get_astring(h->sio, &c);
  h->quota_max_k = h->quota_used_k = 0;
  if (root) {
    if (strcmp(root, h->quota_root))
      g_debug("expected QUOTA for %s, not for %s", h->quota_root,
              root);
    else {
      while ((c = sio_getc(h->sio)) != -1 && c == ' ');
      if (c == '(') {
        do {
          char resource[32];
          
          c = imap_get_atom(h->sio, resource, 32);
          if (c == ' ') {
            char usage[16];
            char limit[16];

            imap_get_atom(h->sio, usage, 16);
            c = imap_get_atom(h->sio, limit, 16);

            /* ignore other limits than 'STORAGE' */
            if (!strcmp(resource, "STORAGE")) {
              char *endptr1;
              char *endptr2;

              h->quota_used_k = strtoul(usage, &endptr1, 10);
              h->quota_max_k = strtoul(limit, &endptr2, 10);
              if (*endptr1 != '\0' || *endptr2 != '\0') {
                g_debug("bad QUOTA '%s %s %s'", resource, usage,
                        limit);
                h->quota_max_k = h->quota_used_k = 0;
                c = ')';
              } else
                retval = IMR_OK;
            }
          }
        } while (c != ')');
      }
      EAT_LINE(h, c);
    }
  }

  free(root);
  return retval;
}

/** \brief Interpret a RFC 4314 ACL
 *
 * \param h IMAP mailbox handle
 * \param eject a string containing all characters which shall terminate
 *        scanning the ACL
 * \param acl filled with the extracted ACL's, or IMAP_ACL_NONE on error
 * \param eol if not NULL, filled with 1 or 0 to indicate if the end of line
 *        has been reached
 * \return IMAP response code
 *
 * Scan h's input stream for valid ACL flags (lrswipkxtea).  Note that 'c', 'd'
 * and cr are ignored according to RFC 4314, sect. 2.1.1.
 * But note also that a server that complies with the older RFC 2086 and not
 * with RFC 4314 uses 'c' and 'd'; we can distinguish this case because it
 * does not advertise "RIGHTS=" capability.
 */
static ImapResponse
extract_acl(ImapMboxHandle *h, const char *eject, ImapAclType *acl, int *eol)
{
  static const char* rights = "lrswipkxtea";
  static const char* ignore = "cd\r";
  int c;
  char* p;

  if (!imap_mbox_handle_can_do(h, IMCAP_RIGHTS)) {
    /* workaround for RFC 2086- but not RFC 4314-compliant server */
    rights = "lrswipkxteacd";
  }
  *acl = IMAP_ACL_NONE;
  while ((c = sio_getc(h->sio)) != -1 && !strchr(eject, c)) {
    if ((p = strchr(rights, c))) {
      *acl |= 1 << (p - rights);
    } else if (!strchr(ignore, c)) {
      EAT_LINE(h, c);
      if (eol)
        *eol = TRUE;
      return IMR_NO;
    }
  }
  if (eol)
    *eol = (c == '\n');
  return IMR_OK;
}

/* RFC 4314, sect. 3.8: "<mailbox> <rights>" */
static ImapResponse
ir_myrights(ImapMboxHandle *h)
{
  int eol;
  char *mbox;
  ImapResponse retval = IMR_NO;
  char *mbx7 = imap_utf8_to_mailbox(h->mbox);

  mbox = imap_get_astring(h->sio, &eol);
  if (mbox && eol == ' ') {
    if (strcmp(mbox, mbx7))
      g_debug("expected MYRIGHTS for %s, not for %s", mbx7, mbox);
    else {
      retval = extract_acl(h, "\n", &h->rights, NULL);
      h->has_rights = 1;
    }
  }
  free(mbx7);
  free(mbox);
  return retval;
}

/* helper: free an ImapUserAclType */
void
imap_user_acl_free(ImapUserAclType *acl)
{
  if (acl)
    g_free(acl->uid);
  g_free(acl);
}

/* RFC 4314, sect. 3.6: "<mailbox> [[<uid> <rights>] <uid> <rights> ...]" */
static ImapResponse
ir_getacl(ImapMboxHandle *h)
{
  char *mbox;
  char *mbx7;
  ImapResponse retval;
  int eol;

  g_list_foreach(h->acls, (GFunc)imap_user_acl_free, NULL);
  g_list_free(h->acls);
  h->acls = NULL;

  mbox = imap_get_astring(h->sio, &eol);
  if (!mbox)
    return IMR_NO;

  mbx7 = imap_utf8_to_mailbox(h->mbox);
  if (strcmp(mbox, mbx7)) {
    g_debug("expected ACL for %s, not for %s", mbx7, mbox);
    retval = IMR_NO;
  } else if (eol == '\n') {
    retval = IMR_OK;
  } else {
    int c;
    GString *uid;

    retval = IMR_OK;
    do {
      ImapAclType acl_flags;
      ImapUserAclType *acl;

      uid = g_string_new("");
      while ((c = sio_getc(h->sio)) != -1 && c != '\n' && c != ' ')
        uid = g_string_append_c(uid, c);
      if (c != ' ') {
        g_string_free(uid, TRUE);
        EAT_LINE(h, c);
        retval = IMR_NO;
      } else {
        if (extract_acl(h, " \n", &acl_flags, &eol) != IMR_OK) {
          g_string_free(uid, TRUE);
          retval = IMR_NO;
        } else {
          acl = g_new(ImapUserAclType, 1);
          acl->uid = g_string_free(uid, FALSE);
          acl->acl = acl_flags;
          h->acls = g_list_append(h->acls, acl);
        }
      }
    } while (retval == IMR_OK && !eol);
  }
  free(mbox);
  free(mbx7);
  return retval;
}

ImapAddress*
imap_address_new(gchar *name, gchar *addr_spec)
{
  ImapAddress *res = g_new(ImapAddress, 1);
  res->name = name;
  res->addr_spec = addr_spec;
  res->next = NULL;
  return res;
}

void
imap_address_free(ImapAddress* addr)
{
  while(addr) {
    ImapAddress* next = addr->next;
    g_free(addr->name);
    g_free(addr->addr_spec);
    g_free(addr);
    addr = next;
  }
}

static ImapAddress*
imap_address_from_string(const gchar *string, gchar **n)
{
  const gchar *t;
  gchar *comment, *mailbox;
  ImapAddress *res = NULL;

  comment = get_quoted_string(string, &t);
  if(*t == ' ') {
    mailbox = get_quoted_string(t+1, &t);
    if(comment || mailbox) {
      res = imap_address_new(comment, mailbox);
      if(*t == ' ')
        res->next = imap_address_from_string(t+1, (gchar**)&t);
    }
  } else g_free(comment);

  if(n)
    *n = (gchar*)t;
  return res;
}

static gchar*
imap_address_to_string(const ImapAddress *addr)
{
  GString *res = g_string_sized_new(4);

  for(; addr; addr = addr->next) {
    if(addr->name) {
      append_quoted_string(res, addr->name);
    } else g_string_append_c(res, 'N');
    g_string_append_c(res, ' ');
    if(addr->addr_spec) {
      append_quoted_string(res, addr->addr_spec);
    } else g_string_append_c(res, 'N');
    g_string_append_c(res, ' ');
  }
  g_string_append(res, "N N");
  return g_string_free(res, FALSE);
}

/* imap_get_address: returns null if no beginning of address is found
   (eg., when end of list is found instead).
*/
static ImapAddress*
imap_get_address(NetClientSioBuf *sio)
{
  char *addr[4], *p;
  ImapAddress *res = NULL;
  int i, c;

  /* DEERFIELD's IMAP SERVER sends address lists wrong. 
   * but we do not enable the workaround by default. */
#define WORKAROUND_FOR_NON_COMPLIANT_DEERFIELD_IMAP_SERVER 1
#if WORKAROUND_FOR_NON_COMPLIANT_DEERFIELD_IMAP_SERVER
  while((c=sio_getc (sio))==' ')
    ;
#else
  c=sio_getc (sio);
#endif
  if(c != '(') {
    sio_ungetc(sio);
    return NULL;
  }
  
  for(i=0; i<4; i++) {
    addr[i] = imap_get_nstring(sio);
    if( (c=sio_getc(sio)) != ' ') {} /* error if i < 3 but do nothing */
  }

  if (addr[0] && (p = strchr(addr[0], '\r'))) {
    /* Server sent a folded string--unfold it */
    char *q;
    for (q = p; *q; q++) {
      if (*q == '\r') {
        while (p > addr[0] && (p[-1] == ' ' || p[-1] == '\t')) --p;
        do q++;
        while (*q == '\n' || *q == ' ' || *q == '\t');
        if (!*q) break;
        /* Replace FWS with a single space */
        *p++ = ' ';
      }
      *p++ = *q;
    }
    *p = '\0';
  }

  if(c == ')') {
    if(addr[2] == NULL) /* end group */
      res = imap_address_new(NULL, NULL);
    else if(addr[3] == NULL) { /* begin group */
      res = imap_address_new(addr[2], NULL);
      addr[2] = NULL;
    } else {
      gchar * addr_spec = g_strconcat(addr[2], "@", addr[3], NULL);
      res = imap_address_new(addr[0], addr_spec);
      addr[0] = NULL;
    }
  }
  for(i=0; i<4; i++)
    if(addr[i]) g_free(addr[i]);
  return res;
}

static ImapResponse
imap_get_addr_list (NetClientSioBuf *sio, ImapAddress ** list)
{
  int c;
  ImapAddress *res;
  ImapAddress **addr;

  if ((c=sio_getc (sio)) != '(') {
    if (imap_is_nil(sio, c)) return IMR_OK;
    else return IMR_PROTOCOL;
  }

  res = NULL;
  addr = &res;
  while ((*addr = imap_get_address (sio)) != NULL)
    addr = &(*addr)->next;
  if (sio_getc (sio) != ')') {
    imap_address_free (res);
    return IMR_PROTOCOL;
  }

  if (list)
    *list = res;
  else
    imap_address_free (res);

  return IMR_OK;
}

static ImapResponse
ir_envelope(NetClientSioBuf *sio, ImapEnvelope *env)
{
  int c;
  char *date, *str;

  c=sio_getc(sio);

#define GMAIL_BUG_20100725 1
#if GMAIL_BUG_20100725 == 1
  /* GMAIL returns sometimes NIL instead of the envelope. */
  if (c == 'N') {
      g_debug("GMail message/rfc822 bug detected.");
      env = NULL;
      if (sio_getc(sio) == 'I' &&
          sio_getc(sio) == 'L') return IMR_PARSE;
  }
#endif /* GMAIL_BUG_20100725 */
  if( c != '(') {
      g_debug("envelope's ( expected but got '%c'", c);
      return IMR_PROTOCOL;
  }

  date = imap_get_nstring(sio);
  if(date) {
    if (env != NULL) {
      GDateTime *header_date;

      header_date = g_mime_utils_header_decode_date(date);
      if (header_date != NULL) {
        env->date = (time_t) g_date_time_to_unix(header_date);
        g_date_time_unref(header_date);
      }
    }
    g_free(date);
  }
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  str = imap_get_nstring(sio);
  if(env) env->subject = str; else g_free(str);
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->from : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->sender : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->replyto : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->to : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->cc : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->bcc : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  str = imap_get_nstring(sio);
  if(env) env->in_reply_to = str; else g_free(str);
  if( (c=sio_getc(sio)) != ' ') { g_debug("c=%c",c); return IMR_PROTOCOL;}
  str = imap_get_nstring(sio);
  if(env) env->message_id = str; else g_free(str);
  if( (c=sio_getc(sio)) != ')') { g_debug("c=%d",c);return IMR_PROTOCOL;}
  return IMR_OK;
}

static ImapResponse
ir_msg_att_envelope(ImapMboxHandle *h, int c, unsigned seqno)
{
  ImapMessage *msg;
  ImapEnvelope *env;

  CREATE_IMSG_IF_NEEDED(h, seqno);
  msg = h->msg_cache[seqno-1];
  if(msg->envelope) env = NULL;
  else {
    msg->envelope = env = imap_envelope_new();
  }
  return ir_envelope(h->sio, env);
}

static ImapResponse
ir_msg_att_internaldate(ImapMboxHandle *h, int c, unsigned seqno)
{
  return IMR_OK;
}
static ImapResponse
ir_msg_att_rfc822(ImapMboxHandle *h, int c, unsigned seqno)
{
  gchar *str = imap_get_nstring(h->sio);
  if(str && h->body_cb)
    h->body_cb(seqno, IMAP_BODY_TYPE_RFC822, str, strlen(str), h->body_arg);
  g_free(str);
  return IMR_OK;
}

static ImapResponse
ir_msg_att_rfc822_header(ImapMboxHandle *h, int c, unsigned seqno)
{
  return IMR_OK;
}
static ImapResponse
ir_msg_att_rfc822_text(ImapMboxHandle *h, int c, unsigned seqno)
{
  return IMR_OK;
}
static ImapResponse
ir_msg_att_rfc822_size(ImapMboxHandle *h, int c, unsigned seqno)
{
  char buf[12];
  ImapMessage *msg;

  c = imap_get_atom(h->sio, buf, sizeof(buf));

  if(c!= -1) sio_ungetc(h->sio);

  CREATE_IMSG_IF_NEEDED(h, seqno);
  msg = h->msg_cache[seqno-1];
  
  msg->rfc822size = strtol(buf, NULL, 10);  
  return IMR_OK;
}

static ImapResponse
ir_media(NetClientSioBuf *sio, ImapMediaBasic *imb, ImapBody *body)
{
  gchar *type, *subtype;

  type    = imap_get_string(sio);
  if(!type) return IMR_PROTOCOL;

  if(sio_getc(sio) != ' ') { g_free(type); return IMR_PROTOCOL; }
  subtype = imap_get_string(sio);
  /* printf("media: %s/%s\n", type, subtype); */
  if     (g_ascii_strcasecmp(type, "APPLICATION") ==0) *imb = IMBMEDIA_APPLICATION;
  else if(g_ascii_strcasecmp(type, "AUDIO") ==0)       *imb = IMBMEDIA_AUDIO;
  else if(g_ascii_strcasecmp(type, "IMAGE") ==0)       *imb = IMBMEDIA_IMAGE;
  else if(g_ascii_strcasecmp(type, "MESSAGE") ==0) {
    if(g_ascii_strcasecmp(subtype, "RFC822") == 0)
      *imb = IMBMEDIA_MESSAGE_RFC822;
    else
      *imb = IMBMEDIA_MESSAGE_OTHER;
  }
  else if(g_ascii_strcasecmp(type, "TEXT") ==0)        *imb = IMBMEDIA_TEXT;
  else 
    *imb = IMBMEDIA_OTHER;

  if(body) {
    body->media_basic_name = type;
    body->media_basic = *imb;
    body->media_subtype = subtype;
  } else {
    g_free(type);
    g_free(subtype);
  }
  return IMR_OK;
}

static ImapResponse
ir_body_fld_param_hash(NetClientSioBuf *sio, GHashTable * params)
{
  int c;
  gchar *key, *val;
  if( (c=sio_getc(sio)) == '(') {
    do {
      key = imap_get_string(sio);
      if(sio_getc(sio) != ' ') { g_free(key); return IMR_PROTOCOL; }
      val = imap_get_string(sio);
      if(params) {
        for(c=0; key[c]; c++)
          key[c] = tolower(key[c]);
        g_hash_table_insert(params, key, val);
      } else {
        g_free(key); g_free(val);
      }
    } while( (c=sio_getc(sio)) != ')' && c != EOF);
  } else if(toupper(c) != 'N' || toupper(sio_getc(sio)) != 'I' ||
            toupper(sio_getc(sio)) != 'L') return IMR_PROTOCOL;
  return IMR_OK;
}

static ImapResponse
ir_body_fld_param(NetClientSioBuf *sio, ImapBody *body)
{
  return ir_body_fld_param_hash(sio, body ? body->params : NULL);
}

static ImapResponse
ir_body_fld_id(NetClientSioBuf *sio, ImapBody *body)
{
  gchar* id = imap_get_nstring(sio);

  if(body)
    imap_body_set_id(body, id);
  else g_free(id);

  return IMR_OK;
}

static ImapResponse
ir_body_fld_desc(NetClientSioBuf *sio, ImapBody *body)
{
  gchar* desc = imap_get_nstring(sio);
  if(body)
    imap_body_set_desc(body, desc);
  else g_free(desc);

  return IMR_OK;
}

static ImapResponse
ir_body_fld_enc(NetClientSioBuf *sio, ImapBody *body)
{
  gchar* str = imap_get_string(sio);
  ImapBodyEncoding enc;

  /* if(!str) return IMR_PROTOCOL; required - but forgive this error */
  if     (str == NULL) enc = IMBENC_OTHER;
  else if(g_ascii_strcasecmp(str, "7BIT")==0)             enc = IMBENC_7BIT;
  else if(g_ascii_strcasecmp(str, "8BIT")==0)             enc = IMBENC_8BIT;
  else if(g_ascii_strcasecmp(str, "BINARY")==0)           enc = IMBENC_BINARY;
  else if(g_ascii_strcasecmp(str, "BASE64")==0)           enc = IMBENC_BASE64;
  else if(g_ascii_strcasecmp(str, "QUOTED-PRINTABLE")==0) enc = IMBENC_QUOTED;
  else enc = IMBENC_OTHER;
  if(body)
    body->encoding = enc;
  g_free(str);
  return IMR_OK;
}

static ImapResponse
ir_body_fld_octets(NetClientSioBuf *sio, ImapBody *body)
{
  char buf[12];
  int c = imap_get_atom(sio, buf, sizeof(buf));

  if(c!= -1) sio_ungetc(sio);
  if(body) body->octets = strtol(buf, NULL, 10);  
  
  return IMR_OK;
}

static ImapResponse
ir_body_fields(NetClientSioBuf *sio, ImapBody *body)
{
  ImapResponse rc;
  int c;

  if( (rc=ir_body_fld_param (sio, body))!=IMR_OK) return rc;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if( (rc=ir_body_fld_id    (sio, body))!=IMR_OK) return rc;
  if((c=sio_getc(sio)) != ' ') { g_debug("err=%c", c); return IMR_PROTOCOL; }
  if( (rc=ir_body_fld_desc  (sio, body))!=IMR_OK) return rc;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if( (rc=ir_body_fld_enc   (sio, body))!=IMR_OK) return rc;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if( (rc=ir_body_fld_octets(sio, body))!=IMR_OK) return rc;

  return IMR_OK;
}

static ImapResponse
ir_body_fld_lines(NetClientSioBuf *sio, ImapBody* body)
{
  char buf[12];
  int c = imap_get_atom(sio, buf, sizeof(buf));

  if(c!= -1) sio_ungetc(sio);
  if(body) body->lines = strtol(buf, NULL, 10);  
  
  return IMR_OK;
}

/* body_fld_dsp = "(" string SP body_fld_param ")" / nil */
static ImapResponse
ir_body_fld_dsp (NetClientSioBuf *sio, ImapBody * body)
{
  ImapResponse rc;
  int c;
  char *str;

  if ((c = sio_getc (sio)) != '(')
    {
      /* nil */
      if (!imap_is_nil (sio, c))
	return IMR_PROTOCOL;
      return IMR_OK;
    }

  /* "(" string */
  str = imap_get_string (sio);
  if (body)
    {
      if (!g_ascii_strcasecmp (str, "inline"))
	body->content_dsp = IMBDISP_INLINE;
      else if (!g_ascii_strcasecmp (str, "attachment"))
	body->content_dsp = IMBDISP_ATTACHMENT;
      else
	{
	  body->content_dsp = IMBDISP_OTHER;
	  body->content_dsp_other = g_strdup (str);
	}
    }
  g_free (str);

  /* SP body_fld_param ")" */
  if (sio_getc (sio) != ' ')
    return IMR_PROTOCOL;
  if (body)
    {
      body->dsp_params =
	g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
			       g_free);
      rc = ir_body_fld_param_hash (sio, body->dsp_params);
    }
  else
    rc = ir_body_fld_param_hash (sio, NULL);

  if (rc != IMR_OK)
    return rc;

  if (sio_getc (sio) != ')')
    return IMR_PROTOCOL;

  return IMR_OK;
}

/* body-fld-lang = nstring / "(" string *(SP string) ")" */
static ImapResponse
ir_body_fld_lang (NetClientSioBuf *sio, ImapBody * body)
{
  int c;

  c = sio_getc (sio);

  if (c != '(')
    {
      /* nstring */
      char *str;

      sio_ungetc (sio);
      str = imap_get_nstring (sio);
      if (str && body)
	body->ext.mpart.lang = g_slist_append (NULL, str);
      else
	g_free (str);

      return IMR_OK;
    }

  /* string *(SP string) ")" */
  do
    {
      char *str = imap_get_string (sio);
      if (body)
	body->ext.mpart.lang = g_slist_append (body->ext.mpart.lang, str);
      else
	g_free (str);
      c = sio_getc (sio);
      if (c != ' ' && c != ')')
	return IMR_PROTOCOL;
    }
  while (c != ')');

  return IMR_OK;
}

/* body-extension = nstring / number /
 *                  "(" body-extension *(SP body-extension) ")"
 */
static ImapResponse
ir_body_extension (NetClientSioBuf *sio, ImapBody * body)
{
  int c;

  c = sio_getc (sio);
  if (c == '(')
    {
      /* "(" body-extension *(SP body-extension) ")" */
      do
	{
          ImapResponse rc;

	  rc = ir_body_extension (sio, body);
	  if (rc != IMR_OK)
	    return rc;
	  c = sio_getc (sio);
	  if (c != ' ' && c != ')')
	    return IMR_PROTOCOL;
	}
      while (c != ')');
    }
  else if (isdigit (c))
    {
      /* number */
      while (isdigit (sio_getc (sio)))
	;
      sio_ungetc (sio);
    }
  else
    /* nstring */
    g_free(imap_get_nstring(sio));

  return IMR_OK;
}

enum _ImapBodyExtensibility {
    IMB_NON_EXTENSIBLE,
    IMB_EXTENSIBLE,
    IMB_EXTENSIBLE_BUGGY_GMAIL
};
typedef enum _ImapBodyExtensibility ImapBodyExtensibility;

/* body-ext-mpart  = body-fld-param [SP body-fld-dsp [SP body-fld-lang
 *                   [SP body-fld-loc *(SP body-extension)]]]
 *                   ; MUST NOT be returned on non-extensible
 *                   ; "BODY" fetch
 */

static ImapResponse
ir_body_ext_mpart (NetClientSioBuf *sio, ImapBody * body,
		   ImapBodyExtensibility type)
{
  ImapResponse rc;
  char *str;

  if (type == IMB_NON_EXTENSIBLE)
    return IMR_PROTOCOL;

  /* body_fld_param */
  if (body)
    {
      /* body->ext.mpart.params =
	g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        g_free); */
      rc = ir_body_fld_param_hash (sio, body->params);
    }
  else
    rc = ir_body_fld_param_hash (sio, NULL);

  if (rc != IMR_OK)
    return rc;

  /* [SP */
  if (sio_getc (sio) != ' ')
    {
      sio_ungetc (sio);
      return IMR_OK;
    }

  /* body_fld_dsp */
  rc = ir_body_fld_dsp (sio, body);
  if (rc != IMR_OK)
    return rc;

  /* [SP */
  if (sio_getc (sio) != ' ')
    {
      sio_ungetc (sio);
      return IMR_OK;
    }

  /* body_fld_lang */
  rc = ir_body_fld_lang (sio, body);
  if (rc != IMR_OK)
    return rc;

  /* [SP */
  if (sio_getc (sio) != ' ')
    {
      sio_ungetc (sio);
      return IMR_OK;
    }

  /* body-fld-loc */
  str = imap_get_nstring (sio);
  if (body)
    body->content_uri = str;
  else
    g_free(str);

  /* (SP body-extension)]]] */
  while (sio_getc (sio) == ' ')
    {
      rc = ir_body_extension (sio, body);
      if (rc != IMR_OK)
	return rc;
    }
  sio_ungetc (sio);

  return IMR_OK;
}

/* body-ext-1part  = body-fld-md5 [SP body-fld-dsp [SP body-fld-lang
 *                   [SP body-fld-loc *(SP body-extension)]]]
 *                   ; MUST NOT be returned on non-extensible
 *                   ; "BODY" fetch
 */
static ImapResponse
ir_body_ext_1part (NetClientSioBuf *sio, ImapBody * body,
		   ImapBodyExtensibility type)
{
  ImapResponse rc;
  char *str;

  if (type == IMB_NON_EXTENSIBLE)
    return IMR_PROTOCOL;
#define GMAIL_BUG_20080601 1
#if GMAIL_BUG_20080601
  /* GMail sends number of lines on some parts like application/pgp-signature */
  { int c = sio_getc(sio);
    if(c == -1)
      return IMR_PROTOCOL;
    sio_ungetc(sio);
    if(isdigit(c)) { 
      char buf[20];
      g_debug("Incorrect GMail number-of-lines entry detected. "
	     "Working around.");
      c = imap_get_atom(sio, buf, sizeof(buf));
      if(c != ' ')
	return IMR_PROTOCOL;
    }
  }
#endif
  /* body_fld_md5 = nstring */
  if (type == IMB_EXTENSIBLE_BUGGY_GMAIL)
    str = NULL;
  else {
    str = imap_get_nstring (sio);
    if (body && str)
        body->ext.onepart.md5 = str;
    else
        g_free (str);

    /* [SP */
    if (sio_getc(sio) != ' ')
      {
        sio_ungetc (sio);
        return IMR_OK;
     }
  }
  /* body_fld_dsp */
  rc = ir_body_fld_dsp (sio, body);
  if (rc != IMR_OK)
    return rc;

  /* [SP */
  if (sio_getc (sio) != ' ')
    {
      sio_ungetc (sio);
      return IMR_OK;
    }

  /* body_fld_lang */
  rc = ir_body_fld_lang (sio, body);
  if (rc != IMR_OK)
    return rc;

  /* [SP */
  if (sio_getc (sio) != ' ')
    {
      sio_ungetc (sio);
      return IMR_OK;
    }

  /* body-fld-loc */
  str = imap_get_nstring (sio);
  if (body)
    body->content_uri = str;
  else
    g_free(str);

  /* (SP body-extension)]]] */
  while (sio_getc (sio) == ' ')
    {
      rc = ir_body_extension (sio, body);
      if (rc != IMR_OK)
	return rc;
    }
  sio_ungetc (sio);

  return IMR_OK;
}

/* body-type-mpart = 1*body SP media-subtype
 *                   [SP body-ext-mpart]
 */
static ImapResponse ir_body (NetClientSioBuf *sio, int c, ImapBody * body,
			     ImapBodyExtensibility type);
static ImapResponse
ir_body_type_mpart (NetClientSioBuf *sio, ImapBody * body,
		    ImapBodyExtensibility type)
{
  ImapResponse rc;
  gchar *str;
  int c;

  if (body)
    body->media_basic = IMBMEDIA_MULTIPART;

  /* 1*body */
  c = sio_getc (sio);
  do
    {
      ImapBody *b = body ? imap_body_new () : NULL;
      rc = ir_body (sio, c, b, type);
      if (body)
	imap_body_append_child (body, b);
      if (rc != IMR_OK)
	return rc;
    }
  while ((c = sio_getc (sio)) == '(');

  /* SP */
  if (c != ' ')
    return IMR_PROTOCOL;

  /* media-subtype = string */
  str = imap_get_string (sio);
  if (body)
    {
      g_assert (body->media_subtype == NULL);
      body->media_subtype = str;
    }
  else
    g_free (str);

  /* [SP */
  if (sio_getc (sio) != ' ')
    {
      sio_ungetc (sio);
      return IMR_OK;
    }

  /* body-ext-mpart] */
  rc = ir_body_ext_mpart (sio, body, type);
  if (rc != IMR_OK)
    return rc;

  return IMR_OK;
}

/* body-type-1part = (body-type-basic / body-type-msg / body-type-text)
 *                   [SP body-ext-1part]
 */
static ImapResponse
ir_body_type_1part (NetClientSioBuf *sio, ImapBody * body,
		    ImapBodyExtensibility type)
{
  ImapResponse rc;
  ImapMediaBasic media_type;
  ImapEnvelope *env;
  ImapBody *b;

  /* body-type-basic = media-basic SP body-fields 
   * body-type-msg   = media-message SP body-fields SP envelope
   *                   SP body SP body-fld-lines 
   * body-type-text  = media-text SP body-fields SP body-fld-lines */
  if ((rc = ir_media (sio, &media_type, body)) != IMR_OK)
    return rc;
  if (sio_getc (sio) != ' ')
    return IMR_PROTOCOL;
  if ((rc = ir_body_fields (sio, body)) != IMR_OK)
    return rc;

  switch (media_type)
    {
    case IMBMEDIA_APPLICATION:
    case IMBMEDIA_AUDIO:
    case IMBMEDIA_IMAGE:
    case IMBMEDIA_MESSAGE_OTHER:
    case IMBMEDIA_OTHER:
    case IMBMEDIA_MULTIPART:	/*FIXME: check this one */
      break;
    case IMBMEDIA_MESSAGE_RFC822:
      if (sio_getc (sio) != ' ')
	return IMR_PROTOCOL;
      env = body ? imap_envelope_new () : NULL;
      rc = ir_envelope (sio, env);
#if GMAIL_BUG_20100725
      if (rc == IMR_PARSE)
        {
	  if (env)
	    imap_envelope_free (env);
          break;
        }
#endif /* GMAIL_BUG_20100725 */
      if (rc != IMR_OK)
	{
	  if (env)
	    imap_envelope_free (env);
	  return rc;
	}
      if (sio_getc (sio) != ' ')
	{
          if (env)
            imap_envelope_free (env);
          return IMR_PROTOCOL;
	}
      if (body)
	{
	  b = imap_body_new ();
	  body->envelope = env;
	}
      else
        {
	  b = NULL;
	  if (env)
	    imap_envelope_free (env);
        }
      rc = ir_body (sio, sio_getc (sio), b, type);
      if (body)
	imap_body_append_child (body, b);
      if (rc != IMR_OK)
	return rc;
      if (sio_getc (sio) != ' ')
	return IMR_PROTOCOL;
      if ((rc = ir_body_fld_lines (sio, body)) != IMR_OK)
	return rc;
      break;
    case IMBMEDIA_TEXT:
      if (sio_getc (sio) != ' ')
	return IMR_PROTOCOL;
      if ((rc = ir_body_fld_lines (sio, body)) != IMR_OK)
	return rc;
    }

  /* [SP */
  if (sio_getc (sio) != ' ')
    {
      sio_ungetc (sio);
      return IMR_OK;
    }

  /* body-ext-1part] */
#if GMAIL_BUG_20100725
  rc = ir_body_ext_1part (sio, body,
                          (rc == IMR_PARSE ? IMB_EXTENSIBLE_BUGGY_GMAIL : type));
#else  /* GMAIL_BUG_20100725 */
  rc = ir_body_ext_1part (sio, body, type);
#endif /* GMAIL_BUG_20100725 */
  if (rc != IMR_OK)
    return rc;

  return IMR_OK;
}

/* body = "(" (body-type-1part / body-type-mpart) ")" */
static ImapResponse
ir_body (NetClientSioBuf *sio, int c, ImapBody * body,
	 ImapBodyExtensibility type)
{
  ImapResponse rc;

  if (c != '(')
    return IMR_PROTOCOL;

  c = sio_getc (sio);
  sio_ungetc (sio);
  if (c == '(')
    rc = ir_body_type_mpart (sio, body, type);
  else
    rc = ir_body_type_1part (sio, body, type);
  if (rc != IMR_OK)
    return rc;

  if (sio_getc (sio) != ')')
    return IMR_PROTOCOL;

  return IMR_OK;
}

/* read [section] and following string. FIXME: other kinds of body. */ 
static ImapResponse
ir_body_section(ImapMboxHandle *h, unsigned seqno,
		ImapFetchBodyType body_type,
		ImapFetchBodyInternalCb body_cb, void *arg)
{
  char buf[80];
  GString *bs;
  int i, c = imap_get_atom(h->sio, buf, sizeof(buf));

  for(i=0; buf[i] && (isdigit((int)buf[i]) || buf[i] == '.'); i++)
    ;
  if(i>0 && isalpha(buf[i])) /* we have \[[.0-9]something] */
    body_type = IMAP_BODY_TYPE_HEADER;

  if(c != ']') { g_debug("] expected"); return IMR_PROTOCOL; }
  if(sio_getc(h->sio) != ' ') { g_debug("space expected"); return IMR_PROTOCOL;}
  bs = imap_get_binary_string(h->sio);
  if(bs) {
    if (bs->str != NULL) {
      if (body_cb != NULL) {
        body_cb(seqno, body_type, bs->str, bs->len, arg);
      } else if (body_type == IMAP_BODY_TYPE_HEADER) {
        ImapMessage *msg;

        CREATE_IMSG_IF_NEEDED(h, seqno);
        msg = h->msg_cache[seqno-1];
        g_free(msg->fetched_header_fields);
        msg->fetched_header_fields = g_strdup(bs->str);
      }
    }
    g_string_free(bs, TRUE);
  }
  return IMR_OK;
}

static ImapResponse
ir_body_header_fields(ImapMboxHandle *h, unsigned seqno)
{
  ImapMessage *msg;
  char *tmp;
  int c;

  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;

  while ((tmp = imap_get_astring(h->sio, &c))) {
      /* nothing (yet?) */;
    g_free(tmp);
    if (c == ')')
      break;
  }
  if(c != ')') return IMR_PROTOCOL;
  if(sio_getc(h->sio) != ']') return IMR_PROTOCOL;
  if(sio_getc(h->sio) != ' ') return IMR_PROTOCOL;

  tmp = imap_get_nstring(h->sio);
  if(h->body_cb) {
    if(tmp) h->body_cb(seqno, IMAP_BODY_TYPE_HEADER,
		       tmp, strlen(tmp), h->body_arg);
    g_free(tmp);
  } else {
    CREATE_IMSG_IF_NEEDED(h, seqno);
    msg = h->msg_cache[seqno-1];
    g_free(msg->fetched_header_fields);
    msg->fetched_header_fields = tmp;
  }
  return IMR_OK;
}

static ImapResponse
ir_msg_att_body(ImapMboxHandle *h, int c, unsigned seqno)
{
  ImapMessage *msg;
  ImapResponse rc;
  char buf[19];	/* Just large enough to hold "HEADER.FIELDS.NOT". */

  switch(c) {
  case '[': 
    c = sio_getc (h->sio);
    sio_ungetc (h->sio);
    if(isdigit (c)) {
      rc = ir_body_section(h, seqno, IMAP_BODY_TYPE_BODY,
			   h->body_cb, h->body_arg);
      break;
    }
    c = imap_get_atom(h->sio, buf, sizeof buf);
    if (c == ']' &&
        (g_ascii_strcasecmp(buf, "HEADER") == 0 ||
         g_ascii_strcasecmp(buf, "TEXT") == 0)) {
      ImapFetchBodyType body_type = 
	(g_ascii_strcasecmp(buf, "TEXT") == 0)
	? IMAP_BODY_TYPE_TEXT : IMAP_BODY_TYPE_HEADER;
      sio_ungetc (h->sio); /* put the ']' back */
      rc = ir_body_section(h, seqno, body_type, h->body_cb, h->body_arg);
    } else {
      if (c == ' ' && 
          (g_ascii_strcasecmp(buf, "HEADER.FIELDS") == 0 ||
           g_ascii_strcasecmp(buf, "HEADER.FIELDS.NOT") == 0))
        rc = ir_body_header_fields(h, seqno);
      else
        rc = IMR_PROTOCOL;
    }
    break;
  case ' ':
    CREATE_IMSG_IF_NEEDED(h, seqno);
    msg = h->msg_cache[seqno-1];
    rc = ir_body(h->sio, sio_getc(h->sio),
                 msg->body ? NULL : (msg->body = imap_body_new()), 
		 IMB_NON_EXTENSIBLE);
    break;
  default: rc = IMR_PROTOCOL; break;
  }
  return rc;
}

static ImapResponse
ir_msg_att_bodystructure(ImapMboxHandle *h, int c, unsigned seqno)
{
  ImapMessage *msg;
  ImapResponse rc;

  switch(c) {
  case ' ':
    CREATE_IMSG_IF_NEEDED(h, seqno);
    msg = h->msg_cache[seqno-1];
    rc = ir_body(h->sio, sio_getc(h->sio),
                 msg->body ? NULL : (msg->body = imap_body_new()), 
		 IMB_EXTENSIBLE);
    break;
  default: rc = IMR_PROTOCOL; break;
  }
  return rc;
}

static ImapResponse
ir_msg_att_uid(ImapMboxHandle *h, int c, unsigned seqno)
{
  char buf[12];
  c = imap_get_atom(h->sio, buf, sizeof(buf));

  if(c!= -1) sio_ungetc(h->sio);
  CREATE_IMSG_IF_NEEDED(h, seqno);
  h->msg_cache[seqno-1]->uid = strtol(buf, NULL, 10);
  return IMR_OK;
}

static ImapResponse
ir_fetch_seq(ImapMboxHandle *h, unsigned seqno)
{
  static const struct {
    const gchar* name;
    ImapResponse (*handler)(ImapMboxHandle *h, int c, unsigned seqno);
  } msg_att[] = {
    { "FLAGS",         ir_msg_att_flags },
    { "ENVELOPE",      ir_msg_att_envelope },
    { "INTERNALDATE",  ir_msg_att_internaldate }, 
    { "BODY[]",        ir_msg_att_rfc822 },
    { "RFC822.HEADER", ir_msg_att_rfc822_header }, 
    { "RFC822.TEXT",   ir_msg_att_rfc822_text }, 
    { "RFC822.SIZE",   ir_msg_att_rfc822_size }, 
    { "BINARY",        ir_msg_att_body }, 
    { "BODY",          ir_msg_att_body }, 
    { "BODYSTRUCTURE", ir_msg_att_bodystructure }, 
    { "UID",           ir_msg_att_uid }
  };
  char atom[LONG_STRING]; /* make sure LONG_STRING is longer than all */
                          /* strings above */
  unsigned i;
  int c = 0;
  ImapResponse rc;

  if (h->state != IMHS_SELECTED) {
    /* bad state for a response to FETCH, see RFC 3501, Sect. 6.4. and 7.4.2. */
    g_info("received FETCH response in bad state %d", h->state);
    return IMR_PROTOCOL;
  }

  if(seqno<1 || seqno > h->exists) return IMR_PROTOCOL;
  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;
  do {
    for(i=0; i<sizeof(atom)-1 && (c = sio_getc(h->sio)) != -1; i++) {
      c = toupper(c);
      if( !( (c >='A' && c<='Z') || (c >='0' && c<='9') || c == '.') ) break;
      atom[i] = c;
    }
    atom[i] = '\0';
    /* special case to work around #94: check for 'BODY[]' */
    if ((strcmp(atom, "BODY") == 0) && (c == '[')) {
      if (sio_getc(h->sio) == ']') {
        atom[i++] = '[';
        atom[i++] = ']';
        atom[i] = '\0';
        c = sio_getc(h->sio);
      } else {
        sio_ungetc(h->sio);
      }
    }
    for(i=0; i<G_N_ELEMENTS(msg_att); i++) {
      if(g_ascii_strcasecmp(atom, msg_att[i].name) == 0) {
        if( (rc=msg_att[i].handler(h, c, seqno)) != IMR_OK)
          return rc;
        break;
      }
    }
    c=sio_getc(h->sio);
  } while( c!= EOF && c == ' ');
  if(c!=')') return IMR_PROTOCOL;
  return ir_check_crlf(h, sio_getc(h->sio));
}

static ImapResponse
ir_fetch(ImapMboxHandle *h)
{
  char buf[12];
  unsigned seqno;
  int i;

  i = imap_get_atom(h->sio, buf, sizeof(buf));
  seqno = strtol(buf, NULL, 10);
  if(seqno == 0) return IMR_PROTOCOL;
  if(i != ' ') return IMR_PROTOCOL;
  return ir_fetch_seq(h, seqno);
}

/* THREAD response handling code.
   Example:    S: * THREAD (2)(3 6 (4 23)(44 7 96))
   
   The first thread consists only of message 2.  The second thread
   consists of the messages 3 (parent) and 6 (child), after which it
   splits into two subthreads; the first of which contains messages 4
   (child of 6, sibling of 44) and 23 (child of 4), and the second of
   which contains messages 44 (child of 6, sibling of 4), 7 (child of
   44), and 96 (child of 7).  Since some later messages are parents
   of earlier messages, the messages were probably moved from some
   other mailbox at different times.
   
   -- 2
   
   -- 3
     \-- 6
         |-- 4
         |   \-- 23
         |
         \-- 44
             \-- 7
                \-- 96
*/
static ImapResponse
ir_thread_sub(ImapMboxHandle *h, GNode *parent, int last)
{
  char buf[12];
  unsigned seqno;
  int c;
  GNode *item;
  ImapResponse rc = IMR_OK;

  c = imap_get_atom(h->sio, buf, sizeof(buf));

  seqno = strtol(buf, NULL, 10);
  if(seqno == 0 && c == '(') {
      while (c == '(') {
	  rc = ir_thread_sub(h, parent, c);
	  if (rc!=IMR_OK) {
	      return rc;
	  }
	  c=sio_getc(h->sio);
          if(c<0) return IMR_SEVERED;
      }
      return rc;
  }
  if(seqno == 0) return IMR_PROTOCOL;
  item = g_node_append_data(parent, GUINT_TO_POINTER(seqno));
  if (c == ' ') {
      rc = ir_thread_sub(h, item, c);
  }

  return rc;
}

static ImapResponse
ir_thread(ImapMboxHandle *h)
{
  GNode *root;
  int c;
  ImapResponse rc = IMR_OK;
  
  c=sio_getc(h->sio);
  if(h->thread_root)
    g_node_destroy(h->thread_root);
  h->thread_root = NULL;
  root = g_node_new(NULL);
  while (c == '(') {
    rc=ir_thread_sub(h, root, c);
    if (rc!=IMR_OK)
      break;
    c=sio_getc(h->sio);
  }
  if (rc == IMR_OK)
    rc = ir_check_crlf(h, c);

  if (rc != IMR_OK)
      g_node_destroy(root);
  else
      h->thread_root = root;

  return rc;
}


/* response dispatch code */
static const struct {
  const gchar *response;
  int keyword_len;
  ImapResponse (*handler)(ImapMboxHandle *h);
} ResponseHandlers[] = {
  { "OK",         2, ir_ok },
  { "NO",         2, ir_no },
  { "BAD",        3, ir_bad },
  { "PREAUTH",    7, ir_preauth },
  { "BYE",        3, ir_bye },
  { "CAPABILITY",10, ir_capability },
  { "LIST",       4, ir_list },
  { "LSUB",       4, ir_lsub },
  { "STATUS",     6, ir_status },
  { "ESEARCH",    7, ir_esearch },
  { "SEARCH",     6, ir_search },
  { "SORT",       4, ir_sort   },
  { "THREAD",     6, ir_thread },
  { "FLAGS",      5, ir_flags  },
  { "FETCH",      5, ir_fetch  },
  { "MYRIGHTS",   8, ir_myrights },
  { "ACL",        3, ir_getacl },
  { "QUOTAROOT",  9, ir_quotaroot },
  { "QUOTA",      5, ir_quota }
};
static const struct {
  const gchar *response;
  int keyword_len;
  ImapResponse (*handler)(ImapMboxHandle *h, unsigned seqno);
} NumHandlers[] = {
  { "EXISTS",     6, ir_exists },
  { "RECENT",     6, ir_recent },
  { "EXPUNGE",    7, ir_expunge },
  { "FETCH",      5, ir_fetch_seq }
};
  
/* the public interface: */
static ImapResponse
ir_handle_response(ImapMboxHandle *h)
{
  int c;
  char atom[LONG_STRING];
  ImapResponse rc = IMR_BAD; /* unknown response is really an error */

  c = imap_get_atom(h->sio, atom, sizeof(atom));
  if( isdigit(atom[0]) ) {
    unsigned i, seqno;

    if (c != ' ')
      return IMR_PROTOCOL;
    seqno = strtol(atom, NULL, 10);
    c = imap_get_atom(h->sio, atom, sizeof(atom));
    if (c == 0x0d)
      sio_ungetc(h->sio);
    for(i=0; i<G_N_ELEMENTS(NumHandlers); i++) {
      if(g_ascii_strncasecmp(atom, NumHandlers[i].response, 
                             NumHandlers[i].keyword_len) == 0) {
        rc = NumHandlers[i].handler(h, seqno);
        break;
      }
    }
  } else {
    unsigned i;

    if (c == 0x0d)
      sio_ungetc(h->sio);
    for(i=0; i<G_N_ELEMENTS(ResponseHandlers); i++) {
      if(g_ascii_strncasecmp(atom, ResponseHandlers[i].response, 
                             ResponseHandlers[i].keyword_len) == 0) {
        rc = ResponseHandlers[i].handler(h);
        break;
      }
    }
  }
  imap_handle_process_tasks(h);
  return rc;
}

GNode*
imap_mbox_handle_get_thread_root(ImapMboxHandle* handle)
{
  g_return_val_if_fail(handle, NULL);
  return handle->thread_root;
}

gchar*
imap_coalesce_seq_range(int lo, int hi, ImapCoalesceFunc incl, void *data)
{
  GString * res = g_string_sized_new(16);
  enum { BEGIN, LASTOUT, LASTIN, RANGE } mode = BEGIN;
  int seq;
  unsigned prev =0, num = 0;

  for(seq=lo; seq<=hi+1; seq++) {
    if(seq<=hi && (num=incl(seq, data)) != 0) {
      switch(mode) {
      case BEGIN: 
        g_string_append_printf(res, "%u", num);
        mode = LASTIN; break;
      case RANGE:
        if(num!=prev+1) {
          g_string_append_printf(res, ":%u,%u", prev, num);
          mode = LASTIN;
        }
        break;
      case LASTIN: 
        if(num==prev+1) {
          mode = RANGE;
          break;
        } /* else fall through */
      case LASTOUT: 
        g_string_append_printf(res, ",%u", num);
        mode = LASTIN; break;
      }
    } else {
      switch(mode) {
      case BEGIN:
      case LASTOUT: break;
      case LASTIN: mode = LASTOUT; break;
      case RANGE: 
        g_string_append_printf(res, ":%u", prev);
        mode = LASTOUT;
        break;
      }
    }
    prev = num;
  }
  return g_string_free(res, mode == BEGIN);
}

unsigned
imap_coalesce_func_simple(int i, unsigned msgno[])
{
  return msgno[i];
}

gchar*
imap_coalesce_set(int cnt, unsigned *seqnos)
{
 return imap_coalesce_seq_range(0, cnt-1,
				(ImapCoalesceFunc)imap_coalesce_func_simple,
				seqnos);
}


/* =================================================================== */
/*               MboxView routines                                     */
/* =================================================================== */
#ifdef DEEP_MBOX_VIEW_IMPLEMENTATION_OUT_OF_BALSA
#define MBOX_VIEW_IS_ACTIVE(mv) ((mv)->arr != NULL)
#else
#define MBOX_VIEW_IS_ACTIVE(mv) 0
#endif
void
mbox_view_init(MboxView *mv)
{
  mv->arr = NULL;
  mv->allocated = mv->entries = 0;
  mv->filter_str = NULL;
}

/* mbox_view_resize:
   When new messages appear in the mailbox, we need to resize the view
   as well. We assume that the new messages fulfill the filtering
   condition which does not have to be true. In principle, we should
   apply the filter for them as well. We do it next time.
*/
void
mbox_view_resize(MboxView *mv, unsigned old_exists, unsigned new_exists)
{

  if( !MBOX_VIEW_IS_ACTIVE(mv) ) return;
  if(old_exists>new_exists) {
    unsigned src, dest;
    /* entries (new_exists, old_exists] removed */
    /* this probably will never get called without earlier EXPUNGE */
    for(dest=src=0; src<mv->entries; src++) {
      if(mv->arr[src]<=new_exists)
        mv->arr[dest++] = mv->arr[src];
    }
    mv->entries = dest;
  } else {
    /* entries (old_exists, new_exists] added */
    /* FIXME: we should apply the filter below, instead of assuming
     * that all the messages match the filter.
     * but, since we may be in the response handler.
     * Queue new request? Idea of tasklets? */
    int delta, i;
    if(new_exists>mv->allocated) {
      mv->allocated = mv->allocated ? mv->allocated*2 : 16;
      mv->arr = g_realloc(mv->arr, mv->allocated*sizeof(unsigned));
    }
    delta = new_exists - old_exists;
    for(i=0; i<delta; i++)
      mv->arr[mv->entries+i] = old_exists+1+i;
  mv->entries += delta;
  }
}

void
mbox_view_expunge(MboxView *mv, unsigned seqno)
{
  unsigned i;

  if( !MBOX_VIEW_IS_ACTIVE(mv) ) return;
  for(i=0; i<mv->entries && mv->arr[i] != seqno; i++)
    ;
  for(; i<mv->entries-1; i++)
    mv->arr[i] = mv->arr[i+1];
  mv->entries = i;
}

void
mbox_view_dispose(MboxView *mv)
{
  g_free(mv->arr);
  mv->arr = NULL;
  mv->allocated = mv->entries = 0;
  g_free(mv->filter_str); mv->filter_str = NULL;
}

void
mbox_view_append_no(MboxView *mv, unsigned seqno)
{
  if(mv->allocated == mv->entries) {
    mv->allocated = mv->allocated ? mv->allocated*2 : 16;
    mv->arr = g_realloc(mv->arr, mv->allocated*sizeof(unsigned));
  }
  mv->arr[mv->entries++] = seqno;
}

gboolean
mbox_view_is_active(MboxView *mv)
{
  return MBOX_VIEW_IS_ACTIVE(mv);
}

unsigned
mbox_view_cnt(MboxView *mv)
{
  return mv->entries;
}

unsigned
mbox_view_get_msg_no(MboxView *mv, unsigned msgno)
{
  return mv->arr[msgno-1];
}

unsigned
mbox_view_get_rev_no(MboxView *mv, unsigned seqno)
{
  unsigned lo = 0, hi = mv->entries-1;
  if(mv->entries == 0) return 0;
  if(seqno<mv->arr[lo] || seqno>mv->arr[hi]) return 0;
  while(lo<=hi) {
    if(seqno == mv->arr[lo])
      return lo+1;
    else if(seqno == mv->arr[hi])
      return hi+1;
    else {
      unsigned mid = (lo+hi)/2;
      if(seqno<mv->arr[mid])
        hi=mid-1;
      else if(seqno>mv->arr[mid])
        lo=mid+1;
      else return mid+1;
    }
  }
  return 0;
}

const char*
mbox_view_get_str(MboxView *mv)
{
  return mv->filter_str ? mv->filter_str : "";
}

gboolean
imap_server_probe(const gchar *host, guint timeout_secs, NetClientProbeResult *result, GCallback cert_cb, GError **error)
{
	guint16 probe_ports[] = {993U, 143U, 0U};		/* imaps, imap */
	gchar *host_only;
	gboolean retval = FALSE;
	gint check_id;

	/* paranoia check */
	g_return_val_if_fail((host != NULL) && (result != NULL), FALSE);

	host_only = net_client_host_only(host);

	if (!net_client_host_reachable(host_only, error)) {
		g_free(host_only);
		return FALSE;
	}

	for (check_id = 0; !retval && (probe_ports[check_id] > 0U); check_id++) {
		ImapMboxHandle *handle;

		g_debug("%s: probing %s:%u…", __func__, host_only, probe_ports[check_id]);
		handle = imap_mbox_handle_new();
		handle->sio = net_client_siobuf_new(host_only, probe_ports[check_id]);
		net_client_set_timeout(NET_CLIENT(handle->sio), timeout_secs);
		if (net_client_connect(NET_CLIENT(handle->sio), NULL)) {
			gboolean this_success;
			ImapResponse resp;
			gboolean can_starttls = FALSE;

			if (cert_cb != NULL) {
				g_signal_connect(handle->sio, "cert-check", cert_cb, handle->sio);
			}
			if (check_id == 0) {	/* imaps */
				this_success = net_client_start_tls(NET_CLIENT(handle->sio), NULL);
			} else {
				this_success = TRUE;
			}

			/* get the server greeting and initialise the capabilities */
			if (this_success) {
				handle->state = IMHS_CONNECTED;
				resp = imap_cmd_step(handle, 0);
				if (resp != IMR_UNTAGGED) {
					imap_handle_disconnect(handle);
					this_success = FALSE;
				} else {
					/* fetch capabilities */
					can_starttls = imap_mbox_handle_can_do(handle, IMCAP_STARTTLS) != 0;
				}
			}

			/* try to perform STARTTLS if supported */
			if (this_success && can_starttls) {
				can_starttls = FALSE;
				resp = imap_cmd_exec(handle, "StartTLS");
				if (resp == IMR_OK) {
					if (net_client_start_tls(NET_CLIENT(handle->sio), NULL)) {
						handle->has_capabilities = 0;
						can_starttls = TRUE;
					}
				}
			}

			/* evaluate on success */
			if (this_success) {
				result->port = probe_ports[check_id];

				if (check_id == 0) {
					result->crypt_mode = NET_CLIENT_CRYPT_ENCRYPTED;
				} else if (can_starttls) {
					result->crypt_mode = NET_CLIENT_CRYPT_STARTTLS;
				} else {
					result->crypt_mode = NET_CLIENT_CRYPT_NONE;
				}

				result->auth_mode = 0U;
				if (imap_mbox_handle_can_do(handle, IMCAP_AANONYMOUS) != 0) {
					result->auth_mode |= NET_CLIENT_AUTH_NONE_ANON;
				}
				if ((imap_mbox_handle_can_do(handle, IMCAP_ACRAM_MD5) != 0) ||
					(imap_mbox_handle_can_do(handle, IMCAP_APLAIN) != 0)) {
					result->auth_mode |= NET_CLIENT_AUTH_USER_PASS;
				}
				if (imap_mbox_handle_can_do(handle, IMCAP_AGSSAPI) != 0) {
					result->auth_mode |= NET_CLIENT_AUTH_KERBEROS;
				}
				retval = TRUE;
			}
		}

		g_object_unref(handle);
	}

	if (!retval) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, NET_CLIENT_PROBE_FAILED,
			_("the server %s does not offer the IMAP service at port 993 or 143"), host_only);
	}

	g_free(host_only);

	return retval;
}
