
#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <glib.h>
#include <ctype.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gmime/gmime-utils.h> 
#include "libimap-marshal.h"
#include "imap.h"
#include "imap-handle.h"
#include "imap-auth.h"
#include "siobuf.h"
#include "util.h"

FILE *debug_stream = NULL;

#define LONG_STRING 512
static const int IMAP_CMD_DELTA = 128;
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

#include "imap_private.h"

struct _ImapMboxHandleClass {
  GObjectClass parent_class;
  /* Signal */
  void (*fetch_response)(ImapMboxHandle* handle);
  void (*list_response)(ImapMboxHandle* handle, int flags, char delim,
                        const gchar* mbox);
  void (*lsub_response)(ImapMboxHandle* handle, int flags, char delim,
                        const gchar* mbox);
  void (*search_response)(ImapMboxHandle* handle, guint32 nr);
  void (*expunge_notify)(ImapMboxHandle* handle);
  void (*exists_notify)(ImapMboxHandle* handle);
};

enum {
  FETCH_RESPONSE,
  LIST_RESPONSE,
  LSUB_RESPONSE,
  SEARCH_RESPONSE,
  EXPUNGE_NOTIFY,
  EXISTS_NOTIFY,
  LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;
static guint imap_mbox_handle_signals[LAST_SIGNAL] = { 0 };

static void imap_mbox_handle_init(ImapMboxHandle *handle);
static void imap_mbox_handle_class_init(ImapMboxHandleClass * klass);
static void imap_mbox_handle_finalize(GObject* handle);

static ImapResponse imap_mbox_connect(ImapMboxHandle* handle);

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
  handle->port   = 143;
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
                 libimap_VOID__INT_CHAR_POINTER, G_TYPE_NONE, 3,
                 G_TYPE_INT, G_TYPE_CHAR, G_TYPE_POINTER);
  imap_mbox_handle_signals[LSUB_RESPONSE] = 
    g_signal_new("lsub-response",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 G_STRUCT_OFFSET(ImapMboxHandleClass, lsub_response),
                 NULL, NULL,
                 libimap_VOID__INT_CHAR_POINTER, G_TYPE_NONE, 3,
                 G_TYPE_INT, G_TYPE_CHAR, G_TYPE_POINTER);
  imap_mbox_handle_signals[SEARCH_RESPONSE] = 
    g_signal_new("search-response",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 G_STRUCT_OFFSET(ImapMboxHandleClass, search_response),
                 NULL, NULL,
		 g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1,
		 G_TYPE_UINT);


  imap_mbox_handle_signals[EXPUNGE_NOTIFY] = 
    g_signal_new("expunge-notify",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_FIRST,
                 G_STRUCT_OFFSET(ImapMboxHandleClass, expunge_notify),
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

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

ImapResult
imap_mbox_handle_connect(ImapMboxHandle* ret, const char *host, int port, 
                         const char* user, const char* passwd, 
                         const char *mbox)
{
  ImapResult rc;
  g_free(ret->host);   ret->host   = g_strdup(host);
  g_free(ret->user);   ret->user   = g_strdup(user);
  g_free(ret->passwd); ret->passwd = g_strdup(passwd);

  if( (rc=imap_mbox_connect(ret)) != IMAP_SUCCESS)
    return rc;

  if( (rc = imap_authenticate(ret, user, passwd)) != IMAP_SUCCESS)
    return rc;

  if(mbox) rc = imap_mbox_select(ret, mbox, NULL);
  return rc;
}

static int
socket_open(const char* host, int iport)
{
  static const int USEIPV6 = 0;
  int rc, fd = -1;
  
  /* --- IPv4/6 --- */
  /* "65536\0" */
  char port[6];
  struct addrinfo hints;
  struct addrinfo* res;
  struct addrinfo* cur;
  
  /* we accept v4 or v6 STREAM sockets */
  memset (&hints, 0, sizeof (hints));

  hints.ai_family = USEIPV6 ? AF_UNSPEC : AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  snprintf (port, sizeof (port), "%d", iport);

  rc = getaddrinfo(host, port, &hints, &res);
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
  handle->sd = socket_open(handle->host, handle->port);
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
  
  if (imap_mbox_handle_can_do(handle, IMCAP_STARTTLS))
    printf("Server is TLS capable.\n");
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
  return handle->exists;
}

GNode *imap_mbox_handle_get_thread_root(ImapMboxHandle* handle)
{
  g_return_val_if_fail(handle, NULL);
  return handle->thread_root;
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
}


ImapMessage*
imap_mbox_handle_get_msg(ImapMboxHandle* h, unsigned no)
{
  no--;
  g_return_val_if_fail(no<h->exists, NULL);
  return h->msg_cache[no];
}
unsigned
imap_mbox_handle_first_unseen(ImapMboxHandle* handle)
{
  /* might do search here. */
  return handle->unseen;
}

ImapEnvelope*
imap_envelope_new()
{
  return g_malloc0(sizeof(ImapEnvelope));
}
static void
imap_envelope_free_data(ImapEnvelope* env)
{
  g_free(env->subject);
  g_free(env->from);
  g_free(env->sender);
  g_free(env->replyto);

  g_list_foreach(env->to_list, (GFunc)g_free, NULL);
  g_list_free(env->to_list);

  g_list_foreach(env->cc_list, (GFunc)g_free, NULL);
  g_list_free(env->cc_list);

  g_list_foreach(env->bcc_list, (GFunc)g_free, NULL);
  g_list_free(env->bcc_list);

  g_free(env->in_reply_to);
  g_free(env->message_id);
}
void
imap_envelope_free(ImapEnvelope *env)
{
  g_return_if_fail(env);
  imap_envelope_free_data(env);
  g_free(env);
}

ImapMessage*
imap_message_new()
{
  return g_malloc0(sizeof(ImapMessage));
}

void
imap_message_free(ImapMessage *msg)
{
  g_return_if_fail(msg);
  if(msg->envelope) imap_envelope_free(msg->envelope);
  g_free(msg->body);
  g_free(msg->fetched_header_fields);
  g_free(msg);
}


/* =================================================================== */
/*                Imap command processing routines                     */
/* =================================================================== */
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
  sio_flush(handle->sio);
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
  int i;
  /* FIXME: sanity test */
  g_return_val_if_fail(handle, IMR_BAD);
  g_return_val_if_fail(handle->state != IMHS_DISCONNECTED, IMR_BAD);

  i = imap_cmd_get_tag(handle->sio, tag, sizeof(tag));
  if (i<0) {
      /* handle errors */
      return IMR_SEVERED;
  }
  /* handle untagged messages. The caller still gets its shot afterwards. */
    if (strcmp(tag, "*") == 0) {
      if(ir_handle_response(handle)!=IMR_OK) puts("cmd_step protocol error");
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
  int rc = 0;
  g_return_val_if_fail(conn, -1);

  sio_write(conn->sio, buf, len); /* why it is void? */
  sio_flush(conn->sio);
  return rc;
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
    res = str->str; g_string_free(str, FALSE);
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
#if NOT_USED
static char*
imap_get_string(struct siobuf* sio)
{
  return imap_get_string_with_lookahead(sio, sio_getc(sio));
}
#endif

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
    res = str->str; g_string_free(str, FALSE);
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
      if (strcasecmp(atom, capabilities[x]) == 0) {
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
    "READ-ONLY", "READ-WRITE", "TRYCREATE", "UIDNEXT", "UIDVALIDITY", "UNSEEN"
  };
  unsigned o;
  char buf[128];
  int c = imap_get_atom(h->sio, buf, sizeof(buf));
  for(o=0; o<ELEMENTS(resp_text_code); o++)
    if(strcasecmp(buf, resp_text_code[o]) == 0) break;
  switch(o) {
  case 0: if(h->alert_cb) h->alert_cb("ALERT", h->alert_arg);/* FIXME*/break;
  case 1: ignore_bad_charset(h->sio, c); break;
  case 2: ir_capability_data(h); break;
  case 3: /* ignore parse */; break;
  case 4: ir_permanent_flags(h); break;
  case 5: h->readonly_mbox = TRUE; /* read-only */ break;
  case 6: h->readonly_mbox = FALSE; /* read-write */ break;
  case 7: /* ignore try-create */; break;
  case 8: imap_get_atom(h->sio, buf, sizeof(buf)); h->uidnext=atoi(buf); break;
  case 9: imap_get_atom(h->sio, buf, sizeof(buf)); h->uidval =atoi(buf); break;
  case 10:imap_get_atom(h->sio, buf, sizeof(buf)); h->unseen =atoi(buf); break;
  default: while( (c=sio_getc(h->sio)) != EOF && c != ']') ; break;
  }
  return IMR_OK;
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
  if( (l=strlen(line))>0) { 
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
  return IMR_OK;
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
  return IMR_OK;
}

static ImapResponse
ir_preauth(ImapMboxHandle *h)
{
  if(imap_mbox_handle_get_state(h) == IMHS_CONNECTED)
    imap_mbox_handle_set_state(h, IMHS_AUTHENTICATED);
  return IMR_OK;
}

static ImapResponse
ir_bye(ImapMboxHandle *h)
{
  int c; while( (c=sio_getc(h->sio))!=EOF && c != 0x0a);
  if(!h->doing_logout) /* it is not we, so it must be the server */
    imap_mbox_handle_set_state(h, IMHS_DISCONNECTED);
  /* we close the connection here unless we are doing logout. */
  return IMR_OK;
}


static ImapResponse
ir_capability(ImapMboxHandle *handle)
{
  int c = ir_capability_data(handle);
  printf("ir_capability, c='%c'[%d]\n", c, c);
  return (c == 0x0d && sio_getc(handle->sio) == 0x0a) ? IMR_OK : IMR_PROTOCOL;
}
/* follow mailbox-list syntax (See rfc) */
static ImapResponse
ir_list_lsub(ImapMboxHandle *h, const char *signal_name)
{
  static struct { const char *name; int len; } mbx_flags[] = 
    { {"Noinferiors",11}, {"Noselect",8}, {"Marked",6}, {"Unmarked",8} };
  int flags=0;
  char buf[LONG_STRING], *s;
  char delim;
  int c;

  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;

  /* [mbx-list-flags] */
  c=sio_getc(h->sio);
  while(c != ')') {
    unsigned i;
    if(c!= '\\') return IMR_PROTOCOL;
    c = imap_get_atom(h->sio, buf, sizeof(buf));
    for(i=0; i< ELEMENTS(mbx_flags); i++) {
      if(strncasecmp(buf, mbx_flags[i].name, mbx_flags[i].len) ==0) {
        flags |= 1<<i;
        break;
      }
    }
    if( c != ' ' && c != ')') return IMR_PROTOCOL;
    if(c==' ') c = sio_getc(h->sio);
  }
  if(sio_getc(h->sio) != ' ') return IMR_PROTOCOL;
  c=sio_getc(h->sio);
  if(c == '"') {
      delim = sio_getc(h->sio);
      if(sio_getc(h->sio) != '"') return IMR_PROTOCOL;
  } else {
    if(c != 'N' ||
       sio_getc(h->sio) != 'I' ||
       sio_getc(h->sio) != 'L') return IMR_PROTOCOL;
    delim = '\0';
  }
  if(sio_getc(h->sio) != ' ') return IMR_PROTOCOL;
  /* mailbox */
  s = imap_get_astring(h->sio, &c);
  g_signal_emit_by_name(G_OBJECT(h), signal_name, flags, delim, s);
  g_free(s);
  if( c != 0x0d) {printf("CR:%d\n",c); return IMR_PROTOCOL;}
  if( (c=sio_getc(h->sio)) != 0x0a) {printf("LF:%d\n",c); return IMR_PROTOCOL;}
  return IMR_OK;
}

static ImapResponse
ir_list(ImapMboxHandle *h)
{
  return ir_list_lsub(h, "list-response");
}

static ImapResponse
ir_lsub(ImapMboxHandle *h)
{
  return ir_list_lsub(h, "lsub-response");
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
  char buf[12];
  int c;
  gint32 uid;

  do {
      c = imap_get_atom(h->sio, buf, sizeof(buf));
      if (c == EOF) 
	  return IMR_PROTOCOL;

      uid = atoi(buf);
      g_signal_emit_by_name(G_OBJECT(h), "search-response", uid);
  } while (c != 0x0d);
  if( (c=sio_getc(h->sio)) != 0x0a) {printf("LF:%d\n",c); return IMR_PROTOCOL;}
  return IMR_OK;
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
  int c;
  unsigned i;

  while( (c=sio_getc(h->sio))!=EOF && c != 0x0a);
  if(seqno<h->exists) { /* shrink msg_cache */
    for(i=seqno; i<h->exists; i++) {
      if(h->msg_cache[i])
        imap_message_free(h->msg_cache[i]);
    }
  }
  h->msg_cache = g_realloc(h->msg_cache, seqno*sizeof(ImapMessage*));
  for(i=h->exists; i<seqno; i++) 
    h->msg_cache[i] = NULL;
  h->exists = seqno;
  /* FIXME: send a signal here! */
  return IMR_OK;
}

static ImapResponse
ir_recent(ImapMboxHandle *h, unsigned seqno)
{
  int c; while( (c=sio_getc(h->sio))!=EOF && c != 0x0a);
  h->recent = seqno;
  /* FIXME: send a signal here! */
  return IMR_OK;
}

static ImapResponse
ir_expunge(ImapMboxHandle *h, unsigned seqno)
{
  int c; while( (c=sio_getc(h->sio))!=EOF && c != 0x0a);
  /* FIXME: implement! */
  return IMR_OK;
}

const char* msg_flags[] = { 
  "seen", "answered", "flagged", "deleted", "draft", "recent"
};

static ImapResponse
ir_msg_att_flags(ImapMboxHandle *h, unsigned seqno)
{
  int c;
  unsigned i;
  ImapMessage *msg = h->msg_cache[seqno-1];

  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;
  for(i=0; i<ELEMENTS(msg->flags); i++)
    msg->flags[i] = FALSE;

  do {
    char buf[LONG_STRING];
    c = imap_get_flag(h->sio, buf, sizeof(buf));
    for(i=0; i<ELEMENTS(msg_flags); i++)
      if(buf[0] == '\\' && strcasecmp(msg_flags[i], buf+1) == 0) {
        msg->flags[i] = TRUE;
        break;
      }
  } while(c!=-1 && c != ')');

  if(h->flags_cb)
    h->flags_cb(seqno, h->flags_arg);
  return IMR_OK;
}

/* imap_get_address: returns null if no beginning of address is found
   (eg., when end of list is found instead).
*/
static ImapAddress*
imap_get_address(struct siobuf* sio)
{
  ImapAddress *address;
  int c;

  if(sio_getc(sio) != '(') return NULL;
  
  address = g_new0(ImapAddress, 1);

  address->addr_name = imap_get_nstring(sio);
  if( (c=sio_getc(sio)) != ' '); /* error but do nothing */
  address->addr_adl = imap_get_nstring(sio);
  if( (c=sio_getc(sio)) != ' '); /* error but do nothing */
  address->addr_mailbox = imap_get_nstring(sio);
  if( (c=sio_getc(sio)) != ' '); /* error but do nothing */
  address->addr_host = imap_get_nstring(sio);
  if( (c=sio_getc(sio)) != ')'); /* error but do nothing */

  return address;
}
static GList*
imap_get_addr_list(struct siobuf *sio)
{
  GList *list=NULL;
  ImapAddress *addr;

  int c=sio_getc(sio);
  if( c == '(') {
    while( (addr=imap_get_address(sio)) != NULL) {
      list = g_list_prepend(list, addr);
    }
    /* assert(c==')'); */
  } else { /* nil; FIXME: error checking */
    sio_getc(sio); /* i */
    sio_getc(sio); /* l */
  }
  return g_list_reverse(list);
}

static ImapResponse
ir_msg_att_envelope(ImapMboxHandle *h, unsigned seqno)
{
  int c;
  char *date;
  ImapMessage *msg = h->msg_cache[seqno-1];
  GList *address_list;

  if( (c=sio_getc(h->sio)) != '(') return IMR_PROTOCOL;
  if(!msg->envelope)
    msg->envelope = imap_envelope_new();
  else
    imap_envelope_free_data(msg->envelope);
  date = imap_get_nstring(h->sio);
  if(date) {
      msg->envelope->date = g_mime_utils_header_decode_date(date, NULL);
      g_free(date);
  }
  if( (c=sio_getc(h->sio)) != ' ') return IMR_PROTOCOL;

  msg->envelope->subject = imap_get_nstring(h->sio);
  if( (c=sio_getc(h->sio)) != ' ') return IMR_PROTOCOL;

  address_list = imap_get_addr_list(h->sio);
  msg->envelope->from = address_list->data;
  address_list = g_list_delete_link(address_list, address_list);
  g_list_foreach(address_list, (GFunc)g_free, NULL);
  g_list_free(address_list);
  if( (c=sio_getc(h->sio)) != ' ') return IMR_PROTOCOL;

  address_list = imap_get_addr_list(h->sio);
  msg->envelope->sender = address_list->data;
  address_list = g_list_delete_link(address_list, address_list);
  g_list_foreach(address_list, (GFunc)g_free, NULL);
  g_list_free(address_list);
  if( (c=sio_getc(h->sio)) != ' ') return IMR_PROTOCOL;

  address_list = imap_get_addr_list(h->sio);
  msg->envelope->replyto = address_list->data;
  address_list = g_list_delete_link(address_list, address_list);
  g_list_foreach(address_list, (GFunc)g_free, NULL);
  g_list_free(address_list);
  if( (c=sio_getc(h->sio)) != ' ') return IMR_PROTOCOL;

  msg->envelope->to_list = imap_get_addr_list(h->sio);
  if( (c=sio_getc(h->sio)) != ' ') return IMR_PROTOCOL;

  msg->envelope->cc_list = imap_get_addr_list(h->sio);
  if( (c=sio_getc(h->sio)) != ' ') return IMR_PROTOCOL;

  msg->envelope->bcc_list = imap_get_addr_list(h->sio);
  if( (c=sio_getc(h->sio)) != ' ') return IMR_PROTOCOL;

  msg->envelope->in_reply_to = imap_get_nstring(h->sio);
  if( (c=sio_getc(h->sio)) != ' ') { printf("c=%c\n",c); return IMR_PROTOCOL;}

  msg->envelope->message_id = imap_get_nstring(h->sio);
  if( (c=sio_getc(h->sio)) != ')') { printf("c=%d\n",c);return IMR_PROTOCOL;}

  return IMR_OK;
}

static ImapResponse
ir_msg_att_internaldate(ImapMboxHandle *h, unsigned seqno)
{
  return IMR_OK;
}
static ImapResponse
ir_msg_att_rfc822(ImapMboxHandle *h, unsigned seqno)
{
  return IMR_OK;
}
static ImapResponse
ir_msg_att_rfc822_header(ImapMboxHandle *h, unsigned seqno)
{
  return IMR_OK;
}
static ImapResponse
ir_msg_att_rfc822_text(ImapMboxHandle *h, unsigned seqno)
{
  return IMR_OK;
}
static ImapResponse
ir_msg_att_rfc822_size(ImapMboxHandle *h, unsigned seqno)
{
  return IMR_OK;
}
static ImapResponse
ir_msg_att_body(ImapMboxHandle *h, unsigned seqno)
{
  int c;
  ImapMessage *msg = h->msg_cache[seqno-1];

  printf("%d", seqno);
  if((c=sio_getc(h->sio)) == '[') {
	  printf("%c", c);
	  while((c=sio_getc(h->sio)) != ']')
	  printf("%c", c);
		  ;
  }
  printf("%c<\n", c);
  g_free(msg->body);
  msg->body = imap_get_nstring(h->sio);
  return IMR_OK;
}
static ImapResponse
ir_msg_att_body_header_fields(ImapMboxHandle *h, unsigned seqno)
{
  int c;
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
ir_msg_att_uid(ImapMboxHandle *h, unsigned seqno)
{
  char buf[12];
  ImapMessage *msg = h->msg_cache[seqno-1];
  int c = imap_get_atom(h->sio, buf, sizeof(buf));

  if(c!= -1) sio_ungetc(h->sio);
  msg->uid = atoi(buf);
  return IMR_OK;
}

static ImapResponse
ir_fetch_seq(ImapMboxHandle *h, unsigned seqno)
{
  static const struct {
    const gchar* name;
    ImapResponse (*handler)(ImapMboxHandle *h, unsigned seqno);
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
    { "BODY[HEADER.FIELDS",         ir_msg_att_body_header_fields }, 
    { "UID",           ir_msg_att_uid }
  };
  char atom[LONG_STRING];
  unsigned i;
  int c;
  ImapResponse rc;

  if(sio_getc(h->sio) != '(') return IMR_PROTOCOL;
  if(h->msg_cache[seqno-1] == NULL)
    h->msg_cache[seqno-1] = imap_message_new();
  do {
    imap_get_atom(h->sio, atom, sizeof(atom));
    for(i=0; i<ELEMENTS(msg_att); i++) {
      if(strcasecmp(atom, msg_att[i].name) == 0) {
        if( (rc=msg_att[i].handler(h, seqno)) != IMR_OK)
          return rc;
      }
    }
    c=sio_getc(h->sio);
  } while( c!= EOF && c == ' ');
  if(c!=')') return IMR_PROTOCOL;
  if( (c=sio_getc(h->sio)) != 0x0d) return IMR_PROTOCOL;
  if( (c=sio_getc(h->sio)) != 0x0a) return IMR_PROTOCOL;
  return IMR_OK;
}

static ImapResponse
ir_fetch(ImapMboxHandle *h)
{
  char buf[12];
  unsigned seqno;
  int i;

  i = imap_get_atom(h->sio, buf, sizeof(buf));
  seqno = atoi(buf);
  if(seqno == 0) return IMAP_PROTOCOL_ERROR;
  if(i != ' ') return IMAP_PROTOCOL_ERROR;
  return ir_fetch_seq(h, seqno);
}

/*
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
  if(seqno == 0) return IMAP_PROTOCOL_ERROR;
  printf("%d ", seqno);
  item = g_node_append_data(parent, GUINT_TO_POINTER(seqno));
  if (c == ' ') {
#if DEBUG
      printf(">");
#endif
      rc = ir_thread_sub(h, item, c);
#if DEBUG
      printf("<");
#endif
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
  g_node_destroy(h->thread_root);
  h->thread_root = NULL;
  root = g_node_new(NULL);
  while (c == '(') {
      rc=ir_thread_sub(h, root, c);
      if (rc!=IMR_OK) {
	  return rc;
      }
      printf("\n");
      c=sio_getc(h->sio);
  }
  if( c != 0x0d) {printf("CR:%d\n",c); rc = IMR_PROTOCOL;}
  if( (c=sio_getc(h->sio)) != 0x0a) {printf("LF:%d\n",c); rc = IMR_PROTOCOL;}

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
  { "FLAGS",      5, ir_flags },
  { "FETCH",      5, ir_fetch },
  { "THREAD",     6, ir_thread },
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
  char atom[LONG_STRING];
  unsigned i, seqno;

  imap_get_atom(h->sio, atom, sizeof(atom));
  if( isdigit(atom[0]) ) {
    seqno = atoi(atom);
    imap_get_atom(h->sio, atom, sizeof(atom));
    for(i=0; i<ELEMENTS(NumHandlers); i++) {
      if(strncasecmp(atom, NumHandlers[i].response, 
                     NumHandlers[i].keyword_len) == 0) {
        return
          NumHandlers[i].handler(h, seqno);
      }
    }
  } else {
    for(i=0; i<ELEMENTS(ResponseHandlers); i++) {
      if(strncasecmp(atom, ResponseHandlers[i].response, 
                     ResponseHandlers[i].keyword_len) == 0) {
        return
          ResponseHandlers[i].handler(h);
      }
    }
  }
  printf("Unhandled response: %s\n", atom);
  return IMR_OK; /* FIXME: unknown response is really a failure */
}
