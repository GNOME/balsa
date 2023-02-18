#ifndef __IMAP_PRIVATE_H__
#define __IMAP_PRIVATE_H__
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
#include <glib-object.h>

#include "config.h"

#include "net-client-siobuf.h"

#include "imap-commands.h"
#include "imap_compress.h"

typedef enum {
  IMAP_BODY_TYPE_RFC822, /**< as fetched with RFC822 */
  IMAP_BODY_TYPE_HEADER, /** header of the message: BODY[HEADER] */
  IMAP_BODY_TYPE_TEXT,   /**< content of the message part: BODY[TEXT] */
  IMAP_BODY_TYPE_BODY    /**< a part as fetched with BODY[x] */
} ImapFetchBodyType;


typedef void (*ImapFetchBodyInternalCb)(unsigned seqno,
					ImapFetchBodyType body_type,
					const char *buf,
					size_t buflen, void* arg);


struct _MboxView {
  unsigned *arr;
  unsigned allocated, entries;
  char * filter_str;
};

typedef struct {
  ImapMsgFlag flag_values;
  ImapMsgFlag known_flags;
} ImapFlagCache;

struct _ImapMboxHandle {
  GObject object;

  NetClientSioBuf *sio;

  char *host;
  char* mbox; /* currently selected mailbox, if any */
  int timeout; /* timeout in milliseconds */

  ImapConnectionState state;
  gboolean has_capabilities;
  char capabilities[IMCAP_MAX];
  unsigned exists;
  unsigned recent;
  unsigned unseen; /* msgno of first unseen message */
  ImapUID  uidnext;
  ImapUID  uidval;
  gchar *last_msg; /* last server message; for error reporting purposes */

  ImapMessage **msg_cache;
  GArray       *flag_cache;
  MboxView mbox_view;
  /** cmd_info is a list of commands that serves two-fold purpose. It
      can contain task to execute when certain command completes. It
      can be also used to store return codes in case they were
      received asynchronously. */
  GList *cmd_info;
  GSList *tasks;         /* a list of tasks to be executed after
                          * processing of current line is finished. */
  GNode *thread_root; /* deprecated! */

  struct {
    GList* src; /**< returned by COPY */
    GList* dst; /**< returned by APPEND and COPY */
    unsigned dst_uid_validity;
    unsigned store_response:1;
  } uidplus;

  /* BYE handling depends on the state */
  gboolean doing_logout;
  ImapInfoCb info_cb;
  void *info_arg;

  GCallback auth_cb;	/* authentication callback ("auth" signal) */
  void *auth_arg;

  GCallback cert_cb;	/* untrusted certificate callback ("cert-check" signal) */

  ImapFlagsCb flags_cb;
  void *flags_arg;
  ImapFetchBodyInternalCb body_cb;
  void *body_arg;

  ImapSearchCb search_cb;
  void *search_arg;

  GHashTable *status_resps; /* A hash of STATUS responses that we wait for */

  GSource *sock_source;
  GMutex mutex;
  guint idle_enable_id; /* callback to issue IDLE after a period of
                           inactivity */
  NetClientCryptMode tls_mode; /* disabled, enabled, required */
  NetClientAuthMode auth_mode;

  enum { IDLE_INACTIVE, IDLE_RESPONSE_PENDING, IDLE_ACTIVE }
    idle_state; /*  IDLE State? */
  unsigned op_cancelled:1; /* last op timed out and was cancelled by user */
  unsigned readonly_mbox:1;
  unsigned can_fetch_body:1; /* set for servers that always respond
                              * correctly to FETCH x BODY[y]
                              * requests. */
  unsigned enable_binary:1;      /**< enable binary extension */
  unsigned enable_client_sort:1; /**< client side sorting allowed */
  unsigned enable_compress:1; /**< enable compress extension */
  unsigned enable_idle:1;     /**< use IDLE - no problem with firewalls */
  unsigned has_rights:1;      /**< whether rights are up-to-date. */

  ImapAclType rights;         /**< my rights (RFC 4314) */
  GList *acls;                /**< acl's (RFC 4314) */

  gulong quota_max_k;         /**< max. available quota in kByte */
  gulong quota_used_k;        /**< used quota in kByte */
  gchar *quota_root;
};

#define IMAP_MBOX_IS_DISCONNECTED(h)  ((h)->state == IMHS_DISCONNECTED)
#define IMAP_MBOX_IS_CONNECTED(h)     ((h)->state == IMHS_CONNECTED)
#define IMAP_MBOX_IS_AUTHENTICATED(h) ((h)->state == IMHS_AUTHENTICATED)
#define IMAP_MBOX_IS_SELECTED(h)      ((h)->state == IMHS_SELECTED)

#define IMAP_REQUIRED_STATE1(h, state1, ret)            \
    do{if(!(h) || h->state != (state1))                 \
            {g_mutex_unlock(&h->mutex); return (ret);}}while(0);
#define IMAP_REQUIRED_STATE1_U(h, state1, ret)                  \
    do{if(!(h) || h->state != (state1)) return (ret);}while(0);

#define IMAP_REQUIRED_STATE2(h, state1, state2, ret)                    \
    do{if(!(h) || !(h->state == (state1) || h->state == (state2)))      \
            {g_mutex_unlock(&h->mutex); return (ret);}}while(0);
#define IMAP_REQUIRED_STATE3(h, state1, state2, state3, ret)           \
    do{if(!(h) || !(h->state == (state1) || h->state == (state2)||     \
                    h->state == (state3)))                             \
            {g_mutex_unlock(&h->mutex);return (ret);}}while(0);
#define IMAP_REQUIRED_STATE3_U(h, state1, state2, state3, ret)           \
    do{if(!(h) || !(h->state == (state1) || h->state == (state2)||     \
                    h->state == (state3)))                             \
            {return (ret);}}while(0);


#define IS_ATOM_CHAR(c) (strchr("(){ %*\"\\]",(c))==NULL&&(c)>0x1f&&(c)!=0x7f)

#define EAT_LINE(h, c) c = net_client_siobuf_discard_line(h->sio, NULL)

extern const char* imap_msg_flags[6];

ImapResponse imap_mbox_select_unlocked(ImapMboxHandle* handle, const char *mbox,
                                       gboolean *readonly_mbox);

ImapResponse imap_mbox_fetch_my_rights_unlocked(ImapMboxHandle* handle);

void imap_mbox_resize_cache(ImapMboxHandle *h, unsigned new_size);

ImapResponse imap_cmd_exec_cmdno(ImapMboxHandle* handle, const char* cmd,
				 unsigned *cmdno);
#define imap_cmd_exec(h, c) imap_cmd_exec_cmdno((h),(c),NULL)

ImapResponse imap_cmd_exec_cmds(ImapMboxHandle* handle, const char** cmds,
				unsigned rc_to_return);

ImapResponse imap_cmd_issue(ImapMboxHandle* handle, const char* cmd);

ImapResponse imap_write_key(ImapMboxHandle *handle, ImapSearchKey *s,
                            unsigned cmdno, int use_literal);
ImapResponse imap_search_exec_unlocked(ImapMboxHandle *h, gboolean uid, 
				       ImapSearchKey *s,
				       ImapSearchCb cb, void *cb_arg);
ImapResponse imap_assure_needed_flags(ImapMboxHandle *h,
                                      ImapMsgFlag needed_flags);

void imap_handle_disconnect(ImapMboxHandle *h);
ImapConnectionState imap_mbox_handle_get_state(ImapMboxHandle *h);
void imap_mbox_handle_set_state(ImapMboxHandle *h,
                                ImapConnectionState newstate);
void imap_mbox_handle_set_msg(ImapMboxHandle *handle, const gchar *fmt, ...)
	G_GNUC_PRINTF(2, 3);

typedef unsigned (*ImapCoalesceFunc)(int, void*);
gchar* imap_coalesce_seq_range(int lo, int hi,
			       ImapCoalesceFunc fun, void *data);
unsigned imap_coalesce_func_simple(int i, unsigned msgno[]);
gchar *imap_coalesce_set(int cnt, unsigned *seqnos);

/* even more private functions */
int imap_cmd_start(ImapMboxHandle* handle, const char* cmd, unsigned* cmdno);
ImapResponse imap_cmd_step(ImapMboxHandle* handle, unsigned cmdno);
ImapResponse imap_cmd_process_untagged(ImapMboxHandle* handle, unsigned cmdno);
unsigned imap_make_tag(ImapCmdTag tag);
void mbox_view_append_no(MboxView *mv, unsigned seqno);

extern const char* imap_status_item_names[5];
#endif /* __IMAP_PRIVATE_H__ */
