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
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <string.h>
#include "imap-handle.h"
#include "imap-commands.h"
#include "imap_private.h"
#include "siobuf.h"
#include "util.h"

#define ELEMENTS(x) (sizeof (x) / sizeof(x[0]))

typedef unsigned (*CoalesceFunc)(int, void*);
static gchar*
coalesce_seq_range(int lo, int hi, CoalesceFunc incl, void *data)
{
  GString * res = g_string_sized_new(16);
  char buf[10], *str;
  enum { BEGIN, LASTOUT, LASTIN, RANGE } mode = BEGIN;
  int seq;
  unsigned prev =0, num = 0;

  for(seq=lo; seq<=hi+1; seq++) {
    if(seq<=hi && (num=incl(seq, data)) != 0) {
      switch(mode) {
      case BEGIN: 
        sprintf(buf, "%u", num); g_string_append(res, buf); 
        mode = LASTIN; break;
      case RANGE:
        if(num!=prev+1) {
          sprintf(buf, ":%u,%u", prev, num); g_string_append(res, buf); 
          mode = LASTIN;
        }
        break;
      case LASTIN: 
        if(num==prev+1) {
          mode = RANGE;
          break;
        } /* else fall through */
      case LASTOUT: 
        sprintf(buf, ",%u", num); g_string_append(res, buf); 
        mode = LASTIN; break;
      }
    } else {
      switch(mode) {
      case BEGIN:
      case LASTOUT: break;
      case LASTIN: mode = LASTOUT; break;
      case RANGE: 
        sprintf(buf, ":%u", prev); g_string_append(res, buf); 
        mode = LASTOUT;
        break;
      }
    }
    prev = num;
  }
  if(mode == BEGIN) {
    str = NULL;
    g_string_free(res, TRUE);
  } else {
    str = res->str;
    g_string_free(res, FALSE);
  }
  return str;
}

static unsigned
simple_coealesce_func(int i, unsigned msgno[])
{
  return msgno[i];
}


struct fetch_data {
  ImapMboxHandle* h;
  ImapFetchType ift;
};
static unsigned
need_fetch(unsigned seqno, struct fetch_data* fd)
{
  g_return_val_if_fail(seqno>=1 && seqno<=fd->h->exists, 0);
  if(fd->h->msg_cache[seqno-1] == NULL) return seqno;
  if( (fd->ift & IMFETCH_ENV) 
      && fd->h->msg_cache[seqno-1]->envelope == NULL) return seqno;
  if( (fd->ift & IMFETCH_BODYSTRUCT) 
      && fd->h->msg_cache[seqno-1]->body == NULL) return seqno;
  if( (fd->ift & IMFETCH_RFC822SIZE) 
      && fd->h->msg_cache[seqno-1]->rfc822size <0) return seqno;
  if( (fd->ift & IMFETCH_CONTENT_TYPE) 
      && fd->h->msg_cache[seqno-1]->fetched_header_fields == NULL) return seqno;
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
/* imap_check_capability: make sure we can log in to this server. */
static gboolean
imap_check_capability(ImapMboxHandle* handle)
{
  if (imap_cmd_exec(handle, "CAPABILITY") != 0)
    return FALSE;

  if (!(imap_mbox_handle_can_do(handle, IMCAP_IMAP4) ||
        imap_mbox_handle_can_do(handle, IMCAP_IMAP4REV1))) {
    g_warning("IMAP4rev1 required but not provided.\n");
    return FALSE;
  }  
  return TRUE;
}

int
imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap)
{
  /* perhaps it already has capabilities? */
  if(!handle->has_capabilities)
    imap_check_capability(handle);

  if(cap>=0 && cap<IMCAP_MAX)
    return handle->capabilities[cap];
  else return 0;
}



/* 6.1.2 NOOP Command */
ImapResponse
imap_mbox_handle_noop(ImapMboxHandle *handle)
{
  return imap_cmd_exec(handle, "NOOP");
}

/* 6.1.3 LOGOUT Command */
/* unref handle to logout */


/* 6.2 Client Commands - Non-Authenticated State */


/* 6.2.1 AUTHENTICATE Command */
/* for CRAM methode implemented in auth-cram.c */


/* 6.2.2 LOGIN Command */
/* implemented in imap-auth.c */


/* 6.3 Client Commands - Authenticated State */


/* 6.3.1 SELECT Command */
ImapResponse
imap_mbox_select(ImapMboxHandle* handle, const char *mbox,
                 gboolean *readonly_mbox)
{
  gchar* cmd, *mbx7;
  ImapResponse rc;

  if (handle->state == IMHS_SELECTED && strcmp(handle->mbox, mbox) == 0)
      return IMR_OK;

  imap_mbox_resize_cache(handle, 0);
  mbox_view_dispose(&handle->mbox_view);
  handle->unseen = 0;

  mbx7 = imap_utf8_to_mailbox(mbox);

  cmd = g_strdup_printf("SELECT \"%s\"", mbx7);
  g_free(mbx7);
  rc= imap_cmd_exec(handle, cmd);
  g_free(cmd);
  if(rc == IMR_OK) {
    g_free(handle->mbox);
    handle->mbox = g_strdup(mbox);
    handle->state = IMHS_SELECTED;
    if(readonly_mbox)
      *readonly_mbox = handle->readonly_mbox;
  } else { /* remove even traces of untagged responses */
    imap_mbox_resize_cache(handle, 0);
    mbox_view_dispose(&handle->mbox_view);
  }
  return rc;
}


/* 6.3.2 EXAMINE Command */
ImapResponse
imap_mbox_examine(ImapMboxHandle* handle, const char* mbox)
{
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("EXAMINE \"%s\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  if(rc == IMR_OK) {
    g_free(handle->mbox);
    handle->mbox = g_strdup(mbox);
    handle->state = IMHS_SELECTED;
  } 
  return rc;
}


/* 6.3.3 CREATE Command */
ImapResponse
imap_mbox_create(ImapMboxHandle* handle, const char* mbox)
{
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("CREATE \"%s\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  return rc;
}


/* 6.3.4 DELETE Command */
ImapResponse
imap_mbox_delete(ImapMboxHandle* handle, const char* mbox)
{
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("DELETE \"%s\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
}


/* 6.3.5 RENAME Command */
ImapResponse
imap_mbox_rename(ImapMboxHandle* handle,
		 const char* old_mbox,
		 const char* new_mbox)
{
  gchar *mbx7o = imap_utf8_to_mailbox(old_mbox);
  gchar *mbx7n = imap_utf8_to_mailbox(old_mbox);

  gchar* cmd = g_strdup_printf("RENAME \"%s\" \"%s\"", mbx7o, mbx7n);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
}


/* 6.3.6 SUBSCRIBE Command */
/* 6.3.7 UNSUBSCRIBE Command */
ImapResponse
imap_mbox_subscribe(ImapMboxHandle* handle,
		    const char* mbox, gboolean subscribe)
{
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("%s \"%s\"",
			       subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE",
			       mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  return rc;
}


/* 6.3.8 LIST Command */
ImapResponse
imap_mbox_list(ImapMboxHandle *handle, const char* what)
{
  gchar *mbx7 = imap_utf8_to_mailbox(what);
  gchar *cmd = g_strdup_printf("LIST \"%s\" \"%%\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
}


/* 6.3.9 LSUB Command */
ImapResponse
imap_mbox_lsub(ImapMboxHandle *handle, const char* what)
{
  gchar *mbx7 = imap_utf8_to_mailbox(what);
  gchar *cmd = g_strdup_printf("LSUB \"%s\" \"%%\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  return rc;
}


/* 6.3.10 STATUS Command */
/* FIXME: implement */


/* 6.3.11 APPEND Command */
static gchar*
enum_flag_to_str(ImapMsgFlags flg)
{
  GString *flags_str = g_string_new("");
  unsigned idx;

  for(idx=0; idx < ELEMENTS(msg_flags); idx++) {
    if((flg & (1<<idx)) == 0) continue;
    if(*flags_str->str) g_string_append_c(flags_str, ' ');
    g_string_append_c(flags_str, '\\');
    g_string_append(flags_str, msg_flags[idx]);
  }
  return g_string_free(flags_str, FALSE);
}

ImapResponse
imap_mbox_append(ImapMboxHandle *handle, const char *mbox,
                 ImapMsgFlags flags, size_t sz,
                 ImapAppendFunc dump_cb, void *arg)
{
  int use_literal = imap_mbox_handle_can_do(handle, IMCAP_LITERAL);
  unsigned cmdno;
  ImapResponse rc;
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar *cmd;
  char *litstr = use_literal ? "+" : "";
  char buf[4096];
  size_t s, delta;
  int c;

  if(flags) {
    gchar *str = enum_flag_to_str(flags);
    cmd = g_strdup_printf("APPEND \"%s\" (%s) {%u%s}", mbx7, str, sz, litstr);
    g_free(str);
  } else 
    cmd = g_strdup_printf("APPEND \"%s\" {%u%s}", mbx7, sz, litstr);

  c = imap_cmd_start(handle, cmd, &cmdno);
  g_free(mbx7); g_free(cmd);
  if (c<0) /* irrecoverable connection error. */
    return IMR_SEVERED;
  if(use_literal)
    rc = IMR_RESPOND; /* we do it without flushing */
  else {
    sio_flush(handle->sio);
    do {
      rc = imap_cmd_step (handle, cmdno);
    } while (rc == IMR_UNTAGGED);
    if(rc != IMR_RESPOND) return rc;
    while( (c=sio_getc(handle->sio)) != -1 && c != '\n');
  } 

  for(s=0; s<sz; s+= delta) {
    delta = dump_cb(buf, sizeof(buf), arg);
    if(s+delta>sz) delta = sz-s;
    sio_write(handle->sio, buf, delta);
  }
  
  /* It has been though observed that "Cyrus IMAP4 v2.0.16-p1
   * server" can hang if the flush isn't done under following conditions:
   * a). TLS is enabled, b). message contains NUL characters.  NUL
   * characters are forbidden (RFC3501, sect. 4.3.1) and we probably
   * should make sure on a higher level that they are not sent.
   */
  /* sio_flush(handle->sio); */
  sio_write(handle->sio, "\r\n", 2);
  sio_flush(handle->sio);
  do {
    rc = imap_cmd_step (handle, cmdno);
  } while (rc == IMR_UNTAGGED);

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
/* FIXME: implement */

/* 6.4.3 EXPUNGE Command */
ImapResponse
imap_mbox_expunge(ImapMboxHandle *handle)
{
  return imap_cmd_exec(handle, "EXPUNGE");
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

  filter_str = mbox_view_get_str(&h->mbox_view);
  cmd = g_strdup_printf("SEARCH ALL (SUBJECT \"%s\"%s%s)", search_str,
			*filter_str ? " " : "", filter_str);
  rc = imap_mbox_find_helper(h, cmd, msgcnt, msgs);
  g_free(cmd);

  return rc;
}

ImapResponse
imap_mbox_find_unseen(ImapMboxHandle * h,
		      unsigned       * msgcnt,
		      unsigned      ** msgs)
{
  return imap_mbox_find_helper(h, "SEARCH UNSEEN", msgcnt, msgs);
}

/* 6.4.5 FETCH Command */
ImapResponse
imap_mbox_handle_fetch_range(ImapMboxHandle* handle,
                             unsigned lo, unsigned hi, ImapFetchType ift)
{
  gchar * seq;
  ImapResponse rc;
  unsigned exists = imap_mbox_handle_get_exists(handle);
  CoalesceFunc cf = (CoalesceFunc)(mbox_view_is_active(&handle->mbox_view)
                                   ? need_fetch_view : need_fetch);
  struct fetch_data fd;

  fd.h = handle; fd.ift = ift;
  
  if(lo>hi) return IMR_OK;
  if(lo<1) lo = 1;
  if(hi>exists) hi = exists;
  seq = coalesce_seq_range(lo, hi, cf, &fd);
  if(seq) {
    const char* hdr[8];
    int idx = 0;
    hdr[idx++] = "UID";
    if(ift & IMFETCH_ENV)        hdr[idx++] = "ENVELOPE";
    if(ift & IMFETCH_BODYSTRUCT) hdr[idx++] = "BODY";
    if(ift & IMFETCH_RFC822SIZE) hdr[idx++] = "RFC822.SIZE";
    if(ift & IMFETCH_HEADER_MASK) {
      hdr[idx++] = "BODY.PEEK[HEADER.FIELDS (";
      if(ift & IMFETCH_CONTENT_TYPE) hdr[idx++] = "CONTENT-TYPE";
      hdr[idx++] = ")]";
    }
    hdr[idx] = NULL;
    rc = imap_mbox_handle_fetch(handle, seq, hdr);
    g_free(seq);
  } else rc = IMR_OK;
  return rc;
}

ImapResponse
imap_mbox_handle_fetch_set(ImapMboxHandle* handle,
                           unsigned *set, unsigned cnt, ImapFetchType ift)
{
  gchar * seq;
  ImapResponse rc;
  CoalesceFunc cf;
  struct fetch_data_set fd;

  if(cnt == 0) return IMR_OK;

  fd.fd.h = handle; fd.fd.ift = ift;
  fd.set = set;
  cf = (CoalesceFunc)(mbox_view_is_active(&handle->mbox_view)
                      ? need_fetch_view_set : need_fetch_set);
  seq = coalesce_seq_range(1, cnt, cf, &fd);
  if(seq) {
    const char* hdr[8];
    int idx = 0;
    hdr[idx++] = "UID";
    if(ift & IMFETCH_ENV)        hdr[idx++] = "ENVELOPE";
    if(ift & IMFETCH_BODYSTRUCT) hdr[idx++] = "BODY";
    if(ift & IMFETCH_RFC822SIZE) hdr[idx++] = "RFC822.SIZE";
    if(ift & IMFETCH_HEADER_MASK) {
      hdr[idx++] = "BODY.PEEK[HEADER.FIELDS (";
      if(ift & IMFETCH_CONTENT_TYPE) hdr[idx++] = "CONTENT-TYPE";
      hdr[idx++] = ")]";
    }
    hdr[idx] = NULL;
    rc = imap_mbox_handle_fetch(handle, seq, hdr);
    g_free(seq);
  } else rc = IMR_OK;
  return rc;
}

static void
write_nstring(const char *str, int len, void *fl)
{
  fwrite(str, 1, len, (FILE*)fl);
}

ImapResponse
imap_mbox_handle_fetch_rfc822(ImapMboxHandle* handle, unsigned seqno, 
                              FILE *fl)
{
  char cmd[40];
  handle->body_cb  = write_nstring;
  handle->body_arg = fl;
  sprintf(cmd, "FETCH %u RFC822", seqno);
  return imap_cmd_exec(handle, cmd);
}

ImapResponse
imap_mbox_handle_fetch_rfc822_uid(ImapMboxHandle* handle, unsigned uid, 
                              FILE *fl)
{
  char cmd[40];
  handle->body_cb  = write_nstring;
  handle->body_arg = fl;
  sprintf(cmd, "UID FETCH %u RFC822", uid);
  return imap_cmd_exec(handle, cmd);
}

ImapResponse
imap_mbox_handle_fetch_body(ImapMboxHandle* handle, 
                            unsigned seqno, const char *section,
                            ImapFetchBodyCb body_cb, void *arg)
{
  char cmd[40];
  handle->body_cb  = body_cb;
  handle->body_arg = arg;
  snprintf(cmd, sizeof(cmd), "FETCH %u BODY[%s]", seqno, section);
  return imap_cmd_exec(handle, cmd);
}

ImapResponse
imap_mbox_handle_fetch_structure(ImapMboxHandle* handle, unsigned seqno)
{
  ImapResponse rc;
  ImapMessage *im = imap_mbox_handle_get_msg(handle, seqno);
  if(!im->body) {
    char* cmd = g_strdup_printf("FETCH %u BODYSTRUCTURE", seqno);
    rc = imap_cmd_exec(handle, cmd);
    g_free(cmd);
  } else rc = IMR_OK;
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

ImapResponse
imap_mbox_store_flag(ImapMboxHandle *h, unsigned msgcnt, unsigned*seqno,
                     ImapMsgFlag flg, gboolean state)
{
  ImapResponse res;
  gchar* cmd, *seq, *str;
  struct msg_set csd;
  unsigned i;

  csd.handle = h; csd.msgcnt = msgcnt; csd.seqno = seqno;
  csd.flag = flg; csd.state = state;
  if(msgcnt == 0) return IMR_OK;
  seq = coalesce_seq_range(0, msgcnt-1, (CoalesceFunc)cf_flag, &csd);
  if(!seq) return IMR_OK;
  str = enum_flag_to_str(flg);
  for(i=0; i<msgcnt; i++) {
    ImapMessage *msg = imap_mbox_handle_get_msg(h, seqno[i]);
    if(msg) {
      if(state)
        msg->flags |= flg;
      else
        msg->flags &= ~flg;
    }
    /* should we emit signals here on flag change?
     * What if Store fails below? The flags won't be set. */
  }
  cmd = g_strdup_printf("Store %s %cFlags.Silent (%s)", seq,
                        state ? '+' : '-', str);
  g_free(str);
  g_free(seq);
  res = imap_cmd_exec(h, cmd);
  g_free(cmd);
  return res;
}


/* 6.4.7 COPY Command */
/** imap_mbox_handle_copy() copies given set of seqno from the mailbox
    selected in handle to given mailbox on same server. */
ImapResponse
imap_mbox_handle_copy(ImapMboxHandle* handle, unsigned cnt, unsigned *seqno,
                      const gchar *dest)
{
  gchar *mbx7 = imap_utf8_to_mailbox(dest);
  char *seq = 
    coalesce_seq_range(0, cnt-1,(CoalesceFunc)simple_coealesce_func, seqno);
  gchar *cmd = g_strdup_printf("COPY %s \"%s\"", seq, mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(seq); g_free(mbx7); g_free(cmd);
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
  gchar * cmd = g_strdup_printf("SCAN \"%s\" \"*\" \"%s\"",what, str);
  ImapResponse rc = imap_cmd_exec(handle, cmd);  
  g_free(cmd);
  return rc;
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
  if(imap_mbox_handle_can_do(h, IMCAP_UNSELECT)) {
    rc = imap_cmd_exec(h, "UNSELECT");  
    if(rc == IMR_OK)
      h->state = IMHS_AUTHENTICATED;
  } else 
    rc = IMR_OK;
  return rc;
}

/*
 * THREAD Command
 * see draft-ietf-imapext-thread-12.txt
 */
ImapResponse
imap_mbox_thread(ImapMboxHandle *h, const char *how, ImapSearchKey *filter)
{
  if(imap_mbox_handle_can_do(h, IMCAP_THREAD_REFERENCES)) {
    int can_do_literals = imap_mbox_handle_can_do(h, IMCAP_LITERAL);
    unsigned cmdno;
    ImapResponse rc;
    ImapCmdTag tag;

    cmdno = imap_make_tag(tag);
    
    sio_printf(h->sio, "%s THREAD %s UTF-8 ", tag, how);
    if(!filter)
      sio_write(h->sio, "ALL", 3);
    else
      imap_write_key(h, filter, cmdno, can_do_literals);

    sio_write(h->sio, "\r\n", 2);
    imap_handle_flush(h);
    do
      rc = imap_cmd_step(h, cmdno);
    while(rc == IMR_UNTAGGED);
    return rc;
  } else
    return IMR_NO;
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

ImapResponse
imap_mbox_sort_msgno(ImapMboxHandle *handle, ImapSortKey key,
                     int ascending, int *msgno, unsigned cnt)
{
  ImapResponse rc;
  const char *keystr;
  char *seq, *cmd, *cmd1;
  unsigned i;

  if(!imap_mbox_handle_can_do(handle, IMCAP_SORT)) 
    return IMR_NO;

  /* seq can be pretty long and g_strdup_printf has a limit on
   * string length so we create the command string in two steps. */
  keystr = sort_code_to_string(key);
  seq = coalesce_seq_range(0, cnt-1,(CoalesceFunc)simple_coealesce_func,
                           msgno);
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

  if(rc == IMR_OK)
    for(i=0; i<cnt; i++)
      msgno[i] = handle->mbox_view.arr[i];

  return rc;
}

static void
append_no(ImapMboxHandle *handle, unsigned seqno, void *arg)
{
  mbox_view_append_no(&handle->mbox_view, seqno);
}

ImapResponse
imap_mbox_sort_filter(ImapMboxHandle *handle, ImapSortKey key, int ascending,
                      ImapSearchKey *filter)
{
  ImapResponse rc;
  const char *keystr;
  unsigned i;

  if(key == IMSO_MSGNO) {
    if(filter) {
      handle->mbox_view.entries = 0; /* FIXME: I do not like this! 
                                      * we should not be doing such 
                                      * low level manipulations here */
      rc = imap_search_exec(handle, filter, append_no, NULL);
    } else {
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
      return IMR_OK;
    }
  } else { /* Nontrivial (ie. not over msgno) sort requires an extension */
    unsigned cmdno;
    int can_do_literals =
      imap_mbox_handle_can_do(handle, IMCAP_LITERAL);
    ImapCmdTag tag;

    if(!imap_mbox_handle_can_do(handle, IMCAP_SORT)) 
      return IMR_NO;
    
    cmdno =  imap_make_tag(tag);
    keystr = sort_code_to_string(key);
    sio_printf(handle->sio, "%s SORT (%s%s) UTF-8 ", tag,
               ascending ? "" : "REVERSE ", keystr);
    
    handle->mbox_view.entries = 0; /* FIXME: I do not like this! 
                                    * we should not be doing such 
                                    * low level manipulations here */
    if(!filter)
      sio_write(handle->sio, "ALL", 3);
    else
      imap_write_key(handle, filter, cmdno, can_do_literals);
    sio_write(handle->sio, "\r\n", 2);
    imap_handle_flush(handle);
    do
      rc = imap_cmd_step(handle, cmdno);
    while(rc == IMR_UNTAGGED);
  }
  if(rc == IMR_OK) {
    if(handle->thread_root)
      g_node_destroy(handle->thread_root);
    handle->thread_root = g_node_new(NULL);
      
    for(i=0; i<handle->mbox_view.entries; i++)
      g_node_append_data(handle->thread_root,
                         GUINT_TO_POINTER(handle->mbox_view.arr[i]));
  }
  return rc;
}

static void
make_msgno_table(ImapMboxHandle*handle, unsigned seqno, GHashTable *msgnos)
{
  g_hash_table_insert(msgnos, (void *) seqno, (void *) seqno);
}

ImapResponse
imap_mbox_filter_msgnos(ImapMboxHandle *handle, ImapSearchKey *filter,
			GHashTable *msgnos)
{
  return imap_search_exec(handle, filter, (ImapSearchCb)make_msgno_table,
                          msgnos);
}
