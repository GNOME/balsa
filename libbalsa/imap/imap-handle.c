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
#include "config.h"

#define _POSIX_C_SOURCE 199506L
#define _XOPEN_SOURCE 500

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <glib.h>
#include <glib-object.h>
#include <ctype.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <gmime/gmime-utils.h>

#if defined(USE_TLS)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "libimap-marshal.h"
#include "imap-auth.h"
#include "imap-handle.h"
#include "imap-commands.h"
#include "imap_private.h"
#include "siobuf.h"
#include "util.h"

#define ASYNC_DEBUG 0

#define LONG_STRING 512
#define ELEMENTS(x) (sizeof (x) / sizeof(x[0]))

#define IDLE_TIMEOUT 30

#define LIT_TYPE_HANDLE \
    (imap_mbox_handle_get_type())
#define IMAP_MBOX_HANDLE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST(obj, LIT_TYPE_HANDLE, ImapMboxHandle))
#define IMAP_MBOX_HANDLE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST(klass, LIT_TYPE_HANDLE, \
                             ImapMboxHandleClass))
#define LIT_IS_HANDLE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE(obj, LIT_TYPE_HANDLE))
#define LIT_IS_HANDLE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE(klass, LIT_TYPE_HANDLE))


struct _ImapMboxHandleClass {
  GObjectClass parent_class;
  /* Signal */
  void (*fetch_response)(ImapMboxHandle* handle);
  void (*list_response)(ImapMboxHandle* handle, int delim,
                        ImapMboxFlags flags, const gchar* mbox);
  void (*lsub_response)(ImapMboxHandle* handle, int delim,
                        ImapMboxFlags flags, const gchar* mbox);
  void (*expunge_notify)(ImapMboxHandle* handle, int seqno);
  void (*exists_notify)(ImapMboxHandle* handle);
};

enum _ImapHandleSignal {
  FETCH_RESPONSE,
  LIST_RESPONSE,
  LSUB_RESPONSE,
  EXPUNGE_NOTIFY,
  EXISTS_NOTIFY,
  LAST_SIGNAL
};
typedef enum _ImapHandleSignal ImapHandleSignal;

static GObjectClass *parent_class = NULL;
static guint imap_mbox_handle_signals[LAST_SIGNAL] = { 0 };

static void imap_mbox_handle_init(ImapMboxHandle *handle);
static void imap_mbox_handle_class_init(ImapMboxHandleClass * klass);
static void imap_mbox_handle_finalize(GObject* handle);

static ImapResult imap_mbox_connect(ImapMboxHandle* handle);

static ImapResponse ir_handle_response(ImapMboxHandle *h);

static ImapAddress* imap_address_from_string(const gchar *string);
static gchar*       imap_address_to_string(const ImapAddress *addr);

static GType
imap_mbox_handle_get_type()
{
  static GType imap_mbox_handle_type = 0;

  if(!imap_mbox_handle_type) {
    static const GTypeInfo imap_mbox_handle_info = {
      sizeof(ImapMboxHandleClass),
      NULL,               /* base_init */
      NULL,               /* base_finalize */
      (GClassInitFunc) imap_mbox_handle_class_init,
      NULL,               /* class_finalize */
      NULL,               /* class_data */
      sizeof(ImapMboxHandle),
      0,                  /* n_preallocs */
      (GInstanceInitFunc) imap_mbox_handle_init
    };
    imap_mbox_handle_type =
      g_type_register_static(G_TYPE_OBJECT, "ImapMboxHandle",
                             &imap_mbox_handle_info, 0);
  }
  return imap_mbox_handle_type;
}

static void
imap_mbox_handle_init(ImapMboxHandle *handle)
{
  handle->host   = NULL;
  handle->mbox   = NULL;
  handle->timeout = -1;
  handle->state  = IMHS_DISCONNECTED;
  handle->has_capabilities = FALSE;
  handle->exists = 0;
  handle->recent = 0;
  handle->last_msg = NULL;
  handle->msg_cache = NULL;
  handle->flag_cache=  g_array_new(FALSE, TRUE, sizeof(ImapFlagCache));
  handle->doing_logout = FALSE;
#ifdef USE_TLS
  handle->using_tls = 0;
#endif
  handle->tls_mode = IMAP_TLS_ENABLED;
  handle->cmd_info = NULL;
  handle->status_resps = g_hash_table_new_full(g_str_hash, g_str_equal,
                                               NULL, NULL);

  handle->info_cb  = NULL;
  handle->info_arg = NULL;
  handle->enable_anonymous = 0;
  handle->enable_binary    = 0;
  mbox_view_init(&handle->mbox_view);
#if defined(BALSA_USE_THREADS)
  pthread_mutex_init(&handle->mutex, NULL);
#endif
}

static void
imap_mbox_handle_class_init(ImapMboxHandleClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  
  parent_class = g_type_class_peek_parent(klass);
  imap_mbox_handle_signals[FETCH_RESPONSE] = 
    g_signal_new("fetch-response",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 G_STRUCT_OFFSET(ImapMboxHandleClass, fetch_response),
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  imap_mbox_handle_signals[LIST_RESPONSE] = 
    g_signal_new("list-response",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 G_STRUCT_OFFSET(ImapMboxHandleClass, list_response),
                 NULL, NULL,
                 libimap_VOID__INT_INT_POINTER, G_TYPE_NONE, 3,
                 G_TYPE_INT, G_TYPE_INT, G_TYPE_POINTER);

  imap_mbox_handle_signals[LSUB_RESPONSE] = 
    g_signal_new("lsub-response",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 G_STRUCT_OFFSET(ImapMboxHandleClass, lsub_response),
                 NULL, NULL,
                 libimap_VOID__INT_INT_POINTER, G_TYPE_NONE, 3,
                 G_TYPE_INT, G_TYPE_INT, G_TYPE_POINTER);

  imap_mbox_handle_signals[EXPUNGE_NOTIFY] = 
    g_signal_new("expunge-notify",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 G_STRUCT_OFFSET(ImapMboxHandleClass, expunge_notify),
                 NULL, NULL,
                 g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1,
		 G_TYPE_INT);

  imap_mbox_handle_signals[EXISTS_NOTIFY] = 
    g_signal_new("exists-notify",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 G_STRUCT_OFFSET(ImapMboxHandleClass, exists_notify),
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  object_class->finalize = imap_mbox_handle_finalize;
}

ImapMboxHandle*
imap_mbox_handle_new(void)
{
  ImapMboxHandle *handle = g_object_new(LIT_TYPE_HANDLE, NULL);
  return handle;
}

void
imap_handle_set_option(ImapMboxHandle *h, ImapOption opt, gboolean state)
{
  switch(opt) {
  case IMAP_OPT_ANONYMOUS: h->enable_anonymous = !!state; break;
  case IMAP_OPT_BINARY:    h->enable_binary    = !!state; break;
  default: g_warning("imap_set_option: invalid option\n");
  }
}

void
imap_handle_set_infocb(ImapMboxHandle* h, ImapInfoCb cb, void *arg)
{
  h->info_cb  = cb;
  h->info_arg = arg;
}

void
imap_handle_set_usercb(ImapMboxHandle* h, ImapUserCb cb, void *arg)
{
  h->user_cb  = cb;
  h->user_arg = arg;
}

void
imap_handle_set_monitorcb(ImapMboxHandle* h, ImapMonitorCb cb, void*arg)
{
  h->monitor_cb  = cb;
  h->monitor_arg = arg;
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

void
imap_handle_set_timeout(ImapMboxHandle *h, int milliseconds)
{
  h->timeout = milliseconds;
  if(h->sio)
    sio_set_timeout(h->sio, milliseconds);
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
async_process(GIOChannel *source, GIOCondition condition, gpointer data)
{
  ImapMboxHandle *h = (ImapMboxHandle*)data;
  ImapResponse rc = IMR_UNTAGGED;
  int retval;
  unsigned async_cmd;
  g_return_val_if_fail(h, FALSE);

  if(ASYNC_DEBUG) printf("async_process() ENTER\n");
  if(h->state == IMHS_DISCONNECTED) {
    if(ASYNC_DEBUG) printf("async_process() on disconnected\n");
    return FALSE;
  }
  async_cmd = cmdi_get_pending(h->cmd_info);
  if(ASYNC_DEBUG) printf("async_process() enter loop\n");
  while( (retval = sio_poll(h->sio, TRUE, FALSE, TRUE)) != -1 &&
         (retval & SIO_READ) != 0) {
    if ( (rc=imap_cmd_step(h, async_cmd)) == IMR_UNKNOWN ||
         rc == IMR_SEVERED || rc == IMR_BYE || rc == IMR_PROTOCOL ||
         rc  == IMR_BAD) {
      printf("async_process() got unexpected response %i!\n"
             "Last message was: \"%s\" - shutting down connection.\n",
             rc, h->last_msg);
      imap_handle_disconnect(h);
      return FALSE;
    }
    async_cmd = cmdi_get_pending(h->cmd_info);
    if(ASYNC_DEBUG)
      printf("async_process() loop iteration finished, next async_cmd=%x\n",
           async_cmd);
  }
  if(ASYNC_DEBUG) printf("async_process() loop left\n");
  if(!h->idle_issued && async_cmd == 0) {
    if(ASYNC_DEBUG) printf("Last async command completed.\n");
    if(h->async_watch_id) {
      g_source_remove(h->async_watch_id);
      h->async_watch_id = 0;
    }
    imap_handle_idle_enable(h, IDLE_TIMEOUT);
  }
  if(ASYNC_DEBUG)
    printf("async_process() sio: %d rc: %d returns %d (%d cmds in queue)\n",
           retval, rc, !h->idle_issued && async_cmd == 0,
           g_list_length(h->cmd_info));
  return h->idle_issued || async_cmd != 0;
}

static gboolean
idle_start(gpointer data)
{
  ImapMboxHandle *h = (ImapMboxHandle*)data;
  ImapResponse rc;
  int c;
  ImapCmdTag tag;
  unsigned asyncno;

  /* The test below can probably be weaker since it is ok for the
     channel to get disconnected before IDLE gets activated */
  IMAP_REQUIRED_STATE3(h, IMHS_CONNECTED, IMHS_AUTHENTICATED,
                       IMHS_SELECTED, FALSE);
  asyncno = imap_make_tag(tag); sio_write(h->sio, tag, strlen(tag));
  sio_write(h->sio, " IDLE\r\n", 7); sio_flush(h->sio);
  cmdi_add_handler(&h->cmd_info, asyncno, cmdi_empty, NULL);
  do { /* FIXME: we could at this stage just as well watch for
          response instead of polling. */
    rc = imap_cmd_step (h, asyncno);
  } while (rc == IMR_UNTAGGED);
  if(rc != IMR_RESPOND) {
    g_message("idle_start() expected IMR_RESPOND but got %d\n", rc);
    return FALSE;
  }
  while( (c=sio_getc(h->sio)) != -1 && c != '\n');
  if(!h->iochannel) {
    h->iochannel = g_io_channel_unix_new(h->sd);
    g_io_channel_set_encoding(h->iochannel, NULL, NULL);
  }
  h->async_watch_id = g_io_add_watch(h->iochannel, G_IO_IN, async_process, h);
  h->idle_enable_id = 0;
  h->idle_issued = 1;
  return FALSE;
}

ImapResponse
imap_cmd_issue(ImapMboxHandle* h, const char* cmd)
{
  unsigned async_cmd;
  g_return_val_if_fail(h, IMR_BAD);
  if (h->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;

  /* create sequence for command */
  imap_handle_idle_disable(h);
  if (imap_cmd_start(h, cmd, &async_cmd)<0)
    return IMR_SEVERED;  /* irrecoverable connection error. */

  sio_flush(h->sio);
  if(ASYNC_DEBUG) printf("command '%s' issued.\n", cmd);
  cmdi_add_handler(&h->cmd_info, async_cmd, cmdi_empty, NULL);
  if(!h->iochannel) {
    h->iochannel = g_io_channel_unix_new(h->sd);
    g_io_channel_set_encoding(h->iochannel, NULL, NULL);
  }
  h->async_watch_id = g_io_add_watch(h->iochannel, G_IO_IN, async_process, h);
  return IMR_OK /* async_cmd */;
}

gboolean
imap_handle_idle_enable(ImapMboxHandle *h, int seconds)
{
  if( !imap_mbox_handle_can_do(h, IMCAP_IDLE))
    return FALSE;
  if(h->idle_issued) {
    fprintf(stderr, "IDLE already enabled\n");
    return FALSE;
  }
  if(!h->idle_enable_id)
    h->idle_enable_id = g_timeout_add(seconds*1000, idle_start, h);
  return TRUE;
}

gboolean
imap_handle_idle_disable(ImapMboxHandle *h)
{
  if(h->idle_enable_id) {
    g_source_remove(h->idle_enable_id);
    h->idle_enable_id = 0;
  }
  if(h->async_watch_id) {
    g_source_remove(h->async_watch_id);
    h->async_watch_id = 0;
    if(h->sio && h->idle_issued) {/* we might have been disconnected before */
      sio_write(h->sio,"DONE\r\n",6); sio_flush(h->sio);
      h->idle_issued = 0;
    }
  }
  return TRUE;
}

void
imap_handle_disconnect(ImapMboxHandle *h)
{
  imap_handle_idle_disable(h);
  if(h->sio) {
    sio_detach(h->sio); h->sio = NULL;
  }
  if(h->iochannel) {
    g_io_channel_unref(h->iochannel); h->iochannel = NULL;
  }
  close(h->sd);
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
imap_mbox_handle_connect(ImapMboxHandle* ret, const char *host, int over_ssl)
{
  ImapResult rc;

  g_return_val_if_fail(imap_mbox_is_disconnected(ret), IMAP_CONNECT_FAILED);
#if !defined(USE_TLS)
  if(over_ssl) {
    imap_mbox_handle_set_msg(ret,"SSL requested but SSL support not compiled");
    return IMAP_UNSECURE;
  }
#else
  ret->over_ssl = over_ssl;
#endif

  g_free(ret->host);   ret->host   = g_strdup(host);

  if( (rc=imap_mbox_connect(ret)) != IMAP_SUCCESS)
    return rc;

  rc = imap_authenticate(ret);

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
  

  if( (rc=imap_mbox_connect(h)) != IMAP_SUCCESS)
    return rc;

  if( (rc = imap_authenticate(h)) != IMAP_SUCCESS)
    return rc;

  imap_mbox_resize_cache(h, 0); /* invalidate cache */
  mbox_view_dispose(&h->mbox_view); /* FIXME: recreate it here? */

  if(h->mbox) {
    switch(imap_mbox_select(h, h->mbox, readonly)) {
    case IMR_OK: rc = IMAP_SUCCESS;       break;
    default:     rc = IMAP_SELECT_FAILED; break;
    }
  }
  return rc;
}

ImapTlsMode
imap_handle_set_tls_mode(ImapMboxHandle* r, ImapTlsMode state)
{
  ImapTlsMode res;
  g_return_val_if_fail(r,0);
  res = r->tls_mode;
  r->tls_mode = state;
  return res;
}

const char* msg_flags[6] = { 
  "seen", "answered", "flagged", "deleted", "draft", "recent"
};

struct ListData { 
  ImapListCb cb;
  void * cb_data;
};

int
imap_socket_open(const char* host, const char *def_port)
{
  static const int USEIPV6 = 1;
  int rc, fd = -1;
  
  /* --- IPv4/6 --- */
  /* "65536\0" */
  const char *port;
  char *hostname;
  struct addrinfo hints;
  struct addrinfo* res;
  struct addrinfo* cur;
  
  /* we accept v4 or v6 STREAM sockets */
  memset (&hints, 0, sizeof (hints));

  hints.ai_family = USEIPV6 ? AF_UNSPEC : AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  port = strrchr(host, ':');
  if (port) {
    hostname = g_strndup(host, port-host);
    port ++;
  } else {
    port = def_port;
    hostname = g_strdup(host);
  }
  rc = getaddrinfo(hostname, port, &hints, &res);
  g_free(hostname);
  if(rc)
    return -1;
  
  for (cur = res; cur != NULL; cur = cur->ai_next) {
    fd = socket (cur->ai_family, cur->ai_socktype, cur->ai_protocol);
    if (fd >= 0) {
      if ((rc=connect(fd, cur->ai_addr, cur->ai_addrlen)) == 0) {
	break;
      } else {
	close (fd);
        fd = -1;
      }
    }
  }
  freeaddrinfo (res);
  return fd; /* FIXME: provide more info why the connection failed. */
}

static ImapResult
imap_mbox_connect(ImapMboxHandle* handle)
{
  static const int SIO_BUFSZ=8192;
  ImapResponse resp;
  const char *service = "imap";

  /* reset some handle status */
  handle->has_capabilities = FALSE;
  handle->can_fetch_body = TRUE;
  handle->idle_issued = FALSE;
  if(handle->sio) {
    sio_detach(handle->sio);
    handle->sio = NULL;
  }

#ifdef USE_TLS
  handle->using_tls = 0;
  if(handle->over_ssl) service = "imaps";
#endif

  handle->sd = imap_socket_open(handle->host, service);
  if(handle->sd<0) return IMAP_CONNECT_FAILED;
  
  /* Add buffering to the socket */
  handle->sio = sio_attach(handle->sd, handle->sd, SIO_BUFSZ);
  if (handle->sio == NULL) {
    close(handle->sd);
    return IMAP_NOMEM;
  }
  if(handle->timeout>0)
    sio_set_timeout(handle->sio, handle->timeout);
#ifdef USE_TLS
  if(handle->over_ssl) {
    SSL *ssl = imap_create_ssl();
    if(!ssl) {
      imap_mbox_handle_set_msg(handle,"SSL context could not be created");
      return IMAP_UNSECURE;
    }
    if(imap_setup_ssl(handle->sio, handle->host, ssl,
                      handle->user_cb, handle->user_arg)) 
      handle->using_tls = 1;
    else {
      imap_mbox_handle_set_msg(handle,"SSL negotiation failed");
      imap_handle_disconnect(handle);
      return IMAP_UNSECURE;
    }
  }
#endif
  if(handle->monitor_cb) 
    sio_set_monitorcb(handle->sio, handle->monitor_cb, handle->monitor_arg);

  handle->state = IMHS_CONNECTED;
  if ( (resp=imap_cmd_step(handle, 0)) != IMR_UNTAGGED) {
    g_message("imap_mbox_connect:unexpected initial response(%d):\n%s\n",
	      resp, handle->last_msg);
    imap_handle_disconnect(handle);
    return IMAP_PROTOCOL_ERROR;
  }
  handle->can_fetch_body = 
    (strncmp(handle->last_msg, "Microsoft Exchange", 18) != 0);
#if defined(USE_TLS)
  if(handle->over_ssl)
    resp = IMR_OK; /* secured already with SSL */
  else if(handle->tls_mode != IMAP_TLS_DISABLED &&
          imap_mbox_handle_can_do(handle, IMCAP_STARTTLS)) {
    if( imap_handle_starttls(handle) != IMR_OK) {
      imap_mbox_handle_set_msg(handle,"TLS negotiation failed");
      return IMAP_UNSECURE; /* TLS negotiation error */
    }
    resp = IMR_OK; /* secured with TLS */
  } else
    resp = IMR_NO; /* not over SSL and TLS unavailable */
#else
  resp = IMR_NO;
#endif
  if(handle->tls_mode == IMAP_TLS_REQUIRED && resp != IMR_OK) {
    imap_mbox_handle_set_msg(handle,"TLS required but not available");
    return IMAP_UNSECURE;
  }
  return IMAP_SUCCESS;
}

unsigned
imap_make_tag(ImapCmdTag tag)
{
  static unsigned no = 0; /* MT-locking here */
  sprintf(tag, "%x", ++no);
  return no;
}

static int
imap_get_atom(struct siobuf *sio, char* atom, size_t len)
{
  unsigned i;
  int c = 0;
  for(i=0; i<len-1 && (c=sio_getc(sio)) >=0 && IS_ATOM_CHAR(c); i++)
    atom[i] = c;

  if(i<len-1) {
    if (c < 0)
      return c;
    atom[i] = '\0';
  } else atom[i+1] = '\0'; /* too long tag?  */
  return c;
}

#define IS_FLAG_CHAR(c) (strchr("(){ %*\"]",(c))==NULL&&(c)>0x1f&&(c)!=0x7f)
static int
imap_get_flag(struct siobuf *sio, char* flag, size_t len)
{
  unsigned i;
  int c = 0;
  for(i=0; i<len-1 && (c=sio_getc(sio)) >=0 && IS_FLAG_CHAR(c); i++)
    flag[i] = c;

  if(i<len-1) {
    if (c < 0)
      return c;
    flag[i] = '\0';
  } else flag[i+1] = '\0'; /* too long tag?  */
  return c;
}

/* we include '+' in TAG_CHAR because we want to treat forced responses
   in same code. This may be wrong. Reconsider.
*/
#define IS_TAG_CHAR(c) (strchr("(){ %\"\\]",(c))==NULL&&(c)>0x1f&&(c)!=0x7f)
static int
imap_cmd_get_tag(struct siobuf *sio, char* tag, size_t len)
{
  unsigned i;
  int c = 0;
  for(i=0; i<len-1 && (c=sio_getc(sio)) >=0 && IS_TAG_CHAR(c); i++) {
    tag[i] = c;
  }
  if(i<len-1) {
    if (c < 0)
      return c;
    tag[i] = '\0';
  } else tag[i+1] = '\0'; /* too long tag?  */
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
  /* FIXME: block other list response signals here? */
  guint handler_id = g_signal_connect(G_OBJECT(handle), "list-response",
                                      G_CALLBACK(get_delim),
                                       &delim);

  gchar * cmd = g_strdup_printf("LIST \"%s\" \"\"", namespace);
  imap_cmd_exec(handle, cmd);
  g_free(cmd);
  g_signal_handler_disconnect(G_OBJECT(handle), handler_id);
  return delim;

}

char*
imap_mbox_handle_get_last_msg(ImapMboxHandle *handle)
{
  return g_strdup(handle->last_msg ? handle->last_msg : "");
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
  g_return_if_fail(handle);

  if(handle->state != IMHS_DISCONNECTED) {
    handle->doing_logout = TRUE;
    imap_cmd_exec(handle, "LOGOUT");
  }
  imap_handle_disconnect(handle);
  g_free(handle->host);    handle->host   = NULL;
  g_free(handle->mbox);    handle->mbox   = NULL;
  g_free(handle->last_msg);handle->last_msg = NULL;

  g_list_foreach(handle->cmd_info, (GFunc)g_free, NULL);
  g_list_free(handle->cmd_info); handle->cmd_info = NULL;
  g_hash_table_destroy(handle->status_resps); handle->status_resps = NULL;

  mbox_view_dispose(&handle->mbox_view);
  imap_mbox_resize_cache(handle, 0);
  g_free(handle->msg_cache); handle->msg_cache = NULL;
  g_array_free(handle->flag_cache, TRUE);
#if defined(BALSA_USE_THREADS)
  pthread_mutex_destroy(&handle->mutex);
#endif

  G_OBJECT_CLASS(parent_class)->finalize(gobject);  
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
imap_mbox_handle_fetch(ImapMboxHandle* handle, const gchar *seq, 
                       const gchar* headers[])
{
  char* cmd;
  int i;
  GString* hdr;
  ImapResponse rc;
  
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
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
  
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
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

ImapMessage*
imap_mbox_handle_get_msg_v(ImapMboxHandle* h, unsigned no)
{
  g_return_val_if_fail(h, 0);
  g_return_val_if_fail(no-1<h->exists, NULL);
  if(mbox_view_is_active(&h->mbox_view))
    no = mbox_view_get_msg_no(&h->mbox_view, no);
  return h->msg_cache[no-1];
}
unsigned
imap_mbox_get_msg_no(ImapMboxHandle* h, unsigned no)
{
  g_return_val_if_fail(h, 0);
  if(!mbox_view_is_active(&h->mbox_view))
    return no;
  else
    return mbox_view_get_msg_no(&h->mbox_view, no);
}

unsigned
imap_mbox_get_rev_no(ImapMboxHandle* h, unsigned seqno)
{
  if(!mbox_view_is_active(&h->mbox_view))
    return seqno;
  else
    return mbox_view_get_rev_no(&h->mbox_view, seqno);
}


static void
set_view_cb(ImapMboxHandle* handle, unsigned seqno, void*arg)
{
  mbox_view_append_no(&handle->mbox_view, seqno);
}

unsigned
imap_mbox_set_view(ImapMboxHandle *h, ImapMsgFlag fl, gboolean state)
{
  char *flag;
  gchar * cmd;
  void *arg;
  ImapSearchCb cb;
  ImapResponse rc;

  mbox_view_dispose(&h->mbox_view);
  if(fl==0)
    return 1;

  switch(fl) {
  case IMSGF_SEEN:     flag = "SEEN"; break;
  case IMSGF_ANSWERED: flag = "ANSWERED"; break;
  case IMSGF_FLAGGED:  flag = "FLAGGED"; break;
  case IMSGF_DELETED:  flag = "DELETED"; break;
  case IMSGF_DRAFT:    flag = "DRAFT"; break;
  case IMSGF_RECENT:   flag = "RECENT"; break;
  default: return 1;
  }
  cb  = h->search_cb;  h->search_cb  = (ImapSearchCb)set_view_cb;
  arg = h->search_arg; h->search_arg = NULL;
  g_free(h->mbox_view.filter_str);
  h->mbox_view.filter_str = g_strconcat(state ? "" : "UN", flag, NULL);
  cmd = g_strconcat("SEARCH ALL ", h->mbox_view.filter_str, NULL);
  rc = imap_cmd_exec(h, cmd);
  g_free(cmd);
  h->search_cb = cb; h->search_arg = arg;
  return rc == IMR_OK;
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
  if(body->media_basic == IMBMEDIA_MULTIPART)
    imap_body_ext_mpart_free(&body->ext.mpart);
  if (body->dsp_params)
    g_hash_table_destroy (body->dsp_params);
  g_free(body->media_basic_name);
  g_free(body->media_subtype);
  g_free(body->desc);
  g_free(body->content_id);
  g_free(body->content_dsp_other);
  g_free(body->content_uri);
  g_hash_table_destroy(body->params);
  if(body->envelope) imap_envelope_free(body->envelope);
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
  /* chbm: why not just return media_basic_name always here ? cause we 
     canonize the common names ... */
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

static ImapBody*
get_body_from_section(ImapBody *body, const char *section)
{
  char * dot;
  int is_parent_a_message = 1;
  do {
    int no = atoi(section);

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
/* ================ END OF BODY STRUCTURE FUNCTIONS ==================== */


/* =================================================================== */
/*             IMAP MESSAGE HANDLING CODE                              */
/* =================================================================== */

ImapMessage*
imap_message_new(void)
{
  ImapMessage * msg=g_malloc0(sizeof(ImapMessage));
  msg->rfc822size=-1;
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
/* serialize message itself and the envelope - but not the body */
struct ImapMsgEnvSerialized {
  ssize_t total_size; /* for checksumming */
  /* Message */
  ImapUID      uid;
  ImapMsgFlags flags;
  ImapDate     internal_date; /* delivery date */
  ImapDate     date;          /* sending date */
  int rfc822size;
  ImapFetchType available_headers;
  gchar fetched_headers_first_char;
};

void*
imap_message_serialize(ImapMessage *imsg)
{
#define SER_STR_CNT 10
  gchar *strings[SER_STR_CNT];
  int    lengths[SER_STR_CNT], i;
  ssize_t tot_size;
  gchar *ptr;
  struct ImapMsgEnvSerialized *imes;
  if(!imsg->envelope) /* envelope is required */
    return NULL; 
  strings[0] = imsg->fetched_header_fields;
  strings[1] = imsg->envelope->subject;
  strings[2] = imap_address_to_string(imsg->envelope->from);
  strings[3] = imap_address_to_string(imsg->envelope->sender);
  strings[4] = imap_address_to_string(imsg->envelope->replyto);
  strings[5] = imap_address_to_string(imsg->envelope->to);
  strings[6] = imap_address_to_string(imsg->envelope->cc);
  strings[7] = imap_address_to_string(imsg->envelope->bcc);
  strings[8] = imsg->envelope->in_reply_to;
  strings[9] = imsg->envelope->message_id;
  tot_size = sizeof(struct ImapMsgEnvSerialized)-1;
  for(i=0; i<SER_STR_CNT; i++) {
    lengths[i] = strings[i] ? strlen(strings[i]) : 0;
    tot_size += 1 + lengths[i];
  }
  imes = g_malloc(tot_size);
  imes->total_size = tot_size;
  /* Message */
  imes->uid           = imsg->uid;
  imes->flags         = imsg->flags;
  imes->internal_date = imsg->internal_date; /* delivery date */
  imes->rfc822size    = imsg->rfc822size;
  imes->available_headers = imsg->available_headers;
  /* Envelope */
  imes->date = imsg->envelope->date;
  ptr = &imes->fetched_headers_first_char;
  for(i=0; i<SER_STR_CNT; i++) {
    if(strings[i]) strcpy(ptr, strings[i]);
    ptr += lengths[i];
    *ptr++ = '\0';
  }
  for(i=2; i<=7; i++)
    g_free(strings[i]);
  /* printf("Serialization offset: %d (tot size %d may include alignment)\n",
     ptr-(gchar*)imes, tot_size); */
  return imes;
}
/* obs - no data checking is done as it should be if the data were read
   from a file or other external source. */
ImapMessage*
imap_message_deserialize(void *data)
{
  struct ImapMsgEnvSerialized *imes = (struct ImapMsgEnvSerialized*)data;
  ImapMessage* imsg = imap_message_new();
  gchar *ptr;

  imsg->envelope = imap_envelope_new();
  imsg->uid = imes->uid;
  imsg->flags = imes->flags;
  imsg->internal_date = imes->internal_date; /* delivery date */
  imsg->rfc822size = imes->rfc822size;
  imsg->available_headers = imes->available_headers;
  /* Envelope */
  imsg->envelope->date = imes->date;
  ptr = &imes->fetched_headers_first_char;
  imsg->fetched_header_fields = *ptr ? g_strdup(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope->subject = *ptr ? g_strdup(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope->from    = *ptr ? imap_address_from_string(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope->sender  = *ptr ? imap_address_from_string(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope->replyto = *ptr ? imap_address_from_string(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope->to      = *ptr ? imap_address_from_string(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope->cc      = *ptr ? imap_address_from_string(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope->bcc     = *ptr ? imap_address_from_string(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope->in_reply_to = *ptr ? g_strdup(ptr) : NULL;
  ptr += strlen(ptr) + 1;
  imsg->envelope->message_id  = *ptr ? g_strdup(ptr) : NULL;
  return imsg;
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
  sio_write(handle->sio, tag, strlen(tag));
  sio_write(handle->sio, " ", 1);
  sio_write(handle->sio, cmd, strlen(cmd));
  sio_write(handle->sio, "\r\n", 2);
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

#ifdef USE_TLS
  if(ERR_peek_error()) {
    fprintf(stderr, "OpenSSL error in %s():\n", __FUNCTION__);
    ERR_print_errors_fp(stderr);
    fprintf(stderr, "\nEnd of print_errors - severing the connection...\n");
    imap_handle_disconnect(handle);
    return IMR_SEVERED;
  }
#endif
  ci = cmdi_find_by_no(handle->cmd_info, lastcmd);
  if(ci && ci->completed) {
    /* The response to this command has been encountered earlier,
       send it. */
    printf("Sending stored response to %x and removing info.\n",  lastcmd);
    rc = ci->rc;
    handle->cmd_info = g_list_remove(handle->cmd_info, ci);
    g_free(ci);
    return rc;
  }

  if( imap_cmd_get_tag(handle->sio, tag, sizeof(tag))<0) {
    printf("IMAP connection to %s severed.\n", handle->host);
    imap_handle_disconnect(handle);
    return IMR_SEVERED;
  }
  /* handle untagged messages. The caller still gets its shot afterwards. */
  if (strcmp(tag, "*") == 0) {
    rc = ir_handle_response(handle);
    if(rc == IMR_BYE) {
      return handle->doing_logout ? IMR_UNTAGGED : IMR_BYE;
    }
    return IMR_UNTAGGED;
  }

  /* server demands a continuation response from us */
  if (strcmp(tag, "+") == 0)
    return IMR_RESPOND;

  /* tagged completion code is the only alternative. */
  /* our command tags are hexadecimal numbers */
  if(sscanf(tag, "%x", &cmdno) != 1) {
    printf("scanning '%s' for tag number failed. Cannot recover.\n", tag);
    imap_handle_disconnect(handle);
    return IMR_BAD;
  }

  rc = ir_handle_response(handle);
  /* We check whether we encountered an response to a another,
       possibly asynchronous command, not the one we are currently
       executing. We store the response in the hash table so that we
       can provide a proper response somebody asks. */
  ci = cmdi_find_by_no(handle->cmd_info, cmdno);
  if(lastcmd != cmdno)
    printf("Looking for %x and encountered response to %x (%p)\n",
           lastcmd, cmdno, ci);
  if(ci) {
    if(ci->complete_cb && !ci->complete_cb(handle, ci->cb_data)) {
      ci->rc = rc;
      ci->completed = 1;
      printf("Cmd %x marked as completed with rc=%d\n", cmdno, rc);
    } else {
      printf("CmdInfo for cmd %x removed\n", cmdno);
      handle->cmd_info = g_list_remove(handle->cmd_info, ci);
      g_free(ci);
    }
  }
  return lastcmd == cmdno ? rc : IMR_UNTAGGED;
}

/* imap_cmd_exec: 
 * execute a command, and wait for the response from the server.
 * Also, handle untagged responses.
 * Returns ImapResponse.
 */
ImapResponse
imap_cmd_exec(ImapMboxHandle* handle, const char* cmd)
{
  unsigned cmdno;
  ImapResponse rc;

  g_return_val_if_fail(handle, IMR_BAD);
  if (handle->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;

  /* create sequence for command */
  imap_handle_idle_disable(handle);
  if (imap_cmd_start(handle, cmd, &cmdno)<0)
    return IMR_SEVERED;  /* irrecoverable connection error. */

  sio_flush(handle->sio);
  do {
    rc = imap_cmd_step (handle, cmdno);
  } while (rc == IMR_UNTAGGED);

  imap_handle_idle_enable(handle, IDLE_TIMEOUT);

  return rc;
}

int
imap_handle_write(ImapMboxHandle *conn, const char *buf, size_t len)
{
  g_return_val_if_fail(conn, -1);
  g_return_val_if_fail(conn->sio, -1);

  sio_write(conn->sio, buf, len); /* why it is void? */
  return 0;
}

void
imap_handle_flush(ImapMboxHandle *handle)
{
  g_return_if_fail(handle);
  g_return_if_fail(handle->sio);
  sio_flush(handle->sio);
}

char*
imap_mbox_gets(ImapMboxHandle *h, char* buf, size_t sz)
{
  char* rc;
  g_return_val_if_fail(h, NULL);
  g_return_val_if_fail(h->sio, NULL);

  rc = sio_gets(h->sio, buf, sz);
  if(rc == NULL)
      imap_handle_disconnect(h);
  return rc;
}

const char*
lbi_strerror(ImapResult rc)
{
  switch(rc) {
   
  case IMAP_SUCCESS:        return "action succeeded";
  case IMAP_NOMEM:          return "not enough memory";
  case IMAP_CONNECT_FAILED: return "transport level connect failed";
  case IMAP_PROTOCOL_ERROR: return "unexpected server response";
  case IMAP_AUTH_FAILURE:   return "authentication failure";
  case IMAP_AUTH_UNAVAIL:   return "no supported authentication method available ";
  case IMAP_UNSECURE:       return "secure connection requested but "
                              "could not be established.";
  case IMAP_SELECT_FAILED: return "SELECT command failed";
  default: return "Unknown error";
  }
}

static char*
imap_get_string_with_lookahead(struct siobuf* sio, int c)
{ /* string */
  char *res;
  
  if(c=='"') { /* quoted */
    GString *str = g_string_new("");
    while( (c=sio_getc(sio)) != '"') {
      if(c== '\\')
        c = sio_getc(sio);
      g_string_append_c(str, c);
    }
    res = g_string_free(str, FALSE);
  } else { /* this MUST be literal */
    char buf[15];
    int len;
    if(c!='{')
      return NULL;

    c = imap_get_atom(sio, buf, sizeof(buf));
    len = strlen(buf); 
    if(len==0 || buf[len-1] != '}') return NULL;
    buf[len-1] = '\0';
    len = atoi(buf);
    if( c != 0x0d) { printf("lit1:%d\n",c); return NULL;}
    if( (c=sio_getc(sio)) != 0x0a) { printf("lit1:%d\n",c); return NULL;}
    res = g_malloc(len+1);
    if(len>0) sio_read(sio, res, len);
    res[len] = '\0';
  }
  return res;
}

/* see the spec for the definition of string */
static char*
imap_get_string(struct siobuf* sio)
{
  return imap_get_string_with_lookahead(sio, sio_getc(sio));
}

static gboolean
imap_is_nil (struct siobuf *sio, int c)
{
  return g_ascii_toupper (c) == 'N' && g_ascii_toupper (sio_getc (sio)) == 'I'
    && g_ascii_toupper (sio_getc (sio)) == 'L';
}

/* see the spec for the definition of nstring */
static char*
imap_get_nstring(struct siobuf* sio)
{
  int c = sio_getc(sio);
  if(toupper(c)=='N') { /* nil */
    sio_getc(sio); sio_getc(sio); /* ignore i and l */
    return NULL;
  } else return imap_get_string_with_lookahead(sio, c);
}

/* see the spec for the definition of astring */
#define IS_ASTRING_CHAR(c) (strchr("(){ %*\"\\", (c))==0&&(c)>0x1F&&(c)!=0x7F)
static char*
imap_get_astring(struct siobuf *sio, int* lookahead)
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
    res = imap_get_string_with_lookahead(sio, c);
    *lookahead = sio_getc(sio);
  }
  return res;
}

/* this file contains all the response handlers as defined in
   draft-crspin-imapv-20.txt. 
  
   According to section 7 of this draft, "the client MUST be prepared
   to accept any response at all times".

   The code is closely based on sectin 9 - formal syntax.
*/

#include "imap-handle.h"

static void
ignore_bad_charset(struct siobuf *sio, int c)
{
  while(c==' ') {
    gchar * astring = imap_get_astring(sio, &c); 
    g_free(astring);
  }
  if(c != ')')
    fprintf(stderr,"ignore_bad_charset: expected ')' got '%c'\n", c);
}
static void
ir_permanent_flags(ImapMboxHandle *h)
{
  while(sio_getc(h->sio) != ']')
    ;
}
static int
ir_capability_data(ImapMboxHandle *handle)
{
  /* ordered identically as ImapCapability constants */
  static const char* capabilities[] = {
    "IMAP4", "IMAP4rev1", "STATUS", "ACL", "NAMESPACE",
    "AUTH=ANONYMOUS", "AUTH=CRAM-MD5", "AUTH=GSSAPI", "AUTH=PLAIN",
    "STARTTLS", "LOGINDISABLED", "SORT",
    "THREAD=ORDEREDSUBJECT", "THREAD=REFERENCES",
    "UNSELECT", "SCAN", "CHILDREN", "LITERAL+", "IDLE", "SASL-IR"
  };
  unsigned x;
  int c;
  char atom[LONG_STRING];

  memset (handle->capabilities, 0, sizeof (handle->capabilities));
  
  do {
    c = imap_get_atom(handle->sio, atom, sizeof(atom));
    for (x=0; x<ELEMENTS(capabilities); x++)
      if (g_ascii_strcasecmp(atom, capabilities[x]) == 0) {
	handle->capabilities[x] = 1;
	break;
      }
  } while(c==' ');
  handle->has_capabilities = TRUE;
  return c;
}

static ImapResponse
ir_resp_text_code(ImapMboxHandle *h)
{
  static const char* resp_text_code[] = {
    "ALERT", "BADCHARSET", "CAPABILITY","PARSE", "PERMANENTFLAGS",
    "READ-ONLY", "READ-WRITE", "TRYCREATE", "UIDNEXT", "UIDVALIDITY",
    "UNSEEN"
  };
  unsigned o;
  char buf[128];
  int c = imap_get_atom(h->sio, buf, sizeof(buf));
  ImapResponse rc = IMR_OK;


  for(o=0; o<ELEMENTS(resp_text_code); o++)
    if(g_ascii_strcasecmp(buf, resp_text_code[o]) == 0) break;

  switch(o) {
  case 0: rc = IMR_ALERT;        break;
  case 1: ignore_bad_charset(h->sio, c); break;
  case 2: ir_capability_data(h); break;
  case 3: rc = IMR_PARSE;        break;
  case 4: ir_permanent_flags(h); break;
  case 5: h->readonly_mbox = TRUE;  /* read-only */; break;
  case 6: h->readonly_mbox = FALSE; /* read-write */; break;
  case 7: /* ignore try-create */; break;
  case 8: imap_get_atom(h->sio, buf, sizeof(buf)); h->uidnext=atoi(buf); break;
  case 9: imap_get_atom(h->sio, buf, sizeof(buf)); h->uidval =atoi(buf); break;
  case 10:imap_get_atom(h->sio, buf, sizeof(buf)); h->unseen =atoi(buf); break;
  default: while( (c=sio_getc(h->sio)) != EOF && c != ']') ; break;
  }
  return c != EOF ? rc : IMR_SEVERED;
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
    sio_gets(h->sio, line, sizeof(line));
  } else {
    line[0] = c;
    sio_gets(h->sio, line+1, sizeof(line)-1);
    rc = IMR_OK;
  }
  if(rc == IMR_PARSE)
    rc = IMR_OK;
  else if( (l=strlen(line))>0) { 
    line[l-2] = '\0'; 
    imap_mbox_handle_set_msg(h, line);
    if(h->info_cb)
      h->info_cb(h, rc, line, h->info_arg);
    else
      printf("INFO : '%s'\n", line);
    rc = IMR_OK; /* in case it was IMR_ALERT */
  }
  return IMR_OK;
}

static ImapResponse
ir_no(ImapMboxHandle *h)
{
  char line[LONG_STRING];

  sio_gets(h->sio, line, sizeof(line));
  /* look for information response codes here: section 7.1 of the draft */
  if( strlen(line)>2) {
    imap_mbox_handle_set_msg(h, line);
    if(h->info_cb)
      h->info_cb(h, IMR_NO, line, h->info_arg);
    else
      printf("WARN : '%s'\n", line);
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
    imap_mbox_handle_set_msg(h, line);
    if(h->info_cb)
      h->info_cb(h, IMR_BAD, line, h->info_arg);
    else
      printf("ERROR: %s\n", line);
  }
  return IMR_BAD;
}

static ImapResponse
ir_preauth(ImapMboxHandle *h)
{
  if(imap_mbox_handle_get_state(h) == IMHS_CONNECTED)
    imap_mbox_handle_set_state(h, IMHS_AUTHENTICATED);
  return IMR_OK;
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
    imap_mbox_handle_set_msg(h, line);
    imap_mbox_handle_set_state(h, IMHS_DISCONNECTED);
    /* we close the connection here unless we are doing logout. */
    if(h->sio) { sio_detach(h->sio); h->sio = NULL; }
    close(h->sd);
  }
  return IMR_BYE;
}

static ImapResponse
ir_check_crlf(ImapMboxHandle *h, int c)
{
  if( c != 0x0d) {
    printf("CR:%d\n",c);
    return IMR_PROTOCOL;
  }
  if( (c=sio_getc(h->sio)) != 0x0a) {
    printf("LF:%d\n",c);
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
    for(i=0; i< ELEMENTS(mbx_flags); i++) {
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
  g_signal_emit(G_OBJECT(h), imap_mbox_handle_signals[signal],
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
      for(idx=0; idx<ELEMENTS(imap_status_item_names); idx++)
        if(g_ascii_strcasecmp(item, imap_status_item_names[idx]) == 0)
          break;
      for(i= 0; resp[i].item != IMSTAT_NONE; i++) {
        if(resp[i].item == idx) {
          if (sscanf(count, "%u", &resp[i].result) != 1) {
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

static ImapResponse
ir_search(ImapMboxHandle *h)
{
  int c;
  char seq[12];

  while ((c=imap_get_atom(h->sio, seq, sizeof(seq))), seq[0]) {
    if(h->search_cb)
      h->search_cb(h, atoi(seq), h->search_arg);
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
    mbox_view_append_no(&h->mbox_view, atoi(seq));
    if(c == '\r') break;
  }
  return ir_check_crlf(h, c);
}

static ImapResponse
ir_flags(ImapMboxHandle *h)
{
  /* FIXME: implement! */
  int c; while( (c=sio_getc(h->sio))!=EOF && c != 0x0a);
  return IMR_OK;
}

static ImapResponse
ir_exists(ImapMboxHandle *h, unsigned seqno)
{
  unsigned old_exists = h->exists;
  ImapResponse rc = ir_check_crlf(h, sio_getc(h->sio));
  imap_mbox_resize_cache(h, seqno);
  mbox_view_resize(&h->mbox_view, old_exists, seqno);

  g_signal_emit(G_OBJECT(h), imap_mbox_handle_signals[EXISTS_NOTIFY], 0);
                
  return rc;
}

static ImapResponse
ir_recent(ImapMboxHandle *h, unsigned seqno)
{
  h->recent = seqno;
  /* FIXME: send a signal here! */
  return ir_check_crlf(h, sio_getc(h->sio));
}

static ImapResponse
ir_expunge(ImapMboxHandle *h, unsigned seqno)
{
  ImapResponse rc = ir_check_crlf(h, sio_getc(h->sio));
  g_signal_emit(G_OBJECT(h), imap_mbox_handle_signals[EXPUNGE_NOTIFY],
		0, seqno);
  
  g_array_remove_index(h->flag_cache, seqno-1);
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
    for(i=0; i<ELEMENTS(msg_flags); i++)
      if(buf[0] == '\\' && g_ascii_strcasecmp(msg_flags[i], buf+1) == 0) {
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

static gchar*
get_quoted_string(const gchar *source, gchar const **endpos)
{
  GString *s;
  if(*source == '\0') {
    *endpos = source;
    return NULL;
  }
  if(*source != '"') {
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
static ImapAddress*
imap_address_from_string(const gchar *string)
{
  const gchar *t, *t1;
  gchar *comment, *mailbox;
  ImapAddress *res = NULL;

  comment = get_quoted_string(string, &t);
  if(*t == ' ') {
    mailbox = get_quoted_string(t+1, &t1);
    if(comment || mailbox) {
      res = imap_address_new(comment, mailbox);
      if(*t1 == ' ')
        res->next = imap_address_from_string(t1+1);
    }
  } else g_free(comment);
  return res;
}

static gchar*
imap_address_to_string(const ImapAddress *addr)
{
  GString *res = g_string_sized_new(4);
  gchar *p;
  for(; addr; addr = addr->next) {
    if(addr->name) {
      g_string_append_c(res, '"');
      for(p=addr->name; *p; p++) {
        if(*p == '\\' || *p == '"') g_string_append_c(res, '\\');
        g_string_append_c(res, *p);
      }
      g_string_append_c(res, '"');
    } else g_string_append_c(res, 'N');
    g_string_append_c(res, ' ');
    if(addr->addr_spec) {
      g_string_append_c(res, '"');
      for(p=addr->addr_spec; *p; p++) {
        if(*p == '\\' || *p == '"') g_string_append_c(res, '\\');
        g_string_append_c(res, *p);
      }
      g_string_append_c(res, '"');
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
imap_get_address(struct siobuf* sio)
{
  char *addr[4];
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
    if( (c=sio_getc(sio)) != ' '); /* error if i < 3 but do nothing */
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
imap_get_addr_list (struct siobuf *sio, ImapAddress ** list)
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
ir_envelope(struct siobuf *sio, ImapEnvelope *env)
{
  int c;
  char *date, *str;

  if( (c=sio_getc(sio)) != '(') return IMR_PROTOCOL;
  date = imap_get_nstring(sio);
  if(date) {
    if(env) env->date = g_mime_utils_header_decode_date(date, NULL);
    g_free(date);
  }
  if( (c=sio_getc(sio)) != ' ') return IMR_PROTOCOL;
  str = imap_get_nstring(sio);
  if(env) env->subject = str; else g_free(str);
  if( (c=sio_getc(sio)) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->from : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if( (c=sio_getc(sio)) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->sender : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if( (c=sio_getc(sio)) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->replyto : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if( (c=sio_getc(sio)) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->to : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if( (c=sio_getc(sio)) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->cc : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if( (c=sio_getc(sio)) != ' ') return IMR_PROTOCOL;
  if(imap_get_addr_list(sio, env ? &env->bcc : NULL) != IMR_OK)
    return IMR_PROTOCOL;
  if( (c=sio_getc(sio)) != ' ') return IMR_PROTOCOL;
  str = imap_get_nstring(sio);
  if(env) env->in_reply_to = str; else g_free(str);
  if( (c=sio_getc(sio)) != ' ') { printf("c=%c\n",c); return IMR_PROTOCOL;}
  str = imap_get_nstring(sio);
  if(env) env->message_id = str; else g_free(str);
  if( (c=sio_getc(sio)) != ')') { printf("c=%d\n",c);return IMR_PROTOCOL;}
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
    h->body_cb(seqno, str, strlen(str), h->body_arg);
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
  
  msg->rfc822size = atoi(buf);  
  return IMR_OK;
}

static ImapResponse
ir_media(struct siobuf* sio, ImapMediaBasic *imb, ImapBody *body)
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
ir_body_fld_param_hash(struct siobuf* sio, GHashTable * params)
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
    } while( (c=sio_getc(sio)) != ')');
  } else if(toupper(c) != 'N' || toupper(sio_getc(sio)) != 'I' ||
            toupper(sio_getc(sio)) != 'L') return IMR_PROTOCOL;
  return IMR_OK;
}

static ImapResponse
ir_body_fld_param(struct siobuf* sio, ImapBody *body)
{
  return ir_body_fld_param_hash(sio, body ? body->params : NULL);
}

static ImapResponse
ir_body_fld_id(struct siobuf* sio, ImapBody *body)
{
  gchar* id = imap_get_nstring(sio);

  if(body)
    imap_body_set_id(body, id);
  else g_free(id);

  return IMR_OK;
}

static ImapResponse
ir_body_fld_desc(struct siobuf* sio, ImapBody *body)
{
  gchar* desc = imap_get_nstring(sio);
  if(body)
    imap_body_set_desc(body, desc);
  else g_free(desc);

  return IMR_OK;
}

static ImapResponse
ir_body_fld_enc(struct siobuf* sio, ImapBody *body)
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
ir_body_fld_octets(struct siobuf* sio, ImapBody *body)
{
  char buf[12];
  int c = imap_get_atom(sio, buf, sizeof(buf));

  if(c!= -1) sio_ungetc(sio);
  if(body) body->octets = atoi(buf);  
  
  return IMR_OK;
}

static ImapResponse
ir_body_fields(struct siobuf* sio, ImapBody *body)
{
  ImapResponse rc;
  int c;

  if( (rc=ir_body_fld_param (sio, body))!=IMR_OK) return rc;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if( (rc=ir_body_fld_id    (sio, body))!=IMR_OK) return rc;
  if((c=sio_getc(sio)) != ' ') { printf("err=%c\n", c); return IMR_PROTOCOL; }
  if( (rc=ir_body_fld_desc  (sio, body))!=IMR_OK) return rc;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if( (rc=ir_body_fld_enc   (sio, body))!=IMR_OK) return rc;
  if(sio_getc(sio) != ' ') return IMR_PROTOCOL;
  if( (rc=ir_body_fld_octets(sio, body))!=IMR_OK) return rc;

  return IMR_OK;
}

static ImapResponse
ir_body_fld_lines(struct siobuf* sio, ImapBody* body)
{
  char buf[12];
  int c = imap_get_atom(sio, buf, sizeof(buf));

  if(c!= -1) sio_ungetc(sio);
  if(body) body->lines = atoi(buf);  
  
  return IMR_OK;
}

/* body_fld_dsp = "(" string SP body_fld_param ")" / nil */
static ImapResponse
ir_body_fld_dsp (struct siobuf *sio, ImapBody * body)
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
ir_body_fld_lang (struct siobuf *sio, ImapBody * body)
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
ir_body_extension (struct siobuf *sio, ImapBody * body)
{
  ImapResponse rc;
  int c;

  c = sio_getc (sio);
  if (c == '(')
    {
      /* "(" body-extension *(SP body-extension) ")" */
      do
	{
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
    IMB_EXTENSIBLE
};
typedef enum _ImapBodyExtensibility ImapBodyExtensibility;

/* body-ext-mpart  = body-fld-param [SP body-fld-dsp [SP body-fld-lang
 *                   [SP body-fld-loc *(SP body-extension)]]]
 *                   ; MUST NOT be returned on non-extensible
 *                   ; "BODY" fetch
 */

static ImapResponse
ir_body_ext_mpart (struct siobuf *sio, ImapBody * body,
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
ir_body_ext_1part (struct siobuf *sio, ImapBody * body,
		   ImapBodyExtensibility type)
{
  ImapResponse rc;
  char *str;

  if (type == IMB_NON_EXTENSIBLE)
    return IMR_PROTOCOL;

  /* body_fld_md5 = nstring */
  str = imap_get_nstring (sio);
  if (body && str)
    body->ext.onepart.md5 = str;
  else
    g_free (str);

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

/* body-type-mpart = 1*body SP media-subtype
 *                   [SP body-ext-mpart]
 */
static ImapResponse ir_body (struct siobuf *sio, int c, ImapBody * body,
			     ImapBodyExtensibility type);
static ImapResponse
ir_body_type_mpart (struct siobuf *sio, ImapBody * body,
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
ir_body_type_1part (struct siobuf *sio, ImapBody * body,
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
      if (rc != IMR_OK)
	{
	  if (env)
	    imap_envelope_free (env);
	  return rc;
	}
      if (sio_getc (sio) != ' ')
	return IMR_PROTOCOL;
      if (body)
	{
	  b = imap_body_new ();
	  body->envelope = env;
	}
      else
	b = NULL;
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
  rc = ir_body_ext_1part (sio, body, type);
  if (rc != IMR_OK)
    return rc;

  return IMR_OK;
}

/* body = "(" (body-type-1part / body-type-mpart) ")" */
static ImapResponse
ir_body (struct siobuf *sio, int c, ImapBody * body,
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
ir_body_section(struct siobuf *sio, unsigned seqno,
		ImapFetchBodyCb body_cb, void *arg)
{
  char buf[80], *str;
  int c = imap_get_atom(sio, buf, sizeof(buf));
  if(c != ']') { puts("] expected"); return IMR_PROTOCOL; }
  if(sio_getc(sio) != ' ') { puts("space expected"); return IMR_PROTOCOL;}
  str = imap_get_nstring(sio);
  if(str) {
    if(body_cb)
      body_cb(seqno, str, strlen(str), arg);
    g_free(str);
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
    if(tmp) h->body_cb(seqno, tmp, strlen(tmp), h->body_arg);
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
      rc = ir_body_section(h->sio, seqno, h->body_cb, h->body_arg);
      break;
    }
    c = imap_get_atom(h->sio, buf, sizeof buf);
    if (c == ']' &&
        g_ascii_strcasecmp(buf, "HEADER") == 0) {
      sio_ungetc (h->sio); /* put the ']' back */
      rc = ir_body_section(h->sio, seqno, h->body_cb, h->body_arg);
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
  h->msg_cache[seqno-1]->uid = atoi(buf);
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
    { "RFC822",        ir_msg_att_rfc822 },     
    { "RFC822.HEADER", ir_msg_att_rfc822_header }, 
    { "RFC822.TEXT",   ir_msg_att_rfc822_text }, 
    { "RFC822.SIZE",   ir_msg_att_rfc822_size }, 
    { "BODY",          ir_msg_att_body }, 
    { "BODYSTRUCTURE", ir_msg_att_bodystructure }, 
    { "UID",           ir_msg_att_uid }
  };
  char atom[LONG_STRING]; /* make sure LONG_STRING is longer than all */
                          /* strings above */
  unsigned i;
  int c;
  ImapResponse rc;

  if(seqno<1 || seqno > h->exists) return IMR_PROTOCOL;
  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;
  do {
    for(i=0; (c = sio_getc(h->sio)) != -1; i++) {
      c = toupper(c);
      if( !( (c >='A' && c<='Z') || (c >='0' && c<='9') || c == '.') ) break;
      atom[i] = c;
    }
    atom[i] = '\0';
    for(i=0; i<ELEMENTS(msg_att); i++) {
      if(strcmp(atom, msg_att[i].name) == 0) {
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
  seqno = atoi(buf);
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

  seqno = atoi(buf);
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
  { "SEARCH",     6, ir_search },
  { "SORT",       4, ir_sort   },
  { "THREAD",     6, ir_thread },
  { "FLAGS",      5, ir_flags  },
  /* FIXME Is there an unnumbered FETCH response? */
  { "FETCH",      5, ir_fetch  }
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
  unsigned i, seqno;
  ImapResponse rc = IMR_BAD; /* unknown response is really an error */

  c = imap_get_atom(h->sio, atom, sizeof(atom));
  if( isdigit(atom[0]) ) {
    if (c != ' ')
      return IMR_PROTOCOL;
    seqno = atoi(atom);
    c = imap_get_atom(h->sio, atom, sizeof(atom));
    if (c == 0x0d)
      sio_ungetc(h->sio);
    for(i=0; i<ELEMENTS(NumHandlers); i++) {
      if(g_ascii_strncasecmp(atom, NumHandlers[i].response, 
                             NumHandlers[i].keyword_len) == 0) {
        rc = NumHandlers[i].handler(h, seqno);
        break;
      }
    }
  } else {
    if (c == 0x0d)
      sio_ungetc(h->sio);
    for(i=0; i<ELEMENTS(ResponseHandlers); i++) {
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
