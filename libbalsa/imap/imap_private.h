#ifndef __IMAP_PRIVATE_H__
#define __IMAP_PRIVATE_H__
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

#include <glib-object.h>

#include <config.h>

#include "imap-commands.h"

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

  int sd; /* socket descriptor */
  struct siobuf * sio;
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
  GHashTable *cmd_queue; /* A hash of commands we received return codes for.
                          * We use the command number as the key. */
  GSList *tasks;         /* a list of tasks to be executed after
                          * processing of current line is finished. */

  GNode *thread_root; /* deprecated! */

  /* BYE handling depends on the state */
  gboolean doing_logout;
  ImapInfoCb info_cb;
  void *info_arg;
  ImapUserCb user_cb; /* for user interaction, if really necessary */
  void *user_arg;

  ImapFlagsCb flags_cb;
  void *flags_arg;
  ImapFetchBodyCb body_cb;
  void *body_arg;

  ImapMonitorCb monitor_cb;
  void *monitor_arg;

  ImapSearchCb search_cb;
  void *search_arg;

  GIOChannel *iochannel; /* IO channel used for monitoring the connection */
  guint idle_enable_id; /* callback to issue IDLE after a period of
                           inactivity */
  guint idle_watch_id;  /* callback to process incoming data */
  ImapTlsMode tls_mode; /* disabled, enabled, required */
#ifdef USE_TLS
  unsigned over_ssl:1; /* transmission is to be made over SSL-protected
                        * connection, usually to imaps port. */
  unsigned using_tls:1;
#endif
  unsigned readonly_mbox:1;
  unsigned can_fetch_body:1; /* set for servers that always respond
                              * correctly to FETCH x BODY[y]
                              * requests. */
};

#define IMAP_MBOX_IS_DISCONNECTED(h)  ((h)->state == IMHS_DISCONNECTED)
#define IMAP_MBOX_IS_CONNECTED(h)     ((h)->state == IMHS_CONNECTED)
#define IMAP_MBOX_IS_AUTHENTICATED(h) ((h)->state == IMHS_AUTHENTICATED)
#define IMAP_MBOX_IS_SELECTED(h)      ((h)->state == IMHS_SELECTED)

#define IMAP_REQUIRED_STATE1(h, state1, ret) \
        do{if(!(h) || h->state != (state1)) return (ret);}while(0);
#define IMAP_REQUIRED_STATE2(h, state1, state2, ret) \
        do{if(!(h) || !(h->state == (state1) || h->state == (state2)))\
        return (ret);}while(0);
#define IMAP_REQUIRED_STATE3(h, state1, state2, state3, ret) \
        do{if(!(h) || !(h->state == (state1) || h->state == (state2)|| \
              h->state == (state3))) return (ret);}while(0);


#define IS_ATOM_CHAR(c) (strchr("(){ %*\"\\]",(c))==NULL&&(c)>0x1f&&(c)!=0x7f)

extern const char* msg_flags[6];

void imap_mbox_resize_cache(ImapMboxHandle *h, unsigned new_size);

ImapResponse imap_cmd_exec(ImapMboxHandle* handle, const char* cmd);
char* imap_mbox_gets(ImapMboxHandle *h, char* buf, size_t sz);

ImapResponse imap_write_key(ImapMboxHandle *handle, ImapSearchKey *s,
                            unsigned cmdno, int use_literal);
ImapResponse imap_assure_needed_flags(ImapMboxHandle *h,
                                      ImapMsgFlag needed_flags);

#ifdef USE_TLS
#include <openssl/ssl.h>
SSL* imap_create_ssl(void);
int imap_setup_ssl(struct siobuf *sio, const char* host, SSL *ssl,
                   ImapUserCb user_cb, void *user_arg);
#endif

ImapConnectionState imap_mbox_handle_get_state(ImapMboxHandle *h);
void imap_mbox_handle_set_state(ImapMboxHandle *h,
                                ImapConnectionState newstate);
#define imap_mbox_handle_set_msg(h,s) \
  do{g_free((h)->last_msg); (h)->last_msg = g_strdup(s); }while(0)

/* even more private functions */
int imap_cmd_start(ImapMboxHandle* handle, const char* cmd, unsigned* cmdno);
ImapResponse imap_cmd_step(ImapMboxHandle* handle, unsigned cmdno);
int      imap_handle_write(ImapMboxHandle *conn, const char *buf, size_t len);
void     imap_handle_flush(ImapMboxHandle *handle);
unsigned imap_make_tag(ImapCmdTag tag);
void mbox_view_append_no(MboxView *mv, unsigned seqno);

int imap_socket_open(const char* host, const char *def_port);


#endif /* __IMAP_PRIVATE_H__ */
