/* imap-handle.c:
   Pawel Salek, pawsa@theochem.kth.se, 2003.04.04
   The main IMAP client handling code.
   License: GPL
*/
#include "config.h"

#define _POSIX_SOURCE 1
#define _BSD_SOURCE   1
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

#include "libimap-marshal.h"
#include "imap-auth.h"
#include "imap-handle.h"
#include "imap-commands.h"
#include "imap_private.h"
#include "siobuf.h"
#include "util.h"

FILE *debug_stream = NULL;

#define LONG_STRING 512
#define ELEMENTS(x) (sizeof (x) / sizeof(x[0]))

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


static void mbox_view_append_no(MboxView *mv, unsigned seqno);

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

#define IMAP_MBOX_IS_DISCONNECTED(h)  ((h)->state == IMHS_DISCONNECTED)
#define IMAP_MBOX_IS_CONNECTED(h)     ((h)->state == IMHS_CONNECTED)
#define IMAP_MBOX_IS_AUTHENTICATED(h) ((h)->state == IMHS_AUTHENTICATED)
#define IMAP_MBOX_IS_SELECTED(h)      ((h)->state == IMHS_SELECTED)

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
  handle->user   = NULL;
  handle->passwd = NULL;
  handle->mbox   = NULL;
  handle->state  = IMHS_DISCONNECTED;
  handle->has_capabilities = FALSE;
  handle->exists = 0;
  handle->recent = 0;
  handle->msg_cache = NULL;
  handle->doing_logout = FALSE;

  handle->info_cb  = NULL;
  handle->info_arg = NULL;
  mbox_view_init(&handle->mbox_view);
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
                 libimap_VOID__INT_STRING_POINTER, G_TYPE_NONE, 3,
                 G_TYPE_INT, G_TYPE_STRING, G_TYPE_POINTER);

  imap_mbox_handle_signals[LSUB_RESPONSE] = 
    g_signal_new("lsub-response",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 G_STRUCT_OFFSET(ImapMboxHandleClass, lsub_response),
                 NULL, NULL,
                 libimap_VOID__INT_STRING_POINTER, G_TYPE_NONE, 3,
                 G_TYPE_INT, G_TYPE_STRING, G_TYPE_POINTER);

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
imap_handle_set_infocb(ImapMboxHandle* h, ImapInfoCb cb, void *arg)
{
  h->info_cb  = cb;
  h->info_arg = arg;
}
void
imap_handle_set_alertcb(ImapMboxHandle* h, ImapInfoCb cb, void *arg)
{
  h->alert_cb  = cb;
  h->alert_arg = arg;
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

int imap_mbox_is_disconnected (ImapMboxHandle *h)
{ return IMAP_MBOX_IS_DISCONNECTED(h); }
int imap_mbox_is_connected    (ImapMboxHandle *h)
{ return IMAP_MBOX_IS_CONNECTED(h); }
int imap_mbox_is_authenticated(ImapMboxHandle *h)
{ return IMAP_MBOX_IS_AUTHENTICATED(h); }
int imap_mbox_is_selected     (ImapMboxHandle *h)
{ return IMAP_MBOX_IS_SELECTED(h); }

ImapResult
imap_mbox_handle_connect(ImapMboxHandle* ret, const char *host,
                         const char* user, const char* passwd)
{
  ImapResult rc;

  g_return_val_if_fail(imap_mbox_is_disconnected(ret), IMAP_CONNECT_FAILED);

  g_free(ret->host);   ret->host   = g_strdup(host);
  g_free(ret->user);   ret->user   = g_strdup(user);
  g_free(ret->passwd); ret->passwd = g_strdup(passwd);

  if( (rc=imap_mbox_connect(ret)) != IMAP_SUCCESS)
    return rc;

  rc = imap_authenticate(ret, user, passwd);

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

  if( (rc = imap_authenticate(h, h->user, h->passwd)) != IMAP_SUCCESS)
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

const char* msg_flags[] = { 
  "seen", "answered", "flagged", "deleted", "draft", "recent"
};


struct ListData { 
  ImapListCb cb;
  void * cb_data;
};

static int
socket_open(const char* host, const char *def_port)
{
  static const int USEIPV6 = 0;
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
      int sa_size = sizeof (struct sockaddr_in);
      if (cur->ai_addr->sa_family == AF_INET6)
        sa_size = sizeof (struct sockaddr_in6);
      if ((rc=connect(fd, cur->ai_addr, sa_size)) == 0) {
	break;
      } else {
	close (fd);
        fd = -1;
      }
    }
  }
  freeaddrinfo (res);
  return fd;
}

static ImapResult
imap_mbox_connect(ImapMboxHandle* handle)
{
  static const int SIO_BUFSZ=8192;

  handle->sd = socket_open(handle->host, "imap");
  if(handle->sd<0) return IMAP_CONNECT_FAILED;
  
  /* Add buffering to the socket */
  handle->sio = sio_attach(handle->sd, handle->sd, SIO_BUFSZ);
  if (handle->sio == NULL) {
    close(handle->sd);
    return IMAP_NOMEM;
  }
  if(handle->monitor_cb) 
    sio_set_monitorcb(handle->sio, handle->monitor_cb, handle->monitor_arg);

  handle->state = IMHS_CONNECTED;
  if (imap_cmd_step(handle, "") != IMR_UNTAGGED) {
    g_warning("imap_mbox_connect:unexpected initial response\n");
    sio_detach(handle->sio);
    close(handle->sd);
    handle->state = IMHS_DISCONNECTED;
    return IMAP_PROTOCOL_ERROR;
  }

  /* 
  if (imap_mbox_handle_can_do(handle, IMCAP_STARTTLS))
    printf("Server is TLS capable.\n");
  */
  return IMAP_SUCCESS;
}

static void
imap_make_tag(ImapCmdTag tag)
{
  static unsigned no = 0; /* MT-locking here */
  sprintf(tag, "a%06x", no++);
}

#define IS_ATOM_CHAR(c) (strchr("(){ %*\"\\]",(c))==NULL&&(c)>0x1f&&(c)!=0x7f)
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

  
ImapMboxHandleState
imap_mbox_handle_get_state(ImapMboxHandle *h)
{
  return h->state; 
}

void
imap_mbox_handle_set_state(ImapMboxHandle *h, ImapMboxHandleState newstate)
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
  handle->state = IMHS_DISCONNECTED;
  if(handle->sio) { sio_detach(handle->sio); handle->sio = NULL; }
  close(handle->sd);
  g_free(handle->host);    handle->host   = NULL;
  g_free(handle->user);    handle->user   = NULL;
  g_free(handle->passwd);  handle->passwd = NULL;
  g_free(handle->mbox);    handle->mbox   = NULL;
  mbox_view_dispose(&handle->mbox_view);
  imap_mbox_resize_cache(handle, 0);
}


ImapResponse
imap_mbox_handle_fetch(ImapMboxHandle* handle, const gchar *seq, 
                       const gchar* headers[])
{
  char* cmd;
  int i;
  GString* hdr = g_string_new("FLAGS");
  ImapResponse rc;
  
  for(i=0; headers[i]; i++) {
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
  return 1;
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
  if(env->from)        g_free(env->from);
  if(env->sender)      g_free(env->sender);
  if(env->replyto)     g_free(env->replyto);
  if(env->to)          g_free(env->to);
  if(env->cc)          g_free(env->cc);
  if(env->bcc)         g_free(env->bcc);
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
static gboolean
imap_str_case_equal(gconstpointer v, gconstpointer v2)
{
  return g_ascii_strcasecmp(v, v2) == 0;
}

ImapBody*
imap_body_new(void)
{
  ImapBody *body = g_malloc0(sizeof(ImapBody));
  body->params = g_hash_table_new_full(g_str_hash, imap_str_case_equal, 
                                       g_free, g_free);
  return body;
}

static void
imap_body_ext_mpart_free (ImapBodyExtMPart * mpart)
{
  if (mpart->params)
    g_hash_table_destroy (mpart->params);
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
  g_hash_table_insert(body->params, key, val);
}

const gchar*
imap_body_get_param(ImapBody *body, const gchar *param)
{
  return g_hash_table_lookup(body->params, param);
}

gchar*
imap_body_get_mime_type(ImapBody *body)
{
  const gchar* type = NULL;

  /* chbm: why not just return media_basic_name always here ? cause we 
     canonize the common names ... */
  switch(body->media_basic) {
  case IMBMEDIA_MULTIPART:      type = "multipart"; break;
  case IMBMEDIA_APPLICATION:    type = "application"; break;
  case IMBMEDIA_AUDIO:          type = "audio"; break;
  case IMBMEDIA_IMAGE:          type = "image"; break;
  case IMBMEDIA_MESSAGE_RFC822: return g_strdup("message/rfc822"); break;
  case IMBMEDIA_MESSAGE_OTHER:  type = "message_other"; break;
  case IMBMEDIA_TEXT:           type = "text"; break;
  case IMBMEDIA_OTHER:          return g_strdup(body->media_basic_name);break;
  }
  return g_strconcat(type, "/", body->media_subtype, NULL);
}

static ImapBody*
get_body_from_section(ImapBody *body, const char *section)
{
  char * dot;
  int no = atoi(section);
  if(body && body->media_basic == IMBMEDIA_MULTIPART)
    body = body->child;
  while(--no && body)
    body = body->next;

  if(!body) return NULL; /* non-existing section */
  dot = strchr(section, '.');
  if(dot) return get_body_from_section(body->child, dot+1);
  else return body;
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
  if(id) printf("part ID='%s'\n", id);
  g_free(id);
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
  g_free(msg);
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

int
imap_cmd_start(ImapMboxHandle* handle, const char* cmd, ImapCmdTag tag)
{
  int rc;

  g_return_val_if_fail(handle, -1);
  imap_make_tag(tag);
  rc = sio_printf(handle->sio, "%s %s\r\n", tag, cmd);
  if(rc<0) {
    sio_detach(handle->sio); close(handle->sd);
    handle->state = IMHS_DISCONNECTED;
  }
  return rc;
}

/* imap_cmd_step:
 * Reads server responses from an IMAP command, detects
 * tagged completion response, handles untagged messages.
 * Reads only as much as needed using buffered input.
 */
ImapResponse
imap_cmd_step(ImapMboxHandle* handle, const gchar* cmd)
{
  char tag[12];

  /* FIXME: sanity test */
  g_return_val_if_fail(handle, IMR_BAD);
  g_return_val_if_fail(handle->state != IMHS_DISCONNECTED, IMR_BAD);

  imap_cmd_get_tag(handle->sio, tag, sizeof(tag)); /* handle errors */
  /* handle untagged messages. The caller still gets its shot afterwards. */
    if (strcmp(tag, "*") == 0) {
      ImapResponse rc = ir_handle_response(handle);
      if(rc == IMR_BYE) {
        return handle->doing_logout ? IMR_UNTAGGED : IMR_BYE;
      }
      if( rc!=IMR_OK)
        puts("cmd_step protocol error");
      return IMR_UNTAGGED;
    }

  /* server demands a continuation response from us */
  if (strcmp(tag, "+") == 0)
    return IMR_RESPOND;

  /* tagged completion code is the only alternative. */
  if (strcmp(tag,cmd) == 0) {
     return ir_handle_response(handle);
  }
  printf("tag='%s'\n", tag);
  g_assert_not_reached(); /* or protocol error */
  return IMR_UNTAGGED; 
}

/* imap_cmd_exec: 
 * execute a command, and wait for the response from the server.
 * Also, handle untagged responses.
 * Returns ImapResponse.
 */
ImapResponse
imap_cmd_exec(ImapMboxHandle* handle, const char* cmd)
{
  ImapCmdTag tag;
  ImapResponse rc;

  g_return_val_if_fail(handle, IMR_BAD);
  if (handle->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;

  /* create sequence for command */
  rc = imap_cmd_start(handle, cmd, tag);
  if (rc<0) /* irrecoverable connection error. */
    return IMR_SEVERED;

  sio_flush(handle->sio);
  do {
    rc = imap_cmd_step (handle, tag);
  } while (rc == IMR_UNTAGGED);

  if (rc != IMR_OK)
    g_warning("cmd '%s' failed, rc=%d.\n", cmd, rc);

  return rc;
}

int
imap_handle_write(ImapMboxHandle *conn, const char *buf, size_t len)
{
  g_return_val_if_fail(conn, -1);

  sio_write(conn->sio, buf, len); /* why it is void? */
  return 0;
}

void
imap_handle_flush(ImapMboxHandle *handle)
{
  g_return_if_fail(handle);
  sio_flush(handle->sio);
}

char*
imap_mbox_gets(ImapMboxHandle *h, char* buf, size_t sz)
{
  char* rc;
  g_return_val_if_fail(h, NULL);

  rc = sio_gets(h->sio, buf, sz);
  if(rc == NULL) {
    sio_detach(h->sio);
    h->state = IMHS_DISCONNECTED;
  }
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
    sio_read(sio, res, len);
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

ImapResponse (*handler)(ImapMboxHandle *h);

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
    "IMAP4", "IMAP4rev1", "STATUS", "ACL", "NAMESPACE", "AUTH=CRAM-MD5", 
    "AUTH=GSSAPI", "AUTH=ANONYMOUS", "STARTTLS", "LOGINDISABLED",
    "THREAD=ORDEREDSUBJECT", "THREAD=REFERENCES"
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
  case 0: if(h->alert_cb) h->alert_cb("ALERT", h->alert_arg);/* FIXME*/break;
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
  return rc;
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
    if(h->info_cb)
      h->info_cb(line, h->info_arg);
    else
      printf("INFO : '%s'\n", line);
  }
  return IMR_OK;
}

static ImapResponse
ir_no(ImapMboxHandle *h)
{
  char line[LONG_STRING];
  puts("ir_no");
  sio_gets(h->sio, line, sizeof(line));
  /* look for information response codes here: section 7.1 of the draft */
  if( strlen(line)>2) {
    if(h->info_cb)
      h->info_cb(line, h->info_arg);
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
    if(h->info_cb)
      h->info_cb(line, h->info_arg);
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
  int c; while( (c=sio_getc(h->sio))!=EOF && c != 0x0a);
  if(!h->doing_logout) {/* it is not we, so it must be the server */
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
  printf("ir_capability, c='%c'[%d]\n", c, c);
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
  int flags = 0;
  char buf[LONG_STRING], *s;
  int c, delim;

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
    if(sio_getc(h->sio) != 'N' ||
       sio_getc(h->sio) != 'I' ||
       sio_getc(h->sio) != 'L') return IMR_PROTOCOL;
  }
  if(sio_getc(h->sio) != ' ') return IMR_PROTOCOL;
  /* mailbox */
  s = imap_get_astring(h->sio, &c);
  g_signal_emit(G_OBJECT(h), imap_mbox_handle_signals[signal],
                0, delim, &flags, s);
  g_free(s);
  return ir_check_crlf(h, c);
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

static ImapResponse
ir_status(ImapMboxHandle *h)
{
  /* FIXME: implement! */
  int c; while( (c=sio_getc(h->sio))!=EOF && c != 0x0a);
  return IMR_OK;
}

static ImapResponse
ir_search(ImapMboxHandle *h)
{
  int c;
  char seq[12];

  while ((c=imap_get_atom(h->sio, seq, sizeof(seq))), seq[0])
    if(h->search_cb)
      h->search_cb(h, atoi(seq), h->search_arg);
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

  imap_mbox_resize_cache(h, seqno);
  mbox_view_resize(&h->mbox_view, old_exists, seqno);

  g_signal_emit(G_OBJECT(h), imap_mbox_handle_signals[EXISTS_NOTIFY], 0);
                
  return ir_check_crlf(h, sio_getc(h->sio));
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
  g_signal_emit(G_OBJECT(h), imap_mbox_handle_signals[EXPUNGE_NOTIFY],
		0, seqno);
  
  if(h->msg_cache[seqno-1] != NULL)
    imap_message_free(h->msg_cache[seqno-1]);
  while(seqno<h->exists) {
    h->msg_cache[seqno-1] = h->msg_cache[seqno];
    seqno++;
  }
  h->exists--;
  mbox_view_expunge(&h->mbox_view, seqno);
  return ir_check_crlf(h, sio_getc(h->sio));
}

static ImapResponse
ir_msg_att_flags(ImapMboxHandle *h, int c, unsigned seqno)
{
  unsigned i;
  ImapMessage *msg = h->msg_cache[seqno-1];

  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;
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

  if(h->flags_cb)
    h->flags_cb(seqno, h->flags_arg);
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

/* imap_get_address: returns null if no beginning of address is found
   (eg., when end of list is found instead).
*/
static ImapAddress*
imap_get_address(struct siobuf* sio)
{
  char *addr[4];
  ImapAddress *res = NULL;
  int i, c;

  if((c=sio_getc(sio)) != '(') {
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
  ImapMessage *msg = h->msg_cache[seqno-1];
  ImapEnvelope *env;

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
  if(h->body_cb)
    h->body_cb(str, strlen(str), h->body_arg);
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
  ImapMessage *msg = h->msg_cache[seqno-1];
  
  c = imap_get_atom(h->sio, buf, sizeof(buf));

  if(c!= -1) sio_ungetc(h->sio);
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

  body->media_basic_name = g_strdup (type);

  g_free(type);
  if(body) {
    body->media_basic = *imb;
    body->media_subtype = subtype;
  } else g_free(subtype);
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
      if(params) 
        g_hash_table_insert(params, key, val);
      else {
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
  /* if(desc) printf("body fld-desc=%s\n", desc); */
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

  if(!str) return IMR_PROTOCOL;

  if     (g_ascii_strcasecmp(str, "7BIT")==0)             enc = IMBENC_7BIT;
  else if(g_ascii_strcasecmp(str, "8BIT")==0)             enc = IMBENC_8BIT;
  else if(g_ascii_strcasecmp(str, "BINARY")==0)           enc = IMBENC_BINARY;
  else if(g_ascii_strcasecmp(str, "BASE64")==0)           enc = IMBENC_BASE64;
  else if(g_ascii_strcasecmp(str, "QUOTED-PRINTABLE")==0) enc = IMBENC_QUOTED;
  else enc = IMBENC_OTHER;
  if(body)
    body->encoding = enc;
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
	g_hash_table_new_full (g_str_hash, imap_str_case_equal, g_free,
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
    imap_get_nstring (sio);

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
      body->ext.mpart.params =
	g_hash_table_new_full (g_str_hash, imap_str_case_equal, g_free,
			       g_free);
      rc = ir_body_fld_param_hash (sio, body->ext.mpart.params);
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
      /* printf("Lines: %d\n", body->lines); */
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
ir_body_section(struct siobuf *sio, ImapFetchBodyCb body_cb, void *arg)
{
  char buf[12], *str;
  int c = imap_get_atom(sio, buf, sizeof(buf));
  if(c != ']') { puts("] expected"); return IMR_PROTOCOL; }
  if(sio_getc(sio) != ' ') { puts("space expected"); return IMR_PROTOCOL;}
  str = imap_get_nstring(sio);
  if(body_cb)
    body_cb(str, strlen(str), arg);
  g_free(str);
  return IMR_OK;
}

static ImapResponse
ir_msg_att_body(ImapMboxHandle *h, int c, unsigned seqno)
{
  ImapMessage *msg = h->msg_cache[seqno-1];
  ImapResponse rc;

  switch(c) {
  case '[': 
    rc = ir_body_section(h->sio, h->body_cb, h->body_arg);
    break;
  case ' ':
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
  ImapMessage *msg = h->msg_cache[seqno-1];
  ImapResponse rc;

  switch(c) {
  case ' ':
    rc = ir_body(h->sio, sio_getc(h->sio),
                 msg->body ? NULL : (msg->body = imap_body_new()), 
		 IMB_EXTENSIBLE);
    break;
  default: rc = IMR_PROTOCOL; break;
  }
  return rc;
}

static ImapResponse
ir_msg_att_body_header_fields(ImapMboxHandle *h, int c, unsigned seqno)
{
  ImapMessage *msg = h->msg_cache[seqno-1];

  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;

  while (imap_get_astring(h->sio, &c) && c != ')')
      /* nothing (yet?) */;
  if(c != ')') return IMR_PROTOCOL;
  if(sio_getc(h->sio) != ']') return IMR_PROTOCOL;
  if(sio_getc(h->sio) != ' ') return IMR_PROTOCOL;
  g_free(msg->fetched_header_fields);
  msg->fetched_header_fields = imap_get_nstring(h->sio);
  return IMR_OK;
}

static ImapResponse
ir_msg_att_uid(ImapMboxHandle *h, int c, unsigned seqno)
{
  char buf[12];
  ImapMessage *msg = h->msg_cache[seqno-1];
  c = imap_get_atom(h->sio, buf, sizeof(buf));

  if(c!= -1) sio_ungetc(h->sio);
  msg->uid = atoi(buf);
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
    { "BODY[",         ir_msg_att_body }, 
    { "BODY[HEADER.FIELDS", ir_msg_att_body_header_fields }, 
    { "BODYSTRUCTURE", ir_msg_att_bodystructure }, 
    { "UID",           ir_msg_att_uid }
  };
  char atom[LONG_STRING]; /* make sure LONG_STRING is longer than all */
                          /* strings above */
  unsigned i;
  int c;
  ImapResponse rc;

  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;
  if(h->msg_cache[seqno-1] == NULL)
    h->msg_cache[seqno-1] = imap_message_new();
  do {
    /* FIXME Can we ever match "BODY[" or "BODY[HEADER.FIELDS"? */
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
        return
          NumHandlers[i].handler(h, seqno);
      }
    }
  } else {
    if (c == 0x0d)
      sio_ungetc(h->sio);
    for(i=0; i<ELEMENTS(ResponseHandlers); i++) {
      if(g_ascii_strncasecmp(atom, ResponseHandlers[i].response, 
                             ResponseHandlers[i].keyword_len) == 0) {
	return
	  ResponseHandlers[i].handler(h);
      }
    }
  }
  return IMR_OK; /* FIXME: unknown response is really a failure */
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
   as well. We assume that the new messages fulfillthe filtering
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

static void
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
