#ifndef __MBOX_HANDLE_H__
#define __MBOX_HANDLE_H__

#include <glib.h>

#include "imap.h"

typedef enum {
  IM_EVENT_EXISTS,
  IM_EVENT_EXPUNGE
} ImapMboxEventType;

/* connection states, as defined in rfc-2060, 3 */
typedef enum {
  IMHS_DISCONNECTED,
  IMHS_CONNECTED, /* non authenticated */
  IMHS_AUTHENTICATED,
  IMHS_SELECTED
} ImapMboxHandleState;

int imap_mbox_is_disconnected (ImapMboxHandle *h);
int imap_mbox_is_connected    (ImapMboxHandle *h);
int imap_mbox_is_authenticated(ImapMboxHandle *h);
int imap_mbox_is_selected     (ImapMboxHandle *h);

/* imap server responses, as defined in rfc-2060 */
typedef enum {
  IMR_SEVERED,  /* OK, this is a connection problem */
  IMR_BYE,      /* unexpected but clean bye from server */
  IMR_UNTAGGED, /* this is seen only in low-level: "*" */
  IMR_RESPOND,  /* response is expected: "+" */
  IMR_PROTOCOL, /* unexpected protocol error */
  IMR_OK,
  IMR_NO,
  IMR_BAD,
  IMR_PARSE     /* inofficial: server had problems parsing one of    */
                /* the messages. Ignore is probably the only action. */
} ImapResponse;

typedef char ImapCmdTag[7]; /* Imap command tag */

/* Capabilities recognized by the library. Add new ones alphabetically. */
typedef enum
{
  IMCAP_IMAP4 = 0,
  IMCAP_IMAP4REV1,
  IMCAP_STATUS,
  IMCAP_ACL,			/* RFC 2086: IMAP4 ACL extension */
  IMCAP_NAMESPACE,              /* RFC 2342: IMAP4 Namespace */
  IMCAP_ACRAM_MD5,		/* RFC 2195: CRAM-MD5 authentication */
  IMCAP_AGSSAPI,		/* RFC 1731: GSSAPI authentication */
  IMCAP_AUTH_ANON, 	        /*           AUTH=ANONYMOUS */
  IMCAP_STARTTLS,		/* RFC 2595: STARTTLS */
  IMCAP_LOGINDISABLED,		/*           LOGINDISABLED */
  IMCAP_SORT,                   /* SORT and thread described at: */
  /* http://www.ietf.org/internet-drafts/draft-ietf-imapext-sort-13.txt */
  IMCAP_THREAD_ORDEREDSUBJECT,
  IMCAP_THREAD_REFERENCES,
  IMCAP_MAX
} ImapCapability;

typedef enum {
  IMFETCH_ENV        = 1<<0,
  IMFETCH_BODYSTRUCT = 1<<1
} ImapFetchType;

typedef struct _ImapMboxHandleClass ImapMboxHandleClass;

typedef void (*LBIResponseCallback)(ImapMboxHandle *h, const char* response,
                                    void *data);

typedef struct {
  const gchar* response;
  LBIResponseCallback cb;
  void *cb_data;
} LBIResponseHandler;

typedef void (*ImapMboxNotifyCb)(ImapMboxHandle*handle, ImapMboxEventType ev, 
                                 int seqno, void* data);

typedef void (*ImapInfoCb)(const char *buffer, void *arg);
typedef void (*ImapMonitorCb)(const char *buffer, int length, int direction,
                              void *arg);

typedef void (*ImapFlagsCb)(unsigned seqno, void *arg);
typedef void (*ImapSearchCb)(ImapMboxHandle*handle, unsigned seqno, void *arg);
typedef void(*ImapListCb)(ImapMboxHandle*handle, int delim,
                          const char* mbox, gboolean *flags, void*);


ImapMboxHandle *imap_mbox_handle_new(void);
void imap_handle_set_monitorcb(ImapMboxHandle* h, ImapMonitorCb cb, void*);
void imap_handle_set_infocb(ImapMboxHandle* h, ImapInfoCb cb, void*);
void imap_handle_set_alertcb(ImapMboxHandle* h, ImapInfoCb cb, void*);
void imap_handle_set_flagscb(ImapMboxHandle* h, ImapFlagsCb cb, void*);

ImapResult imap_mbox_handle_connect(ImapMboxHandle* r, const char *hst,
                                    const char* user, const char* passwd);
ImapResponse imap_mbox_handle_reconnect(ImapMboxHandle* r,
                                        gboolean *readonly);

/* int below is a boolean */
int      imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap);
unsigned imap_mbox_handle_get_exists(ImapMboxHandle* handle);
unsigned imap_mbox_handle_get_validity(ImapMboxHandle* handle);
int      imap_mbox_handle_get_delim(ImapMboxHandle* handle,
                                    const char *namespace);

void imap_mbox_handle_connect_notify(ImapMboxHandle* handle,
                                     ImapMboxNotifyCb cb,
                                     void *data);

void imap_mbox_handle_destroy(ImapMboxHandle* handle);

ImapResponse imap_mbox_handle_fetch(ImapMboxHandle* handle, const gchar *seq, 
                                    const gchar* headers[]);
ImapResponse imap_mbox_handle_fetch_env(ImapMboxHandle* handle,
                                        const gchar *seq);
ImapResponse imap_mbox_handle_fetch_range(ImapMboxHandle* handle,
                                          unsigned lo, unsigned hi,
                                          ImapFetchType ift);

typedef void (*ImapFetchBodyCb)(const char *buf, int buflen, void* arg);
ImapResponse imap_mbox_handle_fetch_body(ImapMboxHandle* handle, 
                                         unsigned seqno, 
                                         unsigned partno,
                                         ImapFetchBodyCb body_handler,
                                         void *arg);

ImapResponse imap_mbox_handle_fetch_structure(ImapMboxHandle* handle,
                                              const gchar *seq);

ImapMessage* imap_mbox_handle_get_msg(ImapMboxHandle* handle, unsigned seqno);
ImapMessage* imap_mbox_handle_get_msg_v(ImapMboxHandle* handle, unsigned no);
unsigned imap_mbox_get_msg_no(ImapMboxHandle* h, unsigned no);
unsigned imap_mbox_get_rev_no(ImapMboxHandle* h, unsigned seqno);

unsigned imap_mbox_handle_first_unseen(ImapMboxHandle* handle);
unsigned imap_mbox_find_next(ImapMboxHandle* handle, unsigned start, 
                             const char *search_str);
ImapResponse imap_mbox_find_all(ImapMboxHandle *h, const char *search_str,
                                unsigned *msgcnt, unsigned**msgs);

unsigned imap_mbox_set_view(ImapMboxHandle *h, ImapMsgFlag f, gboolean state);
unsigned imap_mbox_set_sort(ImapMboxHandle *h, ImapSortOrder isr);
const char *imap_mbox_get_filter(ImapMboxHandle *h);
/* get_thread_root is deprecated */
GNode *imap_mbox_handle_get_thread_root(ImapMboxHandle* handle); 

/* ================ BEGIN OF MBOX_VIEW FUNCTIONS ======================= */
typedef struct _MboxView MboxView;
void mbox_view_init(MboxView *mv);
void mbox_view_resize(MboxView *mv, unsigned old_sz, unsigned new_sz);
void mbox_view_expunge(MboxView *mv, unsigned seqno);
void mbox_view_dispose(MboxView *mv);
gboolean mbox_view_is_active(MboxView *mv);
unsigned mbox_view_cnt(MboxView *mv);
unsigned mbox_view_get_msg_no(MboxView *mv, unsigned msgno);
unsigned mbox_view_get_rev_no(MboxView *mv, unsigned seqno);
const char *mbox_view_get_str(MboxView *mv);
/* ================ END OF MBOX_VIEW FUNCTIONS ========================= */

/* possibly private functions... */
ImapMboxHandleState imap_mbox_handle_get_state(ImapMboxHandle *h);
void imap_mbox_handle_set_state(ImapMboxHandle *h,
                                ImapMboxHandleState newstate);

ImapResponse imap_cmd_exec(ImapMboxHandle* handle, const char* cmd);
char* imap_mbox_gets(ImapMboxHandle *h, char* buf, size_t sz);

/* even more private functions */
int imap_cmd_start(ImapMboxHandle* handle, const char* cmd, char* tag);
ImapResponse imap_cmd_step(ImapMboxHandle* handle, const char* tag);
int imap_handle_write(ImapMboxHandle *conn, const char *buf, size_t len);
void imap_handle_flush(ImapMboxHandle *handle);

#endif
