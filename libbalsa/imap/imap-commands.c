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
#define _XOPEN_SOURCE 500
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>

#include "imap-handle.h"
#include "imap-commands.h"
#include "imap_private.h"
#include "siobuf-nc.h"
#include "util.h"

struct fetch_data {
  ImapMboxHandle* h;
  ImapFetchType ift;
  ImapFetchType req_fetch_type; /* an union of what is requested with
                                 * what is fetched already: eg. POST |
                                 *  CONTENT_TYPE -> RFC822_SELECTED */
};
static unsigned
need_fetch(unsigned seqno, struct fetch_data* fd)
{
  ImapFetchType header[] =
    { IMFETCH_CONTENT_TYPE, IMFETCH_REFERENCES, IMFETCH_LIST_POST };
  unsigned i;
  ImapFetchType available_headers;

  g_return_val_if_fail(seqno>=1 && seqno<=fd->h->exists, 0);
  if(fd->h->msg_cache[seqno-1] == NULL) {
    /* We know nothing of that message, we need to fetch at least UID
     * and FLAGS if we are supposed to create ImapMessage structure
     * later. */
    fd->req_fetch_type |= IMFETCH_UID|IMFETCH_FLAGS;
    return seqno;
  }
  if( (fd->ift & IMFETCH_ENV) 
      && fd->h->msg_cache[seqno-1]->envelope == NULL) return seqno;
  if( (fd->ift & IMFETCH_BODYSTRUCT) 
      && fd->h->msg_cache[seqno-1]->body == NULL) return seqno;
  if( (fd->ift & IMFETCH_RFC822SIZE) 
      && fd->h->msg_cache[seqno-1]->rfc822size <0) return seqno;

  available_headers = fd->h->msg_cache[seqno-1]->available_headers;
  for(i=0; i<G_N_ELEMENTS(header); i++) {
    if( (fd->ift & header[i]) &&
        !(available_headers & (header[i]|IMFETCH_RFC822HEADERS|
                               IMFETCH_RFC822HEADERS_SELECTED))) {
      if(available_headers&IMFETCH_HEADER_MASK)
        fd->req_fetch_type = IMFETCH_RFC822HEADERS_SELECTED;
      return seqno;
    }
  }
  if( (fd->ift & IMFETCH_RFC822HEADERS) &&
     !(available_headers & IMFETCH_RFC822HEADERS)) return seqno;
  if( (fd->ift & IMFETCH_RFC822HEADERS_SELECTED) &&
     !(available_headers & (IMFETCH_RFC822HEADERS|
                            IMFETCH_RFC822HEADERS_SELECTED))) return seqno;

  return 0;
}

static unsigned
need_fetch_view(unsigned seqno, struct fetch_data* fd)
{
  unsigned no =0; 
  g_return_val_if_fail(seqno>=1 &&
                       seqno<=mbox_view_cnt(&fd->h->mbox_view), 0);

  no = mbox_view_get_msg_no(&fd->h->mbox_view, seqno);
  return need_fetch(no, fd);
}

struct fetch_data_set {
  struct fetch_data fd;
  unsigned *set;
};
static unsigned
need_fetch_set(unsigned i, struct fetch_data_set* fd)
{
  unsigned seqno = fd->set[i-1];
  return need_fetch(seqno, &fd->fd);
}

static unsigned
need_fetch_view_set(unsigned i, struct fetch_data_set* fd)
{
  unsigned seqno = fd->set[i-1];
  return need_fetch_view(seqno, &fd->fd);
}

/* RFC2060 */
/* 6.1 Client Commands - Any State */


/* 6.1.1 CAPABILITY Command */
int
imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap)
{
  IMAP_REQUIRED_STATE3_U(handle, IMHS_CONNECTED, IMHS_AUTHENTICATED,
                         IMHS_SELECTED, IMR_BAD);
  if(cap == IMCAP_FETCHBODY)
    return handle->can_fetch_body;

  /* perhaps it already has capabilities? */
  if(!handle->has_capabilities) {
    /* If we request CAPABILITY, we have the definitive answer
     * regardless of the response.
     */
    handle->has_capabilities = TRUE;
    imap_cmd_exec(handle, "CAPABILITY");
    if (!(handle->capabilities[IMCAP_IMAP4] ||
          handle->capabilities[IMCAP_IMAP4REV1])) {
      g_warning("IMAP4rev1 required but not provided.");
    }
  }

  return (cap<IMCAP_MAX) ? handle->capabilities[cap] : 0;
}



/* 6.1.2 NOOP Command */
ImapResponse
imap_mbox_handle_noop(ImapMboxHandle *handle)
{
  ImapResponse rc;
  if(!g_mutex_trylock(&handle->mutex)) return IMR_OK;
  IMAP_REQUIRED_STATE3(handle, IMHS_CONNECTED, IMHS_AUTHENTICATED,
                       IMHS_SELECTED, IMR_BAD);
  rc = imap_cmd_exec(handle, "NOOP");
  g_mutex_unlock(&handle->mutex);
  return rc;
}

/* 6.1.3 LOGOUT Command */
/* unref handle to logout */


/* 6.2 Client Commands - Non-Authenticated State */


/* 6.2.1 AUTHENTICATE Command */
/* for CRAM methode implemented in auth-cram.c */


/* 6.2.2 LOGIN Command */
/* implemented in imap-auth.c */


/* 6.3 Client Commands - Authenticated State */


/* 6.3.1 SELECT Command
 * readonly_mbox can be NULL. */
ImapResponse
imap_mbox_select_unlocked(ImapMboxHandle* handle, const char *mbox,
                          gboolean *readonly_mbox)
{
  gchar *mbx7;
  ImapResponse rc;
  char* cmds[3];

  IMAP_REQUIRED_STATE3_U(handle, IMHS_CONNECTED, IMHS_AUTHENTICATED,
                         IMHS_SELECTED, IMR_BAD);

  if (handle->state == IMHS_SELECTED && strcmp(handle->mbox, mbox) == 0) {
    if(readonly_mbox)
      *readonly_mbox = handle->readonly_mbox;
    return IMR_OK;
  }
  imap_mbox_resize_cache(handle, 0);
  mbox_view_dispose(&handle->mbox_view);
  handle->unseen = 0;
  handle->has_rights = 0;

  mbx7 = imap_utf8_to_mailbox(mbox);

  cmds[0] = g_strdup_printf("SELECT \"%s\"", mbx7);
  if (imap_mbox_handle_can_do(handle, IMCAP_ACL)) {
    cmds[1] = g_strdup_printf("MYRIGHTS \"%s\"", mbx7);
    cmds[2] = NULL;
  } else {
    cmds[1] = NULL;
  }
  g_free(mbx7);

  if(handle->mbox != mbox) { /* we do not "reselect" */
    g_free(handle->mbox);
    handle->mbox = g_strdup(mbox);
  }

  rc= imap_cmd_exec_cmds(handle, (const char**)&cmds[0], 0);

  g_free(cmds[0]);
  g_free(cmds[1]);

  if(rc == IMR_OK) {
    handle->state = IMHS_SELECTED;
    if(readonly_mbox) {
      *readonly_mbox = handle->readonly_mbox;
    }
  } else { /* remove even traces of untagged responses */
    g_free(handle->mbox);
    handle->mbox = NULL;

    imap_mbox_resize_cache(handle, 0);
    mbox_view_dispose(&handle->mbox_view);
    g_signal_emit_by_name(handle, "exists-notify");
  }

  return rc;
}

ImapResponse
imap_mbox_select(ImapMboxHandle* handle, const char *mbox,
                 gboolean *readonly_mbox)
{
  ImapResponse rc;
  g_mutex_lock(&handle->mutex);
  rc = imap_mbox_select_unlocked(handle, mbox, readonly_mbox);
  g_mutex_unlock(&handle->mutex);
  return rc;
}

/* 6.3.2 EXAMINE Command */
ImapResponse
imap_mbox_examine(ImapMboxHandle* handle, const char* mbox)
{
	g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE3(handle, IMHS_CONNECTED, IMHS_AUTHENTICATED,
                       IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("EXAMINE \"%s\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  if(rc == IMR_OK) {
    g_free(handle->mbox);
    handle->mbox = g_strdup(mbox);
    handle->state = IMHS_SELECTED;
    handle->has_rights = 0;
  } 
  g_mutex_unlock(&handle->mutex);
  return rc;
  }
}


/* 6.3.3 CREATE Command */
ImapResponse
imap_mbox_create(ImapMboxHandle* handle, const char* mbox)
{
  gchar *mbx7, *cmd;
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE2(handle, IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);

  mbx7 = imap_utf8_to_mailbox(mbox);
  cmd = g_strdup_printf("CREATE \"%s\"", mbx7);
  rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  g_mutex_unlock(&handle->mutex);

  return rc;
}


/* 6.3.4 DELETE Command */
ImapResponse
imap_mbox_delete(ImapMboxHandle* handle, const char* mbox)
{
  gchar *mbx7, *cmd;
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);

  mbx7 = imap_utf8_to_mailbox(mbox);
  cmd = g_strdup_printf("DELETE \"%s\"", mbx7);
  rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  g_mutex_unlock(&handle->mutex);

  return rc;
}


/* 6.3.5 RENAME Command */
ImapResponse
imap_mbox_rename(ImapMboxHandle* handle,
		 const char* old_mbox,
		 const char* new_mbox)
{
  gchar *mbx7o, *mbx7n, *cmd;  
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);

  mbx7o = imap_utf8_to_mailbox(old_mbox);
  mbx7n = imap_utf8_to_mailbox(new_mbox);

  cmd = g_strdup_printf("RENAME \"%s\" \"%s\"", mbx7o, mbx7n);
  rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);

  g_mutex_unlock(&handle->mutex);
  return rc;
}


/* 6.3.6 SUBSCRIBE Command */
/* 6.3.7 UNSUBSCRIBE Command */
ImapResponse
imap_mbox_subscribe(ImapMboxHandle* handle,
		    const char* mbox, gboolean subscribe)
{
  gchar *mbx7, *cmd;
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);

  mbx7 = imap_utf8_to_mailbox(mbox);
  cmd = g_strdup_printf("%s \"%s\"",
                        subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE",
                        mbx7);
  rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);

  g_mutex_unlock(&handle->mutex);
  return rc;
}


/* 6.3.8 LIST Command */
ImapResponse
imap_mbox_list(ImapMboxHandle *handle, const char* what)
{
  gchar *mbx7, *cmd;
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  mbx7 = imap_utf8_to_mailbox(what);
  cmd = g_strdup_printf("LIST \"%s\" \"%%\"", mbx7);
  rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  g_free(mbx7);

  g_mutex_unlock(&handle->mutex);
  return rc;
}


/* 6.3.9 LSUB Command */
ImapResponse
imap_mbox_lsub(ImapMboxHandle *handle, const char* what)
{
  gchar *mbx7, *cmd;
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);

  mbx7 = imap_utf8_to_mailbox(what);
  cmd = g_strdup_printf("LSUB \"%s\" \"%%\"", mbx7);
  rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);

  g_mutex_unlock(&handle->mutex);
  return rc;
}


/* 6.3.10 STATUS Command */

ImapResponse
imap_mbox_status(ImapMboxHandle *r, const char*what,
                 struct ImapStatusResult *res)
{
  const char *item_arr[G_N_ELEMENTS(imap_status_item_names)+1];
  ImapResponse rc = IMR_OK;
  unsigned i, ipos;
  
  for(ipos = i= 0; res[i].item != IMSTAT_NONE; i++) {
    /* repeated items? */
    g_return_val_if_fail(i<G_N_ELEMENTS(imap_status_item_names), IMR_BAD);
    /* invalid item? */
    g_return_val_if_fail(res[i].item>=IMSTAT_MESSAGES &&
                         res[i].item<=IMSTAT_UNSEEN, IMR_BAD);
    item_arr[ipos++] = imap_status_item_names[res[i].item];
  }
  item_arr[ipos] = NULL;
  if(ipos>0) {
    gchar *mbx7 = imap_utf8_to_mailbox(what);
    gchar *items = g_strjoinv(" ", (gchar**)&item_arr[0]);
    gchar *cmd = g_strdup_printf("STATUS \"%s\" (%s)", mbx7, items);
    g_mutex_lock(&r->mutex);
    g_hash_table_insert(r->status_resps, (gpointer)what, res);
    rc = imap_cmd_exec(r, cmd);
    g_hash_table_remove(r->status_resps, what);
    g_mutex_unlock(&r->mutex);
    g_free(mbx7); g_free(cmd);
    g_free(items);
  }
  return rc; 
}
/* 6.3.11 APPEND Command */
static gchar*
enum_flag_to_str(ImapMsgFlags flg)
{
  GString *flags_str = g_string_new("");
  unsigned idx;

  for(idx=0; idx < G_N_ELEMENTS(imap_msg_flags); idx++) {
    if((flg & (1<<idx)) == 0) continue;
    if(*flags_str->str) g_string_append_c(flags_str, ' ');
    g_string_append_c(flags_str, '\\');
    g_string_append(flags_str, imap_msg_flags[idx]);
  }
  return g_string_free(flags_str, FALSE);
}

struct SingleAppendData {
  ImapAppendFunc cb;
  void *cb_data;
  size_t size;
  ImapMsgFlags flags;
};

static size_t
single_append_cb(char *buf, size_t buf_sz, ImapAppendMultiStage stage,
		 ImapMsgFlags *f, void *arg)
{
  struct SingleAppendData *sad = (struct SingleAppendData*)arg;
  size_t sz;
  switch(stage) {
  case IMA_STAGE_NEW_MSG:
    *f = sad->flags; 
    sz = sad->size; sad->size = 0;
    return sz;
  case IMA_STAGE_PASS_DATA:
    return sad->cb(buf, buf_sz, sad->cb_data);
  }
  g_assert_not_reached();
  return 0;
}
 
ImapResponse
imap_mbox_append(ImapMboxHandle *handle, const char *mbox,
                 ImapMsgFlags flags, size_t sz,
                 ImapAppendFunc dump_cb, void *arg)
{
  struct SingleAppendData sad = { dump_cb, arg, sz, flags };
  return imap_mbox_append_multi(handle, mbox, single_append_cb, &sad, NULL);
}

static ImapResponse
append_commit(ImapMboxHandle *handle, unsigned cmdno, ImapSequence *uid_seq)
{
  ImapResponse rc;

  if(handle->uidplus.dst)
    g_warning("Leaking memory in imap_append");

  if(uid_seq) {
    handle->uidplus.dst = uid_seq->ranges;
    handle->uidplus.store_response = 1;
  }
  net_client_siobuf_flush(handle->sio, NULL);
  rc = imap_cmd_process_untagged(handle, cmdno);

  if(uid_seq) {
    if(uid_seq->uid_validity == 0) {
      uid_seq->uid_validity = handle->uidplus.dst_uid_validity;
      uid_seq->ranges = handle->uidplus.dst;
    } else if(uid_seq->uid_validity != handle->uidplus.dst_uid_validity) {
      g_debug("The IMAP server keeps changing UID validity, "
	     "ignoring UIDPLUS response (%u -> %u)",
	     uid_seq->uid_validity, handle->uidplus.dst_uid_validity);
      uid_seq->uid_validity = handle->uidplus.dst_uid_validity;
      g_list_free(uid_seq->ranges);
      uid_seq->ranges = NULL;
    } else {
      uid_seq->ranges = handle->uidplus.dst;
    }
  }
  handle->uidplus.dst = NULL;
  handle->uidplus.store_response = 0;

  return rc;
}

static ImapResponse
imap_mbox_append_multi_real(ImapMboxHandle *handle,
			    const char *mbox,
			    ImapAppendMultiFunc dump_cb,
			    void* cb_arg,
			    ImapSequence *uid_sequence)
{
  static const unsigned TRANSACTION_SIZE = 10*1024*1024;
  int use_literal, use_multiappend, use_uidplus;
  unsigned cmdno;
  ImapResponse rc = IMR_OK;
  char *litstr;
  char buf[16384];
  size_t s, msg_size, delta, current_transaction_size = 0;
  int c, msg_cnt;
  ImapMsgFlags flags;
  gboolean new_append = TRUE;

  use_literal = imap_mbox_handle_can_do(handle, IMCAP_LITERAL);
  use_multiappend = imap_mbox_handle_can_do(handle, IMCAP_MULTIAPPEND);
  use_uidplus = imap_mbox_handle_can_do(handle, IMCAP_UIDPLUS);
  litstr = use_literal ? "+" : "";

  if(uid_sequence)
    uid_sequence->ranges = NULL;

  if (!imap_handle_idle_disable(handle)) return IMR_SEVERED;
  for(msg_cnt=0;
      (msg_size = dump_cb(buf, sizeof(buf),
			  IMA_STAGE_NEW_MSG, &flags, cb_arg)) >0;
      msg_cnt++) {

    if (handle->state == IMHS_DISCONNECTED)
      return IMR_SEVERED;
  
    if(new_append || !use_multiappend) {
      gchar *cmd;
      gchar *mbx7 = imap_utf8_to_mailbox(mbox);
      new_append = FALSE;
      if(flags) {
	gchar *str = enum_flag_to_str(flags);
	cmd = g_strdup_printf("APPEND \"%s\" (%s) {%lu%s}",
			      mbx7, str, (unsigned long)msg_size, litstr);
	g_free(str);
      } else 
	cmd = g_strdup_printf("APPEND \"%s\" {%lu%s}",
			      mbx7, (unsigned long)msg_size, litstr);
      c = imap_cmd_start(handle, cmd, &cmdno);
      g_free(mbx7); g_free(cmd);

      if (c<0) /* irrecoverable connection error. */
	return IMR_SEVERED;

    } else {
      /* MULTIAPPEND continuation */
      if(flags) {
	gchar *str = enum_flag_to_str(flags);
	sio_printf(handle->sio, " (%s) {%lu%s}\r\n", str,
		   (unsigned long)msg_size, litstr);
	g_free(str);
      } else 
	sio_printf(handle->sio, " {%lu%s}\r\n",
		   (unsigned long)msg_size, litstr);
    }

    if(use_literal)
      rc = IMR_RESPOND; /* we do it without flushing */
    else {
    	net_client_siobuf_flush(handle->sio, NULL);
    	rc = imap_cmd_process_untagged(handle, cmdno);
      if(rc != IMR_RESPOND) return rc;
      net_client_siobuf_discard_line(handle->sio, NULL);
    }

    for(s=0; s<msg_size; s+= delta) {
      delta = dump_cb(buf, sizeof(buf), IMA_STAGE_PASS_DATA, NULL, cb_arg);
      if(s+delta>msg_size) delta = msg_size-s;
      sio_write(handle->sio, buf, delta);
    }
  
    current_transaction_size += msg_size;
    if(current_transaction_size > TRANSACTION_SIZE) {
      current_transaction_size = 0;
      new_append = TRUE;
    }
    if(new_append || !use_multiappend) { /* Grab the response. */
      /* Data written, tie up the message.  It has been though
       * observed that "Cyrus IMAP4 v2.0.16-p1 server" can hang if the
       * flush isn't done under following conditions: a). TLS is
       * enabled, b). message contains NUL characters.  NUL characters
       * are forbidden (RFC3501, sect. 4.3.1) and we probably should
       * make sure on a higher level that they are not sent.
       */
      /* sio_flush(handle->sio); */
      if( (rc=append_commit(handle, cmdno,
			   use_uidplus ? uid_sequence : NULL)) != IMR_OK)
      return rc;
    }
    /* And move to the next message... */
  }

  if(!new_append && use_multiappend) { /* We get the server response here... */
    rc = append_commit(handle, cmdno, use_uidplus ? uid_sequence : NULL);
  }

  imap_handle_idle_enable(handle, 30);

  if(uid_sequence) {
    uid_sequence->ranges = g_list_reverse(uid_sequence->ranges);
  }
  return rc;
}


/** Appends multiple messages at once, using MULTIAPPEND extension if
    available.
    @param handle the IMAP connection.

    @param mbox the UTF-8 encoded mailbox name the messages are to be
    appended to.

    @param dump_cb function providing the data. It is called first
    once for each message with IMA_STAGE_NEW_MSG stage parameter to
    get the message size and message flags. Size zero indicates last
    message. Next, it is called several times with IMA_STAGE_PASS_DATA
    stage parameter to actually get the message data.

    @param cb_arg the context passed to dump_cb.
 */
ImapResponse
imap_mbox_append_multi(ImapMboxHandle *handle,
		       const char *mbox,
		       ImapAppendMultiFunc dump_cb,
		       void* cb_arg,
		       ImapSequence *uids)
{
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  rc = imap_mbox_append_multi_real(handle, mbox, dump_cb, cb_arg, uids);
  g_mutex_unlock(&handle->mutex);
  return rc;
}

#ifdef USE_IMAP_APPEND_STR	/* not used currently */
static size_t
pass_str(char* buf, size_t sz, void*arg)
{
  char **s = (char**)arg;
  size_t cnt;
  for(cnt=0; (*s)[cnt] && cnt<sz; cnt++)
    ;
  memcpy(buf, *s, cnt);
  *s += cnt;
  return cnt;
}

/* txt must be CRLF-encoded */
ImapResponse
imap_mbox_append_str(ImapMboxHandle *handle, const char *mbox,
                     ImapMsgFlags flags, size_t sz, char *txt)
{
  char * s = txt;
  return imap_mbox_append(handle, mbox, flags, sz, pass_str, &s);
}
#endif

static size_t
pass_stream(char* buf, size_t sz, void*arg)
{
  return g_mime_stream_read((GMimeStream *) arg, buf, sz);
}

ImapResponse
imap_mbox_append_stream(ImapMboxHandle *handle, const char *mbox,
			ImapMsgFlags flags, GMimeStream *stream, ssize_t len)
{
  if (len < 0)
    len = g_mime_stream_length(stream);
  return imap_mbox_append(handle, mbox, flags, len, pass_stream, stream);
}


/* 6.4 Client Commands - Selected State */


/* 6.4.1 CHECK Command */
/* FIXME: implement */

/* 6.4.2 CLOSE Command */
ImapResponse
imap_mbox_close(ImapMboxHandle *h)
{
  ImapResponse rc;
  g_mutex_lock(&h->mutex);
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);

  rc = imap_cmd_exec(h, "CLOSE");  
  if(rc == IMR_OK)
    h->state = IMHS_AUTHENTICATED;
  g_mutex_unlock(&h->mutex);
  return rc;
}

/* 6.4.3 EXPUNGE Command */
ImapResponse
imap_mbox_expunge(ImapMboxHandle *handle)
{
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  rc = imap_cmd_exec(handle, "EXPUNGE");
  g_mutex_unlock(&handle->mutex);
  return rc;
}

ImapResponse
imap_mbox_expunge_a(ImapMboxHandle *handle)
{
  ImapResponse rc;
  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  /* extra care would be required to use this once since no other
     commands that use sequence numbers can be issued before this one
     finishes... */
  rc = imap_cmd_issue(handle, "EXPUNGE");
  g_mutex_unlock(&handle->mutex);
  return rc;
}

/* 6.4.4 SEARCH Command */
unsigned
imap_mbox_handle_first_unseen(ImapMboxHandle* handle)
{
  /* might have to do search here. */
  return handle->unseen;
}

struct find_all_data {
  unsigned *seqno;
  unsigned msgcnt, allocated;
};

static void
find_all_cb(ImapMboxHandle* handle, unsigned seqno, void*arg)
{
  struct find_all_data *ms = arg;
  if(ms->msgcnt==ms->allocated) {
    ms->allocated = ms->allocated > 0 ? ms->allocated*2 : 16;
    ms->seqno = g_realloc(ms->seqno, ms->allocated*sizeof(unsigned));
    /* FIXME: error handling */
  }
  ms->seqno[ms->msgcnt++] = seqno;
}

static ImapResponse
imap_mbox_find_helper(ImapMboxHandle * h,
		      const char     * cmd,
		      unsigned       * msgcnt,
		      unsigned      ** msgs)
{
  ImapResponse rc;
  void *arg;
  ImapSearchCb cb;
  struct find_all_data fad;

  fad.allocated = fad.msgcnt = 0;  fad.seqno = NULL;
  cb  = h->search_cb;  h->search_cb  = (ImapSearchCb)find_all_cb;
  arg = h->search_arg; h->search_arg = &fad;
  rc = imap_cmd_exec(h, cmd);
  h->search_cb = cb; h->search_arg = arg;
  *msgcnt = fad.msgcnt; *msgs = fad.seqno;
  return rc;
}

ImapResponse
imap_mbox_find_all(ImapMboxHandle * h,
		   const char     * search_str,
		   unsigned       * msgcnt,
		   unsigned      ** msgs)
{
  const char *filter_str;
  gchar *cmd;
  ImapResponse rc;

  g_mutex_lock(&h->mutex);
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  filter_str = mbox_view_get_str(&h->mbox_view);
  cmd = g_strdup_printf("SEARCH ALL (SUBJECT \"%s\"%s%s)", search_str,
			*filter_str ? " " : "", filter_str);
  rc = imap_mbox_find_helper(h, cmd, msgcnt, msgs);
  g_free(cmd);

  g_mutex_unlock(&h->mutex);
  return rc;
}

ImapResponse
imap_mbox_find_unseen(ImapMboxHandle * h,
		      unsigned       * msgcnt,
		      unsigned      ** msgs)
{
    ImapResponse rc;

    g_mutex_lock(&h->mutex);
    IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
    rc = imap_mbox_find_helper(h, "SEARCH UNSEEN UNDELETED", msgcnt, msgs);
    g_mutex_unlock(&h->mutex);
    return rc;
}

/* imap_mbox_handle_msgno_has_flags() this is optimized under
   assumtion that we need only few flags. The code should probably
   recognize if too many different flags are needed and in such a case
   issue a full blown fetch with necessary then renumbering to take
   care of possible EXPUNGE - which cannot be sent in response to
   SEARCH.  Additionally, we invert the searching condition for SEEN
   flag which is most commonly set.
 */

static void
set_flag_cache_cb(ImapMboxHandle* h, unsigned seqno, void*arg)
{
  ImapFlagCache *flags;
  ImapMsgFlag *searched_flag = (ImapMsgFlag*)arg;
  flags = &g_array_index(h->flag_cache, ImapFlagCache, seqno-1);
  if(*searched_flag & IMSGF_SEEN)
    flags->flag_values &= ~*searched_flag;
  else
    flags->flag_values |= *searched_flag;
}

struct flag_needed_data {
  ImapMboxHandle *handle;
  ImapMsgFlag     flag;
};
static unsigned
flag_unknown(int i, struct flag_needed_data* d)
{
  ImapFlagCache *f =
    &g_array_index(d->handle->flag_cache, ImapFlagCache, i-1);
  return (f->known_flags & d->flag) == d->flag ? 0 : i;
}
  
/* imap_assure_needed_flags issues several SEARCH queries in one shot
   and hopes the output is not intermixed. This functionl must be
   called with handle locked. */
ImapResponse
imap_assure_needed_flags(ImapMboxHandle *h, ImapMsgFlag needed_flags)
{
  ImapSearchCb cb;
  void *arg;
  struct flag_needed_data fnd;
  unsigned i, shift, issued_cmd = 0;
  unsigned cmdno[32]; /* FIXME: assume no more than 32 different flags */
  ImapMsgFlag flag[32]; /* ditto */
  int ics;
  gchar *cmd = NULL, *seqno;
  ImapResponse rc = IMR_OK;
  gchar *cmd_format;

  if (h->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;

  if(imap_mbox_handle_can_do(h, IMCAP_ESEARCH))
    cmd_format = "SEARCH RETURN (ALL) %s %s";
  else
    cmd_format = "SEARCH %s %s";

  cb  = h->search_cb;  h->search_cb  = (ImapSearchCb)set_flag_cache_cb;
  arg = h->search_arg; 

  fnd.handle = h;
  for(shift=0; needed_flags>>shift; shift++) {
    if((needed_flags>>shift) & 1) {
      const char *flg;
      fnd.flag = 1<<shift;
      if(fnd.flag & IMSGF_SEEN)
        for(i=0; i<h->flag_cache->len; i++) {
          ImapFlagCache *f =
            &g_array_index(h->flag_cache, ImapFlagCache, i);
          if( (f->known_flags & fnd.flag) ==0)
            f->flag_values |= fnd.flag;
        }
      seqno = imap_coalesce_seq_range(1, h->exists,
				      (ImapCoalesceFunc)flag_unknown, &fnd);
      if(!seqno) /* oops, why were we called in the first place!? */
        continue;
      switch(fnd.flag) {
      case IMSGF_SEEN:     flg = "UNSEEN"; break;
      case IMSGF_ANSWERED: flg = "ANSWERED"; break;
      case IMSGF_FLAGGED:  flg = "FLAGGED"; break;
      case IMSGF_DELETED:  flg = "DELETED"; break;
      case IMSGF_DRAFT:    flg = "DRAFT"; break;
      case IMSGF_RECENT:   flg = "RECENT"; break;
      default: g_free(seqno); continue;
      }
      if(!cmd) {
        if (!imap_handle_idle_disable(h)) { rc = IMR_SEVERED; break; }
      }
      cmd = g_strdup_printf(cmd_format, seqno, flg);
      g_free(seqno);
      flag[issued_cmd] = fnd.flag;
      ics = imap_cmd_start(h, cmd, &cmdno[issued_cmd++]);
      g_free(cmd);
      if(ics<0)
        return IMR_SEVERED;  /* irrecoverable connection error. */
    }
  }
  if(cmd) { /* was ever altered */
    for(i=0; i<issued_cmd; i++) {
      h->search_arg = &flag[i];
      rc = imap_cmd_process_untagged(h, cmdno[i]);
    }
    imap_handle_idle_enable(h, 30);
  }
  h->search_cb = cb; h->search_arg = arg;
  if(rc == IMR_OK) {
    for(i=0; i<h->flag_cache->len; i++) {
      ImapFlagCache *f =
        &g_array_index(h->flag_cache, ImapFlagCache, i);
      f->known_flags |= needed_flags;
    }
  }
  return rc;
}

gboolean
imap_mbox_handle_msgno_has_flags(ImapMboxHandle *h, unsigned msgno,
                                 ImapMsgFlag flag_set,
                                 ImapMsgFlag flag_unset)
{
  ImapFlagCache *flags;
  ImapMsgFlag needed_flags;
  gboolean retval;

  g_mutex_lock(&h->mutex);
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);

  flags = &g_array_index(h->flag_cache, ImapFlagCache, msgno-1);
  
  needed_flags = ~flags->known_flags & (flag_set | flag_unset);
  
  retval =
    (!needed_flags||imap_assure_needed_flags(h, needed_flags) == IMR_OK) &&
    (flags->flag_values & flag_set) == flag_set &&
    (flags->flag_values & flag_unset) == 0;
  g_mutex_unlock(&h->mutex);

  return retval;
}



/* 6.4.5 FETCH Command */
static void
ic_construct_header_list(const char **hdr, ImapFetchType ift)
{
  int idx = 0;
  if(ift & IMFETCH_FLAGS)      hdr[idx++] = "FLAGS";
  if(ift & IMFETCH_UID)        hdr[idx++] = "UID";
  if(ift & IMFETCH_ENV)        hdr[idx++] = "ENVELOPE";
  if(ift & IMFETCH_BODYSTRUCT) hdr[idx++] = "BODYSTRUCTURE";
  if(ift & IMFETCH_RFC822SIZE) hdr[idx++] = "RFC822.SIZE";
  if(ift & IMFETCH_HEADER_MASK) {
    hdr[idx++] = "BODY.PEEK[HEADER.FIELDS (";
    if(ift & IMFETCH_CONTENT_TYPE) hdr[idx++] = "CONTENT-TYPE";
    if(ift & IMFETCH_REFERENCES)   hdr[idx++] = "REFERENCES";
    if(ift & IMFETCH_LIST_POST)    hdr[idx++] = "LIST-POST";
    hdr[idx++] = ")]";
  }
  if(ift & IMFETCH_RFC822HEADERS) hdr[idx++] = "BODY.PEEK[HEADER]";
  if(ift & IMFETCH_RFC822HEADERS_SELECTED)
    hdr[idx++] = "BODY.PEEK[HEADER.FIELDS.NOT (DATE SUBJECT FROM SENDER "
      "REPLY-TO TO CC BCC IN-REPLY-TO MESSAGE-ID)]";
  hdr[idx] = NULL;
}

/* set_avail_headers interprets given sequence string as constructed
 * by coalesce_seq_no and set the available_headers field for all the
 * cached message structures. We need of course to reset the
 * available_headers for header-relevant fetches, not for arbitrary
 * ones (BODYSTRUCTURE). */
static void
set_avail_headers(ImapMboxHandle *h, const char *seq, ImapFetchType ift)
{
  const char *s = seq;
  char *tmp;
  unsigned lo, hi; 

  if( !(ift & (IMFETCH_HEADER_MASK|IMFETCH_RFC822HEADERS|
               IMFETCH_RFC822HEADERS_SELECTED)) ) return;
  while(*s) {
    lo = strtoul(s, &tmp, 10);
    switch(*tmp) {
    case '\0':
    case ',':
      if(h->msg_cache[lo-1])
        h->msg_cache[lo-1]->available_headers = ift;
      break;
    case ':': hi = strtoul(tmp+1, &tmp, 10);
      for(;lo<=hi; lo++)
        if(h->msg_cache[lo-1]) {
          h->msg_cache[lo-1]->available_headers = ift;
        }
      
      break;
    default: g_warning("unexpected sequence %s", seq);
    }
    if(*tmp==',') tmp++;
    s = tmp;
  }
      
}

ImapResponse
imap_mbox_handle_fetch_range(ImapMboxHandle* handle,
                             unsigned lo, unsigned hi, ImapFetchType ift)
{
  gchar * seq;
  ImapResponse rc;
  unsigned exists;
  ImapCoalesceFunc cf;
  struct fetch_data fd;

  if(lo>hi) return IMR_OK;
  if(lo<1) lo = 1;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);

  cf = (ImapCoalesceFunc)(mbox_view_is_active(&handle->mbox_view)
                          ? need_fetch_view : need_fetch);
  fd.h = handle; fd.ift = fd.req_fetch_type = ift;
  
  exists = imap_mbox_handle_get_exists(handle);
  if(hi>exists) hi = exists;
  seq = imap_coalesce_seq_range(lo, hi, cf, &fd);
  if(seq) {
    const char* hdr[13];
    ic_construct_header_list(hdr, fd.req_fetch_type);
    rc = imap_mbox_handle_fetch_unlocked(handle, seq, hdr);
    if(rc == IMR_OK) set_avail_headers(handle, seq, fd.req_fetch_type);
    g_free(seq);
  } else rc = IMR_OK;

  g_mutex_unlock(&handle->mutex);
  return rc;
}

static ImapResponse
imap_mbox_handle_fetch_set_unlocked(ImapMboxHandle* handle,
				    unsigned *set, unsigned cnt,
				    ImapFetchType ift)
{
  gchar * seq;
  ImapResponse rc;
  ImapCoalesceFunc cf;
  struct fetch_data_set fd;

  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  if(cnt == 0) return IMR_OK;

  fd.fd.h = handle; fd.fd.ift = fd.fd.req_fetch_type = ift;
  fd.set = set;
  cf = (ImapCoalesceFunc)(mbox_view_is_active(&handle->mbox_view)
			  ? need_fetch_view_set : need_fetch_set);
  seq = imap_coalesce_seq_range(1, cnt, cf, &fd);
  if(seq) {
    const char* hdr[13];
    ic_construct_header_list(hdr, fd.fd.req_fetch_type);
    rc = imap_mbox_handle_fetch_unlocked(handle, seq, hdr);
    if(rc == IMR_OK) set_avail_headers(handle, seq, fd.fd.req_fetch_type);
    g_free(seq);
  } else rc = IMR_OK;
  return rc;
}

ImapResponse
imap_mbox_handle_fetch_set(ImapMboxHandle* handle,
                           unsigned *set, unsigned cnt, ImapFetchType ift)
{
  ImapResponse rc;
  g_mutex_lock(&handle->mutex);
  rc = imap_mbox_handle_fetch_set_unlocked(handle, set, cnt, ift);
  g_mutex_unlock(&handle->mutex);
  return rc;
}

static void
write_nstring(unsigned seqno, ImapFetchBodyType body_type,
              const char *str, size_t len, void *fl)
{
  if (fwrite(str, 1, len, (FILE*)fl) != len)
    perror("write_nstring");
}

struct FetchBodyPassthroughData {
  ImapFetchBodyCb cb;
  void *arg;
  char *body;
  size_t body_length;
  unsigned seqno;
  unsigned pipeline_error;
};

static void
fetch_body_passthrough(unsigned seqno,
		       ImapFetchBodyType body_type,
		       const char *buf,
		       size_t buflen, void* arg)
{
  struct FetchBodyPassthroughData* data = (struct FetchBodyPassthroughData*)arg;
  switch(body_type) {
  case IMAP_BODY_TYPE_RFC822:
    data->cb(seqno, buf, buflen, data->arg);
    break;
  case IMAP_BODY_TYPE_HEADER:
    if(data->seqno == 0) {
      data->seqno = seqno;
      data->cb(seqno, buf, buflen, data->arg);
    } else {
      if(data->seqno == seqno) {
	data->cb(seqno, buf, buflen, data->arg);
	data->cb(seqno, data->body, data->body_length, data->arg);
	g_free(data->body);
	data->body = NULL;
	data->seqno = 0;
      } else {
	/* This server sends data in a strange order that makes
	   efficient pipeline processing impossible. Just signal an
	   error. */
	data->pipeline_error++;
      }
    }
    break;
  case IMAP_BODY_TYPE_TEXT:
    if(data->seqno == seqno) {
      data->cb(seqno, buf, buflen, data->arg);
      data->seqno = 0;
    } else {
      /* Text before header. Still, we can afford to invert it.. */
      if(data->body)
	data->pipeline_error++; /* Unlikely... */
      else {
	data->body = g_malloc(buflen);
	memcpy(data->body, buf, buflen);
	data->body_length = buflen;
	data->seqno = seqno;
      }
    }
    break;
  default:
    data->pipeline_error++;
  }
}

/* note: this function is called by imap_tst.c *only* */
ImapResponse
imap_mbox_handle_fetch_rfc822(ImapMboxHandle* handle,
			      unsigned cnt, unsigned *set,
			      gboolean peek_only,
			      ImapFetchBodyCb fetch_cb,
			      void *fetch_cb_data)
{
  ImapResponse rc = IMR_OK;
  gchar *seq;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);

  seq = imap_coalesce_set(cnt, set);

  if(seq) {
    ImapFetchBodyInternalCb cb = handle->body_cb;
    void                   *arg = handle->body_arg;
    gchar *cmd = g_strdup_printf("FETCH %s %s", seq,
				 peek_only
				 ? "(BODY.PEEK[HEADER] BODY.PEEK[TEXT])"
				 : "BODY[]");
    struct FetchBodyPassthroughData passthrough_data;
    passthrough_data.cb = fetch_cb;
    passthrough_data.arg = fetch_cb_data;
    passthrough_data.body = NULL;
    passthrough_data.seqno = 0;
    passthrough_data.pipeline_error = 0;
    handle->body_cb  = fetch_cb ? fetch_body_passthrough : NULL;
    handle->body_arg = &passthrough_data;
    rc = imap_cmd_exec(handle, cmd);
    handle->body_cb  = cb;
    handle->body_arg = arg;
    g_free(cmd);
    g_free(seq);
    if(passthrough_data.pipeline_error){
      rc = IMR_NO;
      imap_mbox_handle_set_msg(handle, _("Unordered data received from server"));
    }
  }
  g_mutex_unlock(&handle->mutex);

  return rc;
}

/* When peeking into message, we need to assure that we save header
   first and body later: the order the fields are returned is in
   principle undefined. */
struct FetchBodyHeaderText {
  FILE *out_file;
  char *body;
  size_t length;
  gboolean wrote_header;
};

static void
write_header_text_ordered(unsigned seqno, ImapFetchBodyType body_type,
			  const char *str, size_t len, void *arg)
{
  struct FetchBodyHeaderText *fbht = (struct FetchBodyHeaderText*)arg;
  switch(body_type) {
  case IMAP_BODY_TYPE_RFC822:
    g_warning("Server sends unrequested RFC822 response");
    break; /* This is really unexpected response! */
  case IMAP_BODY_TYPE_HEADER:
    if (fwrite(str, 1, len, fbht->out_file) != len)
      perror("write_nstring");
    fbht->wrote_header = TRUE;
    if(fbht->body) {
      if (fwrite(fbht->body, 1, fbht->length, fbht->out_file) != fbht->length)
	perror("write_nstring");
      g_free(fbht->body); fbht->body = NULL;
    }
    break;
  case IMAP_BODY_TYPE_TEXT:
  case IMAP_BODY_TYPE_BODY:
    if(fbht->wrote_header) {
      if (fwrite(str, 1, len, fbht->out_file) != len)
	perror("write_nstring");
    } else {
      fbht->body = g_malloc(len);
      fbht->length = len;
      memcpy(fbht->body, str, len);
    }
    break;
  }
}

/* There is some point to code reuse here. */
struct PassHeaderTextOrdered {
  ImapFetchBodyCb cb;
  void *arg;
  char *body;
  size_t length;
  gboolean wrote_header;
};

static void
pass_header_text_ordered(unsigned seqno, ImapFetchBodyType body_type,
			  const char *str, size_t len, void *arg)
{
  struct PassHeaderTextOrdered *phto = (struct PassHeaderTextOrdered*)arg;
  switch(body_type) {
  case IMAP_BODY_TYPE_RFC822:
    g_warning("Server sends unrequested RFC822 response");
    break; /* This is really unexpected response! */
  case IMAP_BODY_TYPE_HEADER:
    phto->cb(seqno, str, len, phto->arg);
    phto->wrote_header = TRUE;
    if(phto->body) {
      phto->cb(seqno, phto->body, phto->length, phto->arg);
      g_free(phto->body); phto->body = NULL;
    }
    break;
  case IMAP_BODY_TYPE_TEXT:
  case IMAP_BODY_TYPE_BODY:
    if(phto->wrote_header) {
      phto->cb(seqno, str, len, phto->arg);
    } else {
      phto->body = g_malloc(len);
      phto->length = len;
      memcpy(phto->body, str, len);
    }
    break;
  }
}


ImapResponse
imap_mbox_handle_fetch_rfc822_uid(ImapMboxHandle* handle, unsigned uid, 
                                  gboolean peek, FILE *fl)
{
  char cmd[80];
  ImapFetchBodyInternalCb cb = handle->body_cb;
  void          *arg = handle->body_arg;
  ImapResponse rc;
  char *cmdstr;
  struct FetchBodyHeaderText separate_arg;


  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);

  /* Consider switching between BODY.PEEK[HEADER] BODY.PEEK[TEXT] and
     BODY[HEADER] BODY[TEXT] - this would simplify the callback
     code. */
  if(peek) {
    handle->body_cb  = write_header_text_ordered;
    handle->body_arg = &separate_arg;
    separate_arg.body = NULL;
    separate_arg.wrote_header = FALSE;
    separate_arg.out_file = fl;
    cmdstr = "UID FETCH %u (BODY.PEEK[HEADER] BODY.PEEK[TEXT])";
  } else {
    handle->body_cb  = write_nstring;
    handle->body_arg = fl;
    cmdstr = "UID FETCH %u BODY[]";
  }

  snprintf(cmd, sizeof(cmd), cmdstr, uid);
  rc = imap_cmd_exec(handle, cmd);
  if(peek) {
    g_free(separate_arg.body); /* This should never be needed. */
  }

  handle->body_cb  = cb;
  handle->body_arg = arg;
  g_mutex_unlock(&handle->mutex);
  return rc;
}

/** A structure needed to add a faked header to data fetched via
    binary extension. */
struct ImapBinaryData {
  ImapBody *body;
  ImapFetchBodyCb body_cb;
  void *body_arg;
  gboolean first_run;
};

static void
imap_binary_handler(unsigned seqno, ImapFetchBodyType body_type,
		    const char *buf, size_t buflen, void* arg)
{
  struct ImapBinaryData *ibd = (struct ImapBinaryData*)arg;
  if(ibd->first_run) {
    char *content_type = imap_body_get_content_type(ibd->body);
    char *str = g_strdup_printf("Content-Type: %s\r\n"
				"Content-Transfer-Encoding: binary\r\n\r\n",
				content_type);
    g_free(content_type);
    ibd->body_cb(seqno, str, strlen(str), ibd->body_arg);
    g_free(str);
    ibd->first_run = FALSE;
  }
  ibd->body_cb(seqno, buf, buflen, ibd->body_arg);
}

ImapResponse
imap_mbox_handle_fetch_body(ImapMboxHandle* handle, 
                            unsigned seqno, const char *section,
                            gboolean peek_only,
                            ImapFetchBodyOptions options,
                            ImapFetchBodyCb body_cb, void *arg)
{
  char cmd[200];
  ImapFetchBodyInternalCb fcb;
  void          *farg;
  ImapResponse rc;
  const gchar *peek_string = peek_only ? ".PEEK" : "";
  struct PassHeaderTextOrdered pass_ordered_data;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  fcb = handle->body_cb;
  farg = handle->body_arg;

  /* Use BINARY extension if possible */
  if(handle->enable_binary && options == IMFB_MIME &&
     imap_mbox_handle_can_do(handle, IMCAP_BINARY)) {
    struct ImapBinaryData ibd;
    ImapMessage * imsg = imap_mbox_handle_get_msg(handle, seqno);
    ibd.body = imap_message_get_body_from_section(imsg, section);
    ibd.body_cb = body_cb;
    ibd.body_arg = arg;
    ibd.first_run = TRUE;
    handle->body_cb = imap_binary_handler;
    handle->body_arg = &ibd;
    snprintf(cmd, sizeof(cmd), "FETCH %u BINARY%s[%s]",
             seqno, peek_string, section);
    rc = imap_cmd_exec(handle, cmd);
    if(rc != IMR_NO) { /* unknown-cte */
      handle->body_cb  = fcb;
      handle->body_arg = farg;
      g_mutex_unlock(&handle->mutex);
      return rc;
    }
  }

  handle->body_cb  = pass_header_text_ordered;
  handle->body_arg = &pass_ordered_data;
  pass_ordered_data.cb = body_cb;
  pass_ordered_data.arg = arg;
  pass_ordered_data.body = NULL;
  pass_ordered_data.wrote_header = FALSE;
  /* Pure IMAP without extensions */
  if(options == IMFB_NONE)
    snprintf(cmd, sizeof(cmd), "FETCH %u BODY%s[%s]",
             seqno, peek_string, section);
  else {
    char prefix[160];
    if(options == IMFB_HEADER) {
      /* We have to strip last section part and replace it with HEADER */
      unsigned sz;
      char *last_dot = strrchr(section, '.');
      strncpy(prefix, section, sizeof(prefix) - 1);
      
      if(last_dot) {
        sz = last_dot-section+1;
        if(sz>sizeof(prefix)-1) sz = sizeof(prefix)-1;
      } else sz = 0;
      strncpy(prefix + sz, "HEADER", sizeof(prefix)-sz-1);
      prefix[sizeof(prefix)-1] = '\0';
    } else
      snprintf(prefix, sizeof(prefix), "%s.MIME", section);
    snprintf(cmd, sizeof(cmd), "FETCH %u (BODY%s[%s] BODY%s[%s])",
             seqno, peek_string, prefix, peek_string, section);
  }
  rc = imap_cmd_exec(handle, cmd);
  g_free(pass_ordered_data.body);
  handle->body_cb  = fcb;
  handle->body_arg = farg;

  g_mutex_unlock(&handle->mutex);
  return rc;
}

/* 6.4.6 STORE Command */
struct msg_set {
  ImapMboxHandle *handle;
  unsigned        msgcnt;
  unsigned       *seqno;
  ImapMsgFlag     flag;
  gboolean        state;
};

static unsigned
cf_flag(unsigned idx, struct msg_set *csd)
{
  unsigned seqno = csd->seqno[idx];
  ImapMessage *imsg = imap_mbox_handle_get_msg(csd->handle, seqno);
  if(imsg &&
     ( (csd->state  && (imsg->flags & csd->flag)) || 
       (!csd->state && !(imsg->flags & csd->flag))) )
     return 0;
     else return seqno;
}

static gchar*
imap_store_prepare(ImapMboxHandle *h, unsigned msgcnt, unsigned*seqno,
		   ImapMsgFlag flg, gboolean state)
{
  gchar* cmd, *seq, *str;
  struct msg_set csd;
  unsigned i;

  csd.handle = h; csd.msgcnt = msgcnt; csd.seqno = seqno;
  csd.flag = flg; csd.state = state;
  if(msgcnt == 0) return NULL;
  seq = imap_coalesce_seq_range(0, msgcnt-1, (ImapCoalesceFunc)cf_flag, &csd);
  if(!seq) return NULL;
  str = enum_flag_to_str(flg);
  for(i=0; i<msgcnt; i++) {
    ImapMessage *msg = imap_mbox_handle_get_msg(h, seqno[i]);
    ImapFlagCache *f =
      &g_array_index(h->flag_cache, ImapFlagCache, seqno[i]-1);
    f->known_flags |= flg;
    if(state)
      f->flag_values |= flg;
    else
      f->flag_values &= ~flg;
    if(msg) {
      if(state)
        msg->flags |= flg;
      else
        msg->flags &= ~flg;
    }
  }
  if(h->flags_cb)
    h->flags_cb(msgcnt, seqno, h->flags_arg);

  cmd = g_strdup_printf("Store %s %cFlags.Silent (%s)", seq,
                        state ? '+' : '-', str);
  g_free(str);
  g_free(seq);
  return cmd;
}

ImapResponse
imap_mbox_store_flag(ImapMboxHandle *h, unsigned msgcnt, unsigned*seqno,
                     ImapMsgFlag flg, gboolean state)
{
  ImapResponse res;
  gchar* cmd;

  g_mutex_lock(&h->mutex);
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  cmd = imap_store_prepare(h, msgcnt, seqno, flg, state);
  if(cmd) {
    res = imap_cmd_exec(h, cmd);
    g_free(cmd);
  } else {
    res = IMR_OK; /* No action needs to be taken */
  }
  g_mutex_unlock(&h->mutex);
  return res;
}

ImapResponse
imap_mbox_store_flag_a(ImapMboxHandle *h, unsigned msgcnt, unsigned*seqno,
		       ImapMsgFlag flg, gboolean state)
{
  gchar* cmd;
  ImapResponse rc;

  g_mutex_lock(&h->mutex);
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  cmd = imap_store_prepare(h, msgcnt, seqno, flg, state);
  if(cmd) {
    unsigned res = imap_cmd_issue(h, cmd);
    g_free(cmd);
    rc = res != 0 ? IMR_OK : IMR_NO;
  } else /* no action to be done, perhaps message has the flag set already? */
    rc = IMR_OK;
  
  g_mutex_unlock(&h->mutex);
  return rc;
}


/* 6.4.7 COPY Command */
/** imap_mbox_handle_copy() copies given set of seqno from the mailbox
    selected in handle to given mailbox on same server. */
ImapResponse
imap_mbox_handle_copy(ImapMboxHandle* handle, unsigned cnt, unsigned *seqno,
                      const gchar *dest,
		      ImapSequence *ret_sequence)
{
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  {
    gchar *mbx7 = imap_utf8_to_mailbox(dest);
    char *seq = imap_coalesce_set(cnt, seqno);
    gchar *cmd = g_strdup_printf("COPY %s \"%s\"", seq, mbx7);
    unsigned cmdno;
    gboolean use_uidplus = imap_mbox_handle_can_do(handle, IMCAP_UIDPLUS);

    if(ret_sequence) {
      ret_sequence->ranges = NULL;
      handle->uidplus.store_response = 1;
    } else
      handle->uidplus.store_response = 0;

    rc = imap_cmd_exec_cmdno(handle, cmd, &cmdno);
    g_free(seq); g_free(mbx7); g_free(cmd);
    if(use_uidplus && ret_sequence) {
      if(rc == IMR_OK /* && cmdno == handle->uidplus.cmdno */ ) {
	ret_sequence->uid_validity = handle->uidplus.dst_uid_validity;
	ret_sequence->ranges = g_list_reverse(handle->uidplus.dst);
      } else {
	g_list_free(handle->uidplus.dst);
      }
      handle->uidplus.dst = NULL;
      handle->uidplus.store_response = 0;
    }
  }
  g_mutex_unlock(&handle->mutex);
  return rc;
}

/* 6.4.8 UID Command */
/* FIXME: implement */
/* implemented as alternatives of the commands */


/* 6.5 Client Commands - Experimental/Expansion */
/* 6.5.1 X<atom> Command */
ImapResponse
imap_mbox_scan(ImapMboxHandle *handle, const char*what, const char*str)
{
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  if(!imap_mbox_handle_can_do(handle, IMCAP_SCAN)) return IMR_NO;
  {
  gchar * cmd = g_strdup_printf("SCAN \"%s\" \"*\" \"%s\"",what, str);
  ImapResponse rc = imap_cmd_exec(handle, cmd);  
  g_free(cmd);
  return rc;
  }
}

/* imap_mbox_unselect()'s main purpose is to stop the notification
   signals from being triggered. On the other hand, all the precached
   data is lost on UNSELECT so the gain from using UNSELECT can be
   questioned.
*/
ImapResponse
imap_mbox_unselect(ImapMboxHandle *h)
{
  ImapResponse rc;

  g_mutex_lock(&h->mutex);
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  if(imap_mbox_handle_can_do(h, IMCAP_UNSELECT)) {
    rc = imap_cmd_exec(h, "UNSELECT");  
    if(rc == IMR_OK)
      h->state = IMHS_AUTHENTICATED;
  } else 
    rc = IMR_OK;

  g_mutex_unlock(&h->mutex);
  return rc;
}

/*
 * THREAD Command
 * see draft-ietf-imapext-thread-12.txt
 */
ImapResponse
imap_mbox_thread(ImapMboxHandle *h, const char *how, ImapSearchKey *filter)
{
  ImapResponse rc = IMR_NO;

  g_mutex_lock(&h->mutex);
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);

  if(imap_mbox_handle_can_do(h, IMCAP_THREAD_REFERENCES)) {
    int can_do_literals = imap_mbox_handle_can_do(h, IMCAP_LITERAL);
    unsigned cmdno;
    ImapCmdTag tag;

    cmdno = imap_make_tag(tag);
    
    if (!imap_handle_idle_disable(h)) { rc = IMR_SEVERED; goto exit_cleanup; }
    sio_printf(h->sio, "%s THREAD %s UTF-8 ", tag, how);
    if(!filter)
      sio_write(h->sio, "ALL", 3);
    else {
      if( (rc = imap_write_key(h, filter, cmdno, can_do_literals)) != IMR_OK)
        return rc;
    }

    net_client_siobuf_flush(h->sio, NULL);
    rc = imap_cmd_process_untagged(h, cmdno);
    imap_handle_idle_enable(h, 30);
  }
 exit_cleanup:
  g_mutex_unlock(&h->mutex);

  return rc;
}


ImapResponse
imap_mbox_uid_search(ImapMboxHandle *handle, ImapSearchKey *key,
                     void (*cb)(unsigned uid, void *),
                     void *cb_data)
{
  /* FIXME: Implement me! */
  return IMR_BAD;
}

static const char*
sort_code_to_string(ImapSortKey key)
{
  const char *keystr;
  switch(key) {
  case IMSO_ARRIVAL: keystr = "Arrival"; break;
  case IMSO_CC:      keystr = "Cc";      break;
  case IMSO_DATE:    keystr = "Date";    break;
  case IMSO_FROM:    keystr = "From";    break;
  case IMSO_SIZE:    keystr = "Size";    break;
  case IMSO_SUBJECT: keystr = "Subject"; break;
  default:
  case IMSO_TO:      keystr = "To";      break;
  }
  return keystr;
}

/** executes server side sort. The @param msgno array contains @param
    cnt message numbers. It is subseqently replaced with a new,
    altered order */
static ImapResponse
imap_mbox_sort_msgno_srv(ImapMboxHandle *handle, ImapSortKey key,
                         int ascending, unsigned int *msgno, unsigned cnt)
{
  ImapResponse rc;
  const char *keystr;
  char *seq, *cmd, *cmd1;

  /* IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD); */
  if(!imap_mbox_handle_can_do(handle, IMCAP_SORT)) 
    return IMR_NO;

  /* seq can be pretty long and g_strdup_printf has a limit on
   * string length so we create the command string in two steps. */
  keystr = sort_code_to_string(key);
  seq = imap_coalesce_set(cnt, msgno);
  cmd= g_strdup_printf("SORT (%s%s) UTF-8 ", 
                       ascending ? "" : "REVERSE ",
                       keystr);
  cmd1 = g_strconcat(cmd, seq, NULL);
  g_free(seq);
  g_free(cmd);
  handle->mbox_view.entries = 0; /* FIXME: I do not like this! 
                                  * we should not be doing such 
                                  * low level manipulations here */
  rc = imap_cmd_exec(handle, cmd1);
  g_free(cmd1);

  if(rc == IMR_OK) {
    unsigned i;
  /* one version of dovecot (which?) returned insufficient number of
   * numbers, we need to work around it. */
    if(handle->mbox_view.entries == cnt)
      for(i=0; i<cnt; i++)
        msgno[i] = handle->mbox_view.arr[i];
    else {
      for(i=0; i<cnt; i++)
        msgno[i] = i + 1;
      imap_mbox_handle_set_msg(handle,
                               _("Bug in implementation of SORT command on "
                               "IMAP server exposed."));
      rc = IMR_NO;
    }
  }

  return rc;
}

static int
comp_unsigned(const void *a, const void *b)
{
  return ((const unsigned*)a) - ((const unsigned*)b);
}

static int
comp_imap_address(const ImapAddress *a, const ImapAddress *b)
{
  if(a && a->name) {
    if(b && b->name)
      return g_ascii_strcasecmp(a->name, b->name);
    else return 1;
  } else
    return -1;
}

struct SortItem {
  ImapMessage *msg;
  unsigned no;
};
static int
comp_arrival(const void *a, const void *b)
{
  const ImapMessage *x = ((const struct SortItem*)a)->msg;
  const ImapMessage *y = ((const struct SortItem*)b)->msg;
  return x->internal_date - y->internal_date;
}
static int
comp_cc(const void *a, const void *b)
{
  const ImapMessage *x = ((const struct SortItem*)a)->msg;
  const ImapMessage *y = ((const struct SortItem*)b)->msg;
  return comp_imap_address(x->envelope->cc,  y->envelope->cc);
}
static int
comp_date(const void *a, const void *b)
{
  const ImapMessage *x = ((const struct SortItem*)a)->msg;
  const ImapMessage *y = ((const struct SortItem*)b)->msg;
  return x->envelope->date - y->envelope->date;
}
static int
comp_from(const void *a, const void *b)
{
  const ImapMessage *x = ((const struct SortItem*)a)->msg;
  const ImapMessage *y = ((const struct SortItem*)b)->msg;
  return comp_imap_address(x->envelope->from,  y->envelope->from);
}
static int
comp_size(const void *a, const void *b)
{
  const ImapMessage *x = ((const struct SortItem*)a)->msg;
  const ImapMessage *y = ((const struct SortItem*)b)->msg;
  return x->rfc822size - y->rfc822size;
}
static int
comp_subject(const void *a, const void *b)
{
  const ImapMessage *x = ((const struct SortItem*)a)->msg;
  const ImapMessage *y = ((const struct SortItem*)b)->msg;
  if(!x->envelope->subject) return -1;
  if(!y->envelope->subject) return  1;
  return g_ascii_strcasecmp(x->envelope->subject, y->envelope->subject);
}
static int
comp_to(const void *a, const void *b)
{
  const ImapMessage *x = ((const struct SortItem*)a)->msg;
  const ImapMessage *y = ((const struct SortItem*)b)->msg;
  return comp_imap_address(x->envelope->to,  y->envelope->to);
}

static ImapResponse
imap_mbox_sort_msgno_client(ImapMboxHandle *handle, ImapSortKey key,
                            int ascending, unsigned int *msgno, unsigned cnt)
{
  /* FIXME: we possibly do not need content type but I am not sure
   * about all the impliciations... */
  static const ImapFetchType fetch_type 
    = IMFETCH_UID | IMFETCH_ENV | IMFETCH_RFC822SIZE | IMFETCH_CONTENT_TYPE;
  int (*sortfun)(const void *a, const void *b);
    
  unsigned i, fetch_cnt, *seqno_to_fetch;
  struct SortItem *sort_items;

  if(key == IMSO_MSGNO) {
    g_warning("IMSO_MSGNO not yet implemented.");
    return IMR_NO;
  }
  seqno_to_fetch = g_new(unsigned, cnt);
  for(i=fetch_cnt=0; i<cnt; i++) {
    ImapMessage *imsg = imap_mbox_handle_get_msg(handle, msgno[i]);
    if(!imsg || !imsg->envelope)
      seqno_to_fetch[fetch_cnt++] = msgno[i];
  }

  if(fetch_cnt>0) {
    ImapResponse rc;
    qsort(seqno_to_fetch, fetch_cnt, sizeof(unsigned), comp_unsigned);
    g_debug("Should the client side sorting code "
           "be sorry about your bandwidth usage?");
    rc = imap_mbox_handle_fetch_set_unlocked(handle, seqno_to_fetch,
					     fetch_cnt, fetch_type);
    if(rc != IMR_OK)
      return rc;
  }
  g_free(seqno_to_fetch);

  sort_items = g_new(struct SortItem, cnt);
  for(i=0; i<cnt; i++) {
    sort_items[i].msg = imap_mbox_handle_get_msg(handle, msgno[i]);
    if ((sort_items[i].msg == NULL) || (sort_items[i].msg->envelope == NULL)) {
      g_free(sort_items);
      return IMR_BAD;
    }
    sort_items[i].no  = msgno[i];
  }
  switch(key) {
  default:
  case IMSO_ARRIVAL: sortfun = comp_arrival; break;
  case IMSO_CC:      sortfun = comp_cc;      break;
  case IMSO_DATE:    sortfun = comp_date;    break;
  case IMSO_FROM:    sortfun = comp_from;    break;
  case IMSO_SIZE:    sortfun = comp_size;    break;
  case IMSO_SUBJECT: sortfun = comp_subject; break;
  case IMSO_TO:      sortfun = comp_to;      break;
  }
  qsort(sort_items, cnt, sizeof(struct SortItem), sortfun);
  if(ascending)
    for(i=0; i<cnt; i++)
      msgno[i] = sort_items[i].no;
  else
    for(i=0; i<cnt; i++)
      msgno[i] = sort_items[cnt-i-1].no;


  g_free(sort_items);
  return IMR_OK;
}

ImapResponse
imap_mbox_sort_msgno(ImapMboxHandle *handle, ImapSortKey key,
                     int ascending, unsigned int *msgno, unsigned cnt)
{
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  if(imap_mbox_handle_can_do(handle, IMCAP_SORT)) 
    rc = imap_mbox_sort_msgno_srv(handle, key, ascending, msgno, cnt);
  else {
    rc = handle->enable_client_sort
      ? imap_mbox_sort_msgno_client(handle, key, ascending, msgno, cnt)
      : IMR_NO;
  }
  g_mutex_unlock(&handle->mutex);
  return rc;
}

static void
append_no(ImapMboxHandle *handle, unsigned seqno, void *arg)
{
  mbox_view_append_no(&handle->mbox_view, seqno);
}

/** selects a subset of messages specified by given filter and sorts
    it according to specified order. There are four cases possible,
    depending whether filtering needs to be done and whether the SORT
    extension is available.
    CASE 1a: filtered msgno sort.
    CASE 1b: full     msgno sort.
    CASE 2a: other sort when SORT extension is present.
    CASE 2b: other sort without SORT extension.
*/
ImapResponse
imap_mbox_sort_filter(ImapMboxHandle *handle, ImapSortKey key, int ascending,
                      ImapSearchKey *filter)
{
  ImapResponse rc;
  unsigned i;


  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  if(key == IMSO_MSGNO) {
    if(filter) { /* CASE 1a */
      handle->mbox_view.entries = 0; /* FIXME: I do not like this! 
                                      * we should not be doing such 
                                      * low level manipulations here */
      rc = imap_search_exec_unlocked(handle, FALSE, filter, append_no, NULL);
    } else { /* CASE 1b */
      if(handle->thread_root)
        g_node_destroy(handle->thread_root);
      handle->thread_root = g_node_new(NULL);
      
      for(i=1; i<=handle->exists; i++) {
        if(ascending)
          g_node_append_data(handle->thread_root,
                             GUINT_TO_POINTER(i));
        else
          g_node_prepend_data(handle->thread_root,
                              GUINT_TO_POINTER(i));
      }
      rc = IMR_OK;
    }
  } else { 
    if(imap_mbox_handle_can_do(handle, IMCAP_SORT)) { /* CASE 2a */
      unsigned cmdno;
      const char *keystr;
      int can_do_literals =
        imap_mbox_handle_can_do(handle, IMCAP_LITERAL);
      ImapCmdTag tag;

      cmdno =  imap_make_tag(tag);
      keystr = sort_code_to_string(key);
      if (!imap_handle_idle_disable(handle)) { rc = IMR_SEVERED; goto cleanup; }
      sio_printf(handle->sio, "%s SORT (%s%s) UTF-8 ", tag,
                 ascending ? "" : "REVERSE ", keystr);

      handle->mbox_view.entries = 0; /* FIXME: I do not like this! 
                                      * we should not be doing such 
                                      * low level manipulations here */
      if(!filter)
        sio_write(handle->sio, "ALL", 3);
      else {
        if( (rc=imap_write_key(handle, filter, cmdno, can_do_literals))
            != IMR_OK)
          return rc;
      }
      imap_handle_idle_enable(handle, 30);
      net_client_siobuf_flush(handle->sio, NULL);
      rc = imap_cmd_process_untagged(handle, cmdno);
    } else {                                           /* CASE 2b */
      /* try client-side sorting... */
      if(handle->enable_client_sort) {
	handle->mbox_view.entries = 0; /* FIXME: I do not like this! 
					* we should not be doing such 
					* low level manipulations here */
	if(filter)
	  rc = imap_search_exec_unlocked(handle, FALSE, filter,
					 append_no, NULL);
	else {
	  rc = IMR_OK;
	  for(i=0; i<handle->exists; i++)
	    mbox_view_append_no(&handle->mbox_view, i+1);
	}
	if(rc == IMR_OK)
	  rc = imap_mbox_sort_msgno_client(handle, key, ascending,
					   handle->mbox_view.arr,
					   handle->mbox_view.entries);
      } else rc = IMR_NO;
    }
  }
  if(rc == IMR_OK) {
    if(handle->thread_root)
      g_node_destroy(handle->thread_root);
    handle->thread_root = g_node_new(NULL);
      
    for(i=0; i<handle->mbox_view.entries; i++)
      g_node_append_data(handle->thread_root,
                         GUINT_TO_POINTER(handle->mbox_view.arr[i]));
  }
 cleanup:
  g_mutex_unlock(&handle->mutex);

  return rc;
}

static void
make_msgno_table(ImapMboxHandle*handle, unsigned seqno, GHashTable *msgnos)
{
  g_hash_table_insert(msgnos, GUINT_TO_POINTER(seqno),
                      GUINT_TO_POINTER(seqno));
}

/** Appends to given hash the message numbers that match specified
    filter. */
ImapResponse
imap_mbox_filter_msgnos(ImapMboxHandle *handle, ImapSearchKey *filter,
			GHashTable *msgnos)
{
  ImapResponse res;
  g_mutex_lock(&handle->mutex);
  res = imap_search_exec_unlocked(handle, FALSE, filter,
				  (ImapSearchCb)make_msgno_table, msgnos);
  g_mutex_unlock(&handle->mutex);
  return res;
}

/** Helper function for imap_mbox_complete_msgids. Tells whether
    msg-id for a specific message needs to be fetched.
*/
static unsigned
need_msgid(unsigned seqno, GPtrArray* msgids)
{
  return g_ptr_array_index(msgids, seqno-1) ? 0 : seqno;
}

static void
msgid_cb(unsigned seqno, ImapFetchBodyType body_type,
	 const char *buf, size_t buflen, void* arg)
{
  GPtrArray *arr = (GPtrArray*)arg;
  g_return_if_fail(seqno>=1 && seqno<=arr->len);
  g_ptr_array_index(arr, seqno-1) = g_strdup(buf);
}

/** Fills in provided array with msg-ids, starting at specified message.
 *  @returns IMAP response code (OK, NO, etc).
 * @param h the connection handle.
 * @param msgids is the array of message ids.
 * @param first_seqno_to_fetch the number of the first message to be
 * fetched.
 */
ImapResponse
imap_mbox_complete_msgids(ImapMboxHandle *h,
			  GPtrArray *msgids,
			  unsigned first_seqno_to_fetch)
{
  gchar *seq, *cmd;
  ImapResponse rc = IMR_OK;
  ImapFetchBodyInternalCb cb;
  void *arg;

  g_mutex_lock(&h->mutex);
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  seq = imap_coalesce_seq_range(first_seqno_to_fetch, msgids->len,
				(ImapCoalesceFunc)need_msgid, msgids);
  if(seq) {
    cmd = g_strdup_printf("FETCH %s BODY.PEEK[HEADER.FIELDS (message-id)]",
			  seq);
    g_free(seq);

    cb = h->body_cb;   h->body_cb = msgid_cb;
    arg = h->body_arg; h->body_arg = msgids;
    rc = imap_cmd_exec(h, cmd);
    h->body_cb  = cb;
    h->body_arg = arg;
    g_free(cmd);
  }

  g_mutex_unlock(&h->mutex);
  return rc;
}

/* RFC 2087, sect. 4.3: GETQUOTAROOT */
ImapResponse
imap_mbox_get_quota(ImapMboxHandle* handle, const char* mbox,
                    gulong* max, gulong* used)
{
  if (!imap_mbox_handle_can_do(handle, IMCAP_QUOTA))
    return IMR_NO;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE2(handle, IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("GETQUOTAROOT \"%s\"", mbx7);
  ImapResponse rc;

  rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  *max = handle->quota_max_k;
  *used = handle->quota_used_k;
  g_mutex_unlock(&handle->mutex);
  return rc;
  }
}

ImapResponse
imap_mbox_fetch_my_rights_unlocked(ImapMboxHandle* handle)
{
  if (imap_mbox_handle_can_do(handle, IMCAP_ACL)) {
    gchar *mbx7 = imap_utf8_to_mailbox(handle->mbox);
    gchar* cmd = g_strdup_printf("MYRIGHTS \"%s\"", mbx7);
    ImapResponse rc = imap_cmd_exec(handle, cmd);
    g_free(mbx7); g_free(cmd);
    return rc;
  } else {
    return IMR_NO;
  }
}

/** get myrights for the currently selected mailbox.
    RFC 4314, sect. 3.5: MYRIGHTS */
ImapResponse
imap_mbox_get_my_rights(ImapMboxHandle* handle, ImapAclType* my_rights,
			gboolean force_update)
{
  ImapResponse rc;

  g_mutex_lock(&handle->mutex);
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);

  if (force_update || !handle->has_rights) {
    rc = imap_mbox_fetch_my_rights_unlocked(handle);
  } else {
    rc = IMR_OK;
  }

  *my_rights = handle->rights;
  
  g_mutex_unlock(&handle->mutex);
  return rc;
}

/* RFC 4314, sect. 3.3: GETACL */
ImapResponse
imap_mbox_get_acl(ImapMboxHandle* handle, const char* mbox, GList** acls)
{
  if (!imap_mbox_handle_can_do(handle, IMCAP_ACL))
    return IMR_NO;

  g_mutex_lock(&handle->mutex);

  IMAP_REQUIRED_STATE2(handle, IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("GETACL \"%s\"", mbx7);
  ImapResponse rc;

  rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  g_list_foreach(*acls, (GFunc)imap_user_acl_free, NULL);
  g_list_free(*acls);
  *acls = handle->acls;
  handle->acls = NULL;
  g_mutex_unlock(&handle->mutex);
  return rc;
  }
}
