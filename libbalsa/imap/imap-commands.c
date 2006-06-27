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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

static unsigned
simple_coealesce_func(int i, unsigned msgno[])
{
  return msgno[i];
}


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
  for(i=0; i<ELEMENTS(header); i++) {
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
/* imap_check_capability: make sure we can log in to this server. */
static gboolean
imap_check_capability(ImapMboxHandle* handle)
{
  IMAP_REQUIRED_STATE3(handle, IMHS_CONNECTED, IMHS_AUTHENTICATED,
                       IMHS_SELECTED, FALSE);
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
  if(cap == IMCAP_FETCHBODY)
    return handle->can_fetch_body;

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
  IMAP_REQUIRED_STATE3(handle, IMHS_CONNECTED, IMHS_AUTHENTICATED,
                       IMHS_SELECTED, IMR_BAD);
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


/* 6.3.1 SELECT Command
 * readonly_mbox can be NULL. */
ImapResponse
imap_mbox_select(ImapMboxHandle* handle, const char *mbox,
                 gboolean *readonly_mbox)
{
  gchar* cmd, *mbx7;
  ImapResponse rc;

  IMAP_REQUIRED_STATE3(handle, IMHS_CONNECTED, IMHS_AUTHENTICATED,
                       IMHS_SELECTED, IMR_BAD);
  HANDLE_LOCK(handle);
  if (handle->state == IMHS_SELECTED && strcmp(handle->mbox, mbox) == 0) {
    if(readonly_mbox)
      *readonly_mbox = handle->readonly_mbox;
    HANDLE_UNLOCK(handle);
    return IMR_OK;
  }
  imap_mbox_resize_cache(handle, 0);
  mbox_view_dispose(&handle->mbox_view);
  handle->unseen = 0;

  mbx7 = imap_utf8_to_mailbox(mbox);

  cmd = g_strdup_printf("SELECT \"%s\"", mbx7);
  g_free(mbx7);
  rc= imap_cmd_exec(handle, cmd);
  g_free(cmd);
  if(rc == IMR_OK) {
    if(handle->mbox != mbox) { /* we do not "reselect" */
      g_free(handle->mbox);
      handle->mbox = g_strdup(mbox);
    }
    handle->state = IMHS_SELECTED;
    if(readonly_mbox)
      *readonly_mbox = handle->readonly_mbox;
  } else { /* remove even traces of untagged responses */
    imap_mbox_resize_cache(handle, 0);
    mbox_view_dispose(&handle->mbox_view);
    g_signal_emit_by_name(G_OBJECT(handle), "exists-notify");
  }
  HANDLE_UNLOCK(handle);
  return rc;
}


/* 6.3.2 EXAMINE Command */
ImapResponse
imap_mbox_examine(ImapMboxHandle* handle, const char* mbox)
{
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
  } 
  return rc;
  }
}


/* 6.3.3 CREATE Command */
ImapResponse
imap_mbox_create(ImapMboxHandle* handle, const char* mbox)
{
  IMAP_REQUIRED_STATE2(handle, IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("CREATE \"%s\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  return rc;
  }
}


/* 6.3.4 DELETE Command */
ImapResponse
imap_mbox_delete(ImapMboxHandle* handle, const char* mbox)
{
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("DELETE \"%s\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
  }
}


/* 6.3.5 RENAME Command */
ImapResponse
imap_mbox_rename(ImapMboxHandle* handle,
		 const char* old_mbox,
		 const char* new_mbox)
{
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7o = imap_utf8_to_mailbox(old_mbox);
  gchar *mbx7n = imap_utf8_to_mailbox(new_mbox);

  gchar* cmd = g_strdup_printf("RENAME \"%s\" \"%s\"", mbx7o, mbx7n);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
  }
}


/* 6.3.6 SUBSCRIBE Command */
/* 6.3.7 UNSUBSCRIBE Command */
ImapResponse
imap_mbox_subscribe(ImapMboxHandle* handle,
		    const char* mbox, gboolean subscribe)
{
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7 = imap_utf8_to_mailbox(mbox);
  gchar* cmd = g_strdup_printf("%s \"%s\"",
			       subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE",
			       mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  return rc;
  }
}


/* 6.3.8 LIST Command */
ImapResponse
imap_mbox_list(ImapMboxHandle *handle, const char* what)
{
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7 = imap_utf8_to_mailbox(what);
  gchar *cmd = g_strdup_printf("LIST \"%s\" \"%%\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  g_free(mbx7);
  return rc;
  }
}


/* 6.3.9 LSUB Command */
ImapResponse
imap_mbox_lsub(ImapMboxHandle *handle, const char* what)
{
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7 = imap_utf8_to_mailbox(what);
  gchar *cmd = g_strdup_printf("LSUB \"%s\" \"%%\"", mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(mbx7); g_free(cmd);
  return rc;
  }
}


/* 6.3.10 STATUS Command */

ImapResponse
imap_mbox_status(ImapMboxHandle *r, const char*what,
                 struct ImapStatusResult *res)
{
  const char *item_arr[ELEMENTS(imap_status_item_names)+1];
  ImapResponse rc = IMR_OK;
  unsigned i, ipos;
  
  for(ipos = i= 0; res[i].item != IMSTAT_NONE; i++) {
    /* repeated items? */
    g_return_val_if_fail(i<ELEMENTS(imap_status_item_names), IMR_BAD);
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
    g_hash_table_insert(r->status_resps, (gpointer)what, res);
    rc = imap_cmd_exec(r, cmd);
    g_hash_table_remove(r->status_resps, what);
    g_free(mbx7); g_free(cmd);
    g_free(items);
  }
  return IMR_OK; 
}
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
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
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

  imap_handle_idle_disable(handle);
  if(flags) {
    gchar *str = enum_flag_to_str(flags);
    cmd = g_strdup_printf("APPEND \"%s\" (%s) {%u%s}", mbx7, str,
                          (unsigned)sz, litstr);
    g_free(str);
  } else 
    cmd = g_strdup_printf("APPEND \"%s\" {%u%s}", mbx7, (unsigned)sz, litstr);

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

  imap_handle_idle_enable(handle, 30);
  return rc;
  }
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
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);

  rc = imap_cmd_exec(h, "CLOSE");  
  if(rc == IMR_OK)
    h->state = IMHS_AUTHENTICATED;
  return rc;
}

/* 6.4.3 EXPUNGE Command */
ImapResponse
imap_mbox_expunge(ImapMboxHandle *handle)
{
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  return imap_cmd_exec(handle, "EXPUNGE");
}

ImapResponse
imap_mbox_expunge_a(ImapMboxHandle *handle)
{
  IMAP_REQUIRED_STATE2(handle,IMHS_AUTHENTICATED, IMHS_SELECTED, IMR_BAD);
  /* extra care would be required to use this once since no other
     commands that use sequence numbers can be issued before this one
     finishes... */
  return imap_cmd_issue(handle, "EXPUNGE");
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

  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
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

  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
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
  return imap_mbox_find_helper(h, "SEARCH UNSEEN UNDELETED", msgcnt, msgs);
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
   and hopes the output is not intermixed */
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

  if (h->state == IMHS_DISCONNECTED)
    return IMR_SEVERED;

  cb  = h->search_cb;  h->search_cb  = (ImapSearchCb)set_flag_cache_cb;
  arg = h->search_arg; 

  fnd.handle = h;
  for(shift=0; needed_flags>>shift; shift++) {
    const char *flg;
    if((needed_flags>>shift) & 1) {
      fnd.flag = 1<<shift;
      if(fnd.flag & IMSGF_SEEN)
        for(i=0; i<h->flag_cache->len; i++) {
          ImapFlagCache *f =
            &g_array_index(h->flag_cache, ImapFlagCache, i);
          if( (f->known_flags & fnd.flag) ==0)
            f->flag_values |= fnd.flag;
        }
      seqno = coalesce_seq_range(1, h->exists,
                                 (CoalesceFunc)flag_unknown, &fnd);
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
      if(!cmd) imap_handle_idle_disable(h);
      cmd = g_strdup_printf("SEARCH %s %s", seqno, flg);
      g_free(seqno);
      flag[issued_cmd] = fnd.flag;
      ics = imap_cmd_start(h, cmd, &cmdno[issued_cmd++]);
      g_free(cmd);
      if(ics<0)
        return IMR_SEVERED;  /* irrecoverable connection error. */
    }
  }
  if(cmd) { /* was ever altered */
    sio_flush(h->sio);
    for(i=0; i<issued_cmd; i++) {
      h->search_arg = &flag[i];
      do {
        rc = imap_cmd_step(h, cmdno[i]);
      } while (rc == IMR_UNTAGGED);
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

  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  flags = &g_array_index(h->flag_cache, ImapFlagCache, msgno-1);
  
  needed_flags = ~flags->known_flags & (flag_set | flag_unset);
  
  return 
    (!needed_flags||imap_assure_needed_flags(h, needed_flags) == IMR_OK) &&
    (flags->flag_values & flag_set) == flag_set &&
    (flags->flag_values & flag_unset) == 0;
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
  if(ift & IMFETCH_RFC822HEADERS) hdr[idx++] = "RFC822.HEADERS";
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
    default: g_warning("unexpected sequence %s\n", seq);
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
  unsigned exists = imap_mbox_handle_get_exists(handle);
  CoalesceFunc cf = (CoalesceFunc)(mbox_view_is_active(&handle->mbox_view)
                                   ? need_fetch_view : need_fetch);
  struct fetch_data fd;

  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  fd.h = handle; fd.ift = fd.req_fetch_type = ift;
  
  if(lo>hi) return IMR_OK;
  if(lo<1) lo = 1;
  if(hi>exists) hi = exists;
  seq = coalesce_seq_range(lo, hi, cf, &fd);
  if(seq) {
    const char* hdr[13];
    ic_construct_header_list(hdr, fd.req_fetch_type);
    HANDLE_LOCK(handle);
    rc = imap_mbox_handle_fetch(handle, seq, hdr);
    if(rc == IMR_OK) set_avail_headers(handle, seq, fd.req_fetch_type);
    HANDLE_UNLOCK(handle);
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

  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  if(cnt == 0) return IMR_OK;

  fd.fd.h = handle; fd.fd.ift = fd.fd.req_fetch_type = ift;
  fd.set = set;
  cf = (CoalesceFunc)(mbox_view_is_active(&handle->mbox_view)
                      ? need_fetch_view_set : need_fetch_set);
  seq = coalesce_seq_range(1, cnt, cf, &fd);
  if(seq) {
    const char* hdr[13];
    ic_construct_header_list(hdr, fd.fd.req_fetch_type);
    HANDLE_LOCK(handle);
    rc = imap_mbox_handle_fetch(handle, seq, hdr);
    if(rc == IMR_OK) set_avail_headers(handle, seq, fd.fd.req_fetch_type);
    HANDLE_UNLOCK(handle);
    g_free(seq);
  } else rc = IMR_OK;
  return rc;
}

static void
write_nstring(unsigned seqno, const char *str, int len, void *fl)
{
  if (fwrite(str, 1, len, (FILE*)fl) != (size_t) len)
    perror("write_nstring");
}

ImapResponse
imap_mbox_handle_fetch_rfc822(ImapMboxHandle* handle, unsigned seqno, 
                              FILE *fl)
{
  char cmd[40];
  ImapFetchBodyCb cb = handle->body_cb;
  void          *arg = handle->body_arg;
  ImapResponse rc;

  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  HANDLE_LOCK(handle);
  handle->body_cb  = write_nstring;
  handle->body_arg = fl;
  sprintf(cmd, "FETCH %u RFC822", seqno);
  rc = imap_cmd_exec(handle, cmd);
  handle->body_cb  = cb;
  handle->body_arg = arg;
  HANDLE_UNLOCK(handle);

  return rc;
}

ImapResponse
imap_mbox_handle_fetch_rfc822_uid(ImapMboxHandle* handle, unsigned uid, 
                              FILE *fl)
{
  char cmd[40];
  ImapFetchBodyCb cb = handle->body_cb;
  void          *arg = handle->body_arg;
  ImapResponse rc;

  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  HANDLE_LOCK(handle);
  handle->body_cb  = write_nstring;
  handle->body_arg = fl;
  sprintf(cmd, "UID FETCH %u RFC822", uid);
  rc = imap_cmd_exec(handle, cmd);
  handle->body_cb  = cb;
  handle->body_arg = arg;
  HANDLE_UNLOCK(handle);
  return rc;
}

ImapResponse
imap_mbox_handle_fetch_body(ImapMboxHandle* handle, 
                            unsigned seqno, const char *section,
                            ImapFetchBodyOptions options,
                            ImapFetchBodyCb body_cb, void *arg)
{
  char cmd[160];
  ImapMessage *msg;
  ImapFetchBodyCb fcb = handle->body_cb;
  void          *farg = handle->body_arg;
  ImapResponse rc;

  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  handle->body_cb  = body_cb;
  handle->body_arg = arg;
  msg = imap_mbox_handle_get_msg(handle, seqno);
  if(options == IMFB_NONE)
    snprintf(cmd, sizeof(cmd), "FETCH %u BODY[%s]",
             seqno, section);
  else {
    char prefix[160];
    if(options == IMFB_HEADER) {
      /* We have to strip last section part and replace it with HEADER */
      unsigned sz;
      char *last_dot = strrchr(section, '.');
      strncpy(prefix, section, sizeof(prefix));

      if(last_dot) {
        sz = last_dot-section+1;
        if(sz>sizeof(prefix)-1) sz = sizeof(prefix)-1;
      } else sz = 0;
      strncpy(prefix + sz, "HEADER", sizeof(prefix)-sz-1);
      prefix[sizeof(prefix)-1] = '\0';
    } else
      snprintf(prefix, sizeof(prefix), "%s.MIME", section);

    snprintf(cmd, sizeof(cmd), "FETCH %u (BODY[%s] BODY[%s])",
             seqno, prefix, section);
  }
  rc = imap_cmd_exec(handle, cmd);
  handle->body_cb  = fcb;
  handle->body_arg = farg;
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
  seq = coalesce_seq_range(0, msgcnt-1, (CoalesceFunc)cf_flag, &csd);
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

  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  cmd = imap_store_prepare(h, msgcnt, seqno, flg, state);
  if(cmd) {
    res = imap_cmd_exec(h, cmd);
    g_free(cmd);
    return res;
  } else return IMR_OK;
}

ImapResponse
imap_mbox_store_flag_a(ImapMboxHandle *h, unsigned msgcnt, unsigned*seqno,
		       ImapMsgFlag flg, gboolean state)
{
  gchar* cmd;

  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  cmd = imap_store_prepare(h, msgcnt, seqno, flg, state);
  if(cmd) {
    unsigned res = imap_cmd_issue(h, cmd);
    g_free(cmd);
    return res != 0 ? IMR_OK : IMR_NO;
  } else /* no action to be done, perhaps message has the flag set already? */
    return IMR_OK;
}


/* 6.4.7 COPY Command */
/** imap_mbox_handle_copy() copies given set of seqno from the mailbox
    selected in handle to given mailbox on same server. */
ImapResponse
imap_mbox_handle_copy(ImapMboxHandle* handle, unsigned cnt, unsigned *seqno,
                      const gchar *dest)
{
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  {
  gchar *mbx7 = imap_utf8_to_mailbox(dest);
  char *seq = 
    coalesce_seq_range(0, cnt-1,(CoalesceFunc)simple_coealesce_func, seqno);
  gchar *cmd = g_strdup_printf("COPY %s \"%s\"", seq, mbx7);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(seq); g_free(mbx7); g_free(cmd);
  return rc;
  }
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
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
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
  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  if(imap_mbox_handle_can_do(h, IMCAP_THREAD_REFERENCES)) {
    int can_do_literals = imap_mbox_handle_can_do(h, IMCAP_LITERAL);
    unsigned cmdno;
    ImapResponse rc;
    ImapCmdTag tag;

    cmdno = imap_make_tag(tag);
    
    imap_handle_idle_disable(h);
    sio_printf(h->sio, "%s THREAD %s UTF-8 ", tag, how);
    if(!filter)
      sio_write(h->sio, "ALL", 3);
    else {
      if( (rc = imap_write_key(h, filter, cmdno, can_do_literals)) != IMR_OK)
        return rc;
    }

    sio_write(h->sio, "\r\n", 2);
    imap_handle_flush(h);
    do
      rc = imap_cmd_step(h, cmdno);
    while(rc == IMR_UNTAGGED);
    imap_handle_idle_enable(h, 30);
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
  unsigned i;

  /* IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD); */
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

  if(rc == IMR_OK) {
  /* one version of dovecot (which?) returned insufficient number of
   * numbers, we need to work around it. */
    if(handle->mbox_view.entries == cnt)
      for(i=0; i<cnt; i++)
        msgno[i] = handle->mbox_view.arr[i];
    else {
      for(i=0; i<cnt; i++)
        msgno[i] = i + 1;
      imap_mbox_handle_set_msg(handle,
                               "bug in implementation of SORT command on "
                               "IMAP server exposed.");
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
  if(a->name) {
    if(b->name)
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
    printf("Should the client side sorting code "
           "be sorry about your bandwidth usage?\n");
    rc = imap_mbox_handle_fetch_set(handle, seqno_to_fetch,
                                    fetch_cnt, fetch_type);
    if(rc != IMR_OK)
      return rc;
  }
  g_free(seqno_to_fetch);

  sort_items = g_new(struct SortItem, cnt);
  for(i=0; i<cnt; i++) {
    sort_items[i].msg = imap_mbox_handle_get_msg(handle, msgno[i]);
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
  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  if(imap_mbox_handle_can_do(handle, IMCAP_SORT)) 
    return imap_mbox_sort_msgno_srv(handle, key, ascending, msgno, cnt);
  else {
    return 
      handle->enable_client_sort
      ? imap_mbox_sort_msgno_client(handle, key, ascending, msgno, cnt)
      : IMR_NO;
  }
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
  const char *keystr;
  unsigned i;

  IMAP_REQUIRED_STATE1(handle, IMHS_SELECTED, IMR_BAD);
  if(key == IMSO_MSGNO) {
    if(filter) { /* CASE 1a */
      handle->mbox_view.entries = 0; /* FIXME: I do not like this! 
                                      * we should not be doing such 
                                      * low level manipulations here */
      rc = imap_search_exec(handle, FALSE, filter, append_no, NULL);
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
      return IMR_OK;
    }
  } else { 
    if(imap_mbox_handle_can_do(handle, IMCAP_SORT)) { /* CASE 2a */
      unsigned cmdno;
      int can_do_literals =
        imap_mbox_handle_can_do(handle, IMCAP_LITERAL);
      ImapCmdTag tag;

      cmdno =  imap_make_tag(tag);
      keystr = sort_code_to_string(key);
      imap_handle_idle_disable(handle);
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
      sio_write(handle->sio, "\r\n", 2);
      imap_handle_idle_enable(handle, 30);
      imap_handle_flush(handle);
      do
        rc = imap_cmd_step(handle, cmdno);
      while(rc == IMR_UNTAGGED);
    } else {                                           /* CASE 2b */
      /* try client-side sorting... */
      if(!handle->enable_client_sort)
        return IMR_NO;
      handle->mbox_view.entries = 0; /* FIXME: I do not like this! 
                                      * we should not be doing such 
                                      * low level manipulations here */
      if(filter)
        rc = imap_search_exec(handle, FALSE, filter, append_no, NULL);
      else {
        rc = IMR_OK;
        for(i=0; i<handle->exists; i++)
          mbox_view_append_no(&handle->mbox_view, i+1);
      }
      if(rc != IMR_OK)
        return rc;
      rc = imap_mbox_sort_msgno_client(handle, key, ascending,
                                       handle->mbox_view.arr,
                                       handle->mbox_view.entries);
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
  return rc;
}

static void
make_msgno_table(ImapMboxHandle*handle, unsigned seqno, GHashTable *msgnos)
{
  g_hash_table_insert(msgnos, GUINT_TO_POINTER(seqno),
                      GUINT_TO_POINTER(seqno));
}

ImapResponse
imap_mbox_filter_msgnos(ImapMboxHandle *handle, ImapSearchKey *filter,
			GHashTable *msgnos)
{
  return imap_search_exec(handle, FALSE, filter,
                          (ImapSearchCb)make_msgno_table, msgnos);
}

/* imap_mbox_complete_msgids:
   finds which msgnos are missing from the supplied table and
   completes them if needed.
*/
static unsigned
need_msgid(unsigned seqno, GPtrArray* msgids)
{
  return g_ptr_array_index(msgids, seqno-1) ? 0 : seqno;
}

static void
msgid_cb(unsigned seqno, const char *buf, int buflen, void* arg)
{
  GPtrArray *arr = (GPtrArray*)arg;
  g_return_if_fail(seqno>=1 && seqno<=arr->len);
  g_ptr_array_index(arr, seqno-1) = g_strdup(buf);
}

ImapResponse
imap_mbox_complete_msgids(ImapMboxHandle *h,
			  GPtrArray *msgids,
			  unsigned first_seqno_to_fetch)
{
  gchar *seq, *cmd;
  ImapResponse rc = IMR_OK;
  ImapFetchBodyCb cb;
  void *arg;

  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);
  seq = coalesce_seq_range(first_seqno_to_fetch, msgids->len,
			   (CoalesceFunc)need_msgid, msgids);
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
  return rc;
}
