#ifndef __MBOX_HANDLE_H__
#define __MBOX_HANDLE_H__
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

#include <glib.h>

#include "net-client.h"
#include "net-client-utils.h"
#include "libimap.h"

typedef enum {
  IM_EVENT_EXISTS,
  IM_EVENT_EXPUNGE
} ImapMboxEventType;

int imap_mbox_is_disconnected (ImapMboxHandle *h);
int imap_mbox_is_connected    (ImapMboxHandle *h);
int imap_mbox_is_authenticated(ImapMboxHandle *h);
int imap_mbox_is_selected     (ImapMboxHandle *h);

/* imap server responses, as defined in rfc-2060 */
typedef enum {
  IMR_UNKNOWN = 1, /* unknown, no response received yet, must be non-zero. */
  IMR_SEVERED,  /* OK, this is a connection problem */
  IMR_BYE,      /* unexpected but clean bye from server */
  IMR_UNTAGGED, /* this is seen only in low-level: "*" */
  IMR_RESPOND,  /* response is expected: "+" */
  IMR_PROTOCOL, /* unexpected protocol error */
  IMR_OK,
  IMR_NO,
  IMR_BAD,
  IMR_ALERT,
  IMR_PARSE     /* inofficial: server had problems parsing one of    */
                /* the messages. Ignore is probably the only action. */
} ImapResponse;

typedef char ImapCmdTag[7]; /* Imap command tag */

/* Capabilities recognized by the library. Add new ones alphabetically
 * and modify ir_capability_data() accordingly. */
typedef enum
{
  IMCAP_IMAP4 = 0,
  IMCAP_IMAP4REV1,              /* RFC 3501: Internet Message Access Protocol */
  IMCAP_STATUS,
  IMCAP_AANONYMOUS, 	        /* RFC 2245: AUTH=ANONYMOUS */
  IMCAP_ACRAM_MD5,		/* RFC 2195: CRAM-MD5 authentication */
  IMCAP_AGSSAPI,		/* RFC 1731: GSSAPI authentication */
  IMCAP_APLAIN,                 /* RFC 2595: */
  IMCAP_ACL,			/* RFC 2086: IMAP4 ACL extension */
  IMCAP_RIGHTS,                 /* RFC 4314: IMAP4 RIGHTS= extension */
  IMCAP_BINARY,                 /* RFC 3516 */
  IMCAP_CHILDREN,               /* RFC 3348 */
  IMCAP_COMPRESS_DEFLATE,       /* RFC 4978 */
  IMCAP_ESEARCH,                /* RFC 4731 */
  IMCAP_IDLE,                   /* RFC 2177 */
  IMCAP_LITERAL,                /* RFC 2088 */
  IMCAP_LOGINDISABLED,		/* RFC 2595 */
  IMCAP_MULTIAPPEND,            /* RFC 3502 */
  IMCAP_NAMESPACE,              /* RFC 2342: IMAP4 Namespace */
  IMCAP_QUOTA,                  /* RFC 2087 */
  IMCAP_SASLIR,                 /* RFC 4959 */
  IMCAP_SCAN,                   /* FIXME: RFC? */
  IMCAP_STARTTLS,		/* RFC 2595: STARTTLS */
  IMCAP_SORT,                   /* SORT and THREAD described at: */
  IMCAP_THREAD_ORDEREDSUBJECT,  /* RFC 5256 */
  IMCAP_THREAD_REFERENCES,
  IMCAP_UIDPLUS,                /* RFC 4315 */
  IMCAP_UNSELECT,               /* RFC 3691 */
  IMCAP_FETCHBODY,              /* basic imap implemented correctly by
                                 * most imap servers but not all. We
                                 * have to detect that. */
  IMCAP_MAX
} ImapCapability;

typedef enum {
  IMAP_OPT_CLIENT_SORT, /**< allow client-side sorting */
  IMAP_OPT_BINARY,      /**< enable binary=no-transfer-encoding msg transfer */
  IMAP_OPT_IDLE,        /**< enable IDLE */
  IMAP_OPT_COMPRESS,    /**< enable COMPRESS */
}  ImapOption;

typedef void (*LBIResponseCallback)(ImapMboxHandle *h, const char* response,
                                    void *data);

typedef struct {
  const gchar* response;
  LBIResponseCallback cb;
  void *cb_data;
} LBIResponseHandler;

typedef void (*ImapMboxNotifyCb)(ImapMboxHandle*handle, ImapMboxEventType ev, 
                                 int seqno, void* data);

typedef void (*ImapInfoCb)(ImapMboxHandle *h, ImapResponse rc,
                           const char *buffer, void *arg);
typedef void (*ImapMonitorCb)(const char *buffer, int length, int direction,
                              void *arg);

typedef void (*ImapFlagsCb)(unsigned cnt, const unsigned seqno[], void *arg);
typedef void (*ImapSearchCb)(ImapMboxHandle*handle, unsigned seqno, void *arg);
typedef void(*ImapListCb)(ImapMboxHandle*handle, int delim,
                          const char* mbox, gboolean *flags, void*);


ImapMboxHandle *imap_mbox_handle_new(void);
void imap_handle_set_option(ImapMboxHandle *h, ImapOption opt, gboolean state);
void imap_handle_set_infocb(ImapMboxHandle* h, ImapInfoCb cb, void*);
void imap_handle_set_flagscb(ImapMboxHandle* h, ImapFlagsCb cb, void*);
void imap_handle_set_authcb(ImapMboxHandle* h, GCallback cb, void *arg);
void imap_handle_set_certcb(ImapMboxHandle* h, GCallback cb);
int imap_handle_set_timeout(ImapMboxHandle *, int milliseconds);
gboolean imap_handle_idle_enable(ImapMboxHandle *, int seconds);
gboolean imap_handle_idle_disable(ImapMboxHandle *)
    __attribute__ ((warn_unused_result));
gboolean imap_handle_op_cancelled(ImapMboxHandle *h);
ImapResult imap_mbox_handle_connect(ImapMboxHandle* r, const char *hst);
ImapResult imap_mbox_handle_reconnect(ImapMboxHandle* r,
                                      gboolean *readonly);
void imap_handle_force_disconnect(ImapMboxHandle *h);

NetClientCryptMode imap_handle_set_tls_mode(ImapMboxHandle *h, NetClientCryptMode option);
NetClientAuthMode imap_handle_set_auth_mode(ImapMboxHandle *h, NetClientAuthMode mode);

/* int below is a boolean */
int      imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap);
unsigned imap_mbox_handle_get_exists(ImapMboxHandle* handle);
unsigned imap_mbox_handle_get_validity(ImapMboxHandle* handle);
unsigned imap_mbox_handle_get_uidnext(ImapMboxHandle* handle);
int      imap_mbox_handle_get_delim(ImapMboxHandle* handle,
                                    const char *namespace);
char* imap_mbox_handle_get_last_msg(ImapMboxHandle *handle);

void imap_mbox_handle_connect_notify(ImapMboxHandle* handle,
                                     ImapMboxNotifyCb cb,
                                     void *data);

void imap_mbox_handle_destroy(ImapMboxHandle* handle);

ImapResponse imap_mbox_handle_fetch_unlocked(ImapMboxHandle* handle,
                                             const gchar *seq, 
                                             const gchar* headers[]);
ImapResponse imap_mbox_handle_fetch_env(ImapMboxHandle* handle,
                                        const gchar *seq);

ImapMessage* imap_mbox_handle_get_msg(ImapMboxHandle* handle, unsigned seqno);

unsigned imap_mbox_handle_first_unseen(ImapMboxHandle* handle);
ImapResponse imap_mbox_find_all(ImapMboxHandle *h, const char *search_str,
                                unsigned *msgcnt, unsigned**msgs);

unsigned imap_mbox_set_sort(ImapMboxHandle *h, ImapSortKey isr, 
                            int ascending);
const char *imap_mbox_get_filter(ImapMboxHandle *h);
/* get_thread_root is deprecated */
GNode *imap_mbox_handle_get_thread_root(ImapMboxHandle* handle); 

/* ================ BEGIN OF BODY STRUCTURE FUNCTIONS ================== */
const gchar *imap_body_get_param(ImapBody *body, const gchar *param);
const gchar *imap_body_get_dsp_param(ImapBody *body, const gchar *key);
gchar *imap_body_get_mime_type(ImapBody *body);
gchar *imap_body_get_content_type(ImapBody *body);

ImapBody *imap_message_get_body_from_section(ImapMessage *msg,
                                             const char *section);

/* ================ END OF BODY STRUCTURE FUNCTIONS ==================== */

/** Stores data returned when UIDPLUS extension is provided. */
typedef struct ImapSequence_ {
  GList *ranges; /**< list of ImapUidRange items */
  unsigned uid_validity;
}  ImapSequence;

/** defines range [lo, hi] */
typedef struct ImapUidRange_ {
  unsigned lo, hi; 
} ImapUidRange;

#define imap_sequence_empty(i_seq) ( (i_seq)->ranges == NULL)
unsigned imap_sequence_length(ImapSequence *i_seq);
unsigned imap_sequence_nth(ImapSequence *i_seq, unsigned nth);
void imap_sequence_foreach(ImapSequence *i_seq,
			   void(*cb)(unsigned uid, void *arg), void *cb_arg);
#define imap_sequence_init(i_seq) \
  do { (i_seq)->ranges = NULL; (i_seq)->uid_validity = 0; }while(0)
void imap_sequence_release(ImapSequence *i_seq);

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

gboolean imap_server_probe(const gchar *host, guint timeout_secs, NetClientProbeResult *result, GCallback cert_cb, GError **error);

#endif
