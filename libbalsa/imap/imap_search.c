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

#define _POSIX_C_SOURCE 199506L
#define _XOPEN_SOURCE 500

#include <string.h>
#include <glib.h>
#include <time.h>

#include "siobuf-nc.h"
#include "imap_search.h"
#include "imap_private.h"

typedef enum {
  IMSE_NOT, IMSE_OR, IMSE_FLAG, IMSE_STRING, IMSE_DATE, IMSE_SIZE,
  IMSE_SEQUENCE
} ImapSearchKeyType;

struct ImapSearchKey_ {
  struct ImapSearchKey_ *next; /* message must match all the conditions 
                                * on the list. */
  ImapSearchKeyType type;
  union {
    /* IMSE_NOT */
    struct ImapSearchKey_ *not;
    /* IMSE_OR */
    struct { struct ImapSearchKey_ *l, *r; } or;
    /* IMSE_FLAG */
    struct { 
      ImapMsgFlag sys_flag; /* only system flags supported ATM */
    } flag;
    /* IMSE_STRING */
    struct {
      char *s, *usr; /* string and user header if any */
      ImapSearchHeader hdr;
    } string;
    /* IMSE_DATE: FIXME */
    struct { time_t dt; unsigned internal_date:1; int range:2; } date;
    /* IMSE_SIZE */
    size_t size;
    /* IMSE_SEQENCE */
      struct { gchar *string;  int uid; } seq;
  } d;
  unsigned negated:1;
};

ImapSearchKey*
imap_search_key_new_not(unsigned negated, ImapSearchKey *list)
{
  ImapSearchKey *s = g_new(ImapSearchKey,1);
  s->next = NULL;
  s->type = IMSE_NOT;
  s->negated = negated; /* Usually true */
  s->d.not = list;
  return s;
}

ImapSearchKey*
imap_search_key_new_or(unsigned negated, ImapSearchKey *a, ImapSearchKey *b)
{
  ImapSearchKey *s = g_new(ImapSearchKey,1);
  s->next = NULL;
  s->type = IMSE_OR;
  s->negated = negated;
  s->d.or.l = a;
  s->d.or.r = b;
  return s;
}

ImapSearchKey*
imap_search_key_new_flag(unsigned negated, ImapMsgFlag flg)
{
  ImapSearchKey *s = g_new(ImapSearchKey,1);
  s->next = NULL;
  s->type = IMSE_FLAG;
  s->negated = negated;
  s->d.flag.sys_flag = flg;
  return s;
}

/* imap_search_key_new_string sets the string. It makes sure that
   the user header - if specified - is sane.
*/
ImapSearchKey*
imap_search_key_new_string(unsigned negated, ImapSearchHeader hdr,
                           const char *string, const char *user_hdr)
{
  ImapSearchKey *s = g_new(ImapSearchKey,1);
  s->next = NULL;
  s->type = IMSE_STRING;
  s->negated = negated;
  s->d.string.hdr = hdr;
  s->d.string.usr = (user_hdr && *user_hdr) ? g_strdup(user_hdr) : NULL;
  s->d.string.s = g_strdup(string);
  if(s->d.string.usr) {
    int i;
    for(i=strlen(s->d.string.usr)-1; i>=0; i--)
      if(!IS_ATOM_CHAR(s->d.string.usr[i])) s->d.string.usr[i] = '_';
  }
  return s;
}

ImapSearchKey*
imap_search_key_new_size_greater(unsigned negated, size_t sz)
{
  ImapSearchKey *s = g_new(ImapSearchKey,1);
  s->next = NULL;
  s->type = IMSE_SIZE;
  s->negated = negated;
  s->d.size = sz;
  return s;
}

/* imap_search_key_new_date() creates new term that matches earlier or
   later dates. It can match both internal date as well as sent
   date. */
ImapSearchKey*
imap_search_key_new_date(ImapSearchDateRange range, int internal, time_t tm)
{
  ImapSearchKey *s = g_new(ImapSearchKey,1);
  s->next = NULL;
  s->type = IMSE_DATE;
  s->negated = FALSE; /* this field is ignored for date */
  s->d.date.dt = tm;
  s->d.date.internal_date = internal;
  s->d.date.range = range;
  return s;
}


ImapSearchKey*
imap_search_key_new_range(unsigned negated, int uid,
                          unsigned lo, unsigned hi)

{
  if(lo<=hi) {
    ImapSearchKey *s = g_new(ImapSearchKey,1);
    s->next = NULL;
    s->type = IMSE_SEQUENCE;
    s->negated = negated;
    s->d.seq.uid = uid;
    s->d.seq.string = lo<hi 
      ? g_strdup_printf("%u:%u", lo, hi)
      : g_strdup_printf("%u", lo);
    return s;
  } else { /* always false */
    return NULL;
  }
}

ImapSearchKey*
imap_search_key_new_set(unsigned negated, int uid, int cnt, unsigned *seqnos)

{
  if(cnt>0) {
    ImapSearchKey *s = g_new(ImapSearchKey,1);
    s->next = NULL;
    s->type = IMSE_SEQUENCE;
    s->negated = negated;
    s->d.seq.uid = uid;
    s->d.seq.string = imap_coalesce_set(cnt, seqnos);
    return s;
  } else { /* always false */
    return NULL;
  }
}

void
imap_search_key_free(ImapSearchKey *s)
{
  while(s) {
    ImapSearchKey *t = s;
    switch(s->type) {
    case IMSE_NOT:
      imap_search_key_free(s->d.not);
      break;
    case IMSE_OR:
      imap_search_key_free(s->d.or.l);
      imap_search_key_free(s->d.or.r);
      break;
    case IMSE_FLAG: break;
    case IMSE_STRING:
      g_free(s->d.string.usr);
      g_free(s->d.string.s);
      break;
    case IMSE_DATE:
    case IMSE_SIZE: break;
    case IMSE_SEQUENCE:
        g_free(s->d.seq.string); 
        break;
    }
    s = s->next;
    g_free(t);
  }
}

static gboolean
imap_search_checks_body(const ImapSearchKey *s)
{
  while(s) {
    switch(s->type) {
    case IMSE_NOT: /* NOOP */ break; 
    case IMSE_OR: 
      if(imap_search_checks_body(s->d.or.l) ||
         imap_search_checks_body(s->d.or.r))
        return TRUE;
      break;
    case IMSE_FLAG: break;
    case IMSE_STRING:
      if(s->d.string.hdr == IMSE_S_BODY)
        return TRUE;
    case IMSE_DATE:
    case IMSE_SIZE:
    case IMSE_SEQUENCE:
      break;
    }
    s = s->next;
  }
  return FALSE;
}

static gboolean
imap_search_checks(const ImapSearchKey *s, ImapSearchKeyType s_type)
{
  while(s) {
    switch(s->type) {
    case IMSE_NOT: /* NOOP */ break; 
    case IMSE_OR: 
      if(imap_search_checks(s->d.or.l, s_type) ||
         imap_search_checks(s->d.or.r, s_type))
        return TRUE;
      break;
    default:
      if(s->type == s_type)
	return TRUE;
      break;
    }
    s = s->next;
  }
  return FALSE;
}

void
imap_search_key_set_next(ImapSearchKey *list, ImapSearchKey *next)
{
  if(list->next) g_warning("I may be loosing my tail now!");
  list->next = next;
}
static void
imap_write_key_flag(ImapMboxHandle *handle, unsigned negated, 
                    ImapMsgFlag flag)
{
  const char *s;
  if(negated) sio_write(handle->sio, "Un", 2);
  switch(flag) {
  default:
  case IMSGF_SEEN:     s = "seen";     break;
  case IMSGF_ANSWERED: s = "answered"; break;
  case IMSGF_FLAGGED:  s = "flagged";  break;
  case IMSGF_DELETED:  s = "deleted";  break;
  case IMSGF_DRAFT:    s = "draft";    break;
  case IMSGF_RECENT:   s = "recent";   break;
  }
  sio_write(handle->sio, s, strlen(s));
}

static ImapResponse
imap_write_key_string(ImapMboxHandle *handle, ImapSearchKey *k,
                      unsigned cmdno, int use_literal)
{
  const char *s;

  g_return_val_if_fail(handle->state != IMHS_DISCONNECTED, IMR_BAD);
  if(k->negated) sio_write(handle->sio, "Not ", 4);
  switch(k->d.string.hdr) {
  case IMSE_S_BCC:     s= "Bcc"; break;
  case IMSE_S_BODY:    s= "Body"; break;
  case IMSE_S_CC:      s= "Cc"; break;
  default: g_warning("Unknown header type!");
  case IMSE_S_FROM:    s= "From"; break;
  case IMSE_S_SUBJECT: s= "Subject"; break;
  case IMSE_S_TEXT:    s= "Text"; break;
  case IMSE_S_TO:      s= "To"; break;
  case IMSE_S_HEADER:  s= "Header"; break;
  }
  sio_printf(handle->sio, "%s ", s);
  if(k->d.string.usr) {
    /* we checked on structure creation that the header does not contain
     * spaces or 8-bit characters so we can write it simply... */
    sio_write(handle->sio, k->d.string.usr, strlen(k->d.string.usr));
    sio_write(handle->sio, " ", 1);
  }
  /* Here comes the difficult part: writing the string. If the server
     does not support LITERAL+, we have to either use quoting or use
     synchronizing literals which are somewhat painful. That's the
     life! */
  if(use_literal)
    sio_printf(handle->sio, "{%u+}\r\n%s",
               (unsigned)strlen(k->d.string.s), k->d.string.s);
  else { /* No literal+ suppport, do it the old way */
    for (s = k->d.string.s; *s && (*s & 0x80) == 0; s++)
      ;
    if(*s & 0x80) { /* use synchronising literals */
      int c;
      ImapResponse rc;
      unsigned len = strlen(k->d.string.s);
      sio_printf(handle->sio, "{%u}", len);
      net_client_siobuf_flush(handle->sio, NULL);
      rc = imap_cmd_process_untagged(handle, cmdno);
      if (rc != IMR_RESPOND) {
        g_debug("%s(): unexpected response", __FUNCTION__);
        return rc;
      }
      /* consume to the end of line */
      while( (c=sio_getc(handle->sio)) != -1 && c != '\n')
        ;
      if(c == -1) return IMR_SEVERED;
      sio_write(handle->sio, k->d.string.s, len);
    } else { /* quoting is sufficient */
      sio_write(handle->sio, "\"", 1);
      for(s=k->d.string.s; *s; s++) {
        if(*s == '"') sio_write(handle->sio, "\\\"", 2);
        else if(*s == '\\') sio_write(handle->sio, "\\\\", 2);
        else sio_write(handle->sio, s, 1);
      }
      sio_write(handle->sio, "\"", 1);
    }
  }
  return IMR_OK;
}

static void
imap_write_key_date(ImapMboxHandle *handle, ImapSearchDateRange range,
                    gboolean internal, time_t tm)
{
  static const char *month[] = 
    { "Jan", "Feb", "Mar", "Apr", "May",
      "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  GDate date;
  if(!internal) sio_write(handle->sio, "SENT", 4);
  switch(range) {
  case IMSE_D_BEFORE : sio_write(handle->sio, "BEFORE ", 7); break;
  case IMSE_D_ON     : sio_write(handle->sio, "ON ",     3); break;
  default: /* which is -2, the only remaining option */
  case IMSE_D_SINCE  : sio_write(handle->sio, "SINCE " , 6); break;
  }
  g_date_set_time_t(&date, tm);
  sio_printf(handle->sio, "%02d-%s-%04d",
	     date.day, month[date.month - 1], date.year);
}

static void
imap_write_key_size(ImapMboxHandle *handle, gboolean negate, size_t size)
{
  if(negate) sio_printf(handle->sio, "NOT LARGER %u", (unsigned)size);
  else       sio_printf(handle->sio, "LARGER %u", (unsigned)size);
}

static void
imap_write_key_sequence(ImapMboxHandle *handle, gboolean negate,
                        gboolean uid, const gchar *seq)
{
    if(negate) sio_write(handle->sio, "NOT ", 4);
    if(uid)    sio_write(handle->sio, "UID ", 4);
    sio_write(handle->sio, seq, strlen(seq));
}
/* private.  */
ImapResponse
imap_write_key(ImapMboxHandle *handle, ImapSearchKey *s, unsigned cmdno,
               int use_literal)
{
  ImapResponse rc;

  g_return_val_if_fail(handle->state != IMHS_DISCONNECTED, IMR_BAD);
  while(s) {
    switch(s->type) {
    case IMSE_NOT:
      if(s->negated) sio_write(handle->sio, "Not (", 5);
      else sio_write(handle->sio, "(", 1);
      rc = imap_write_key(handle, s->d.not, cmdno, use_literal);
      if(rc != IMR_OK) return rc;
      sio_write(handle->sio, ")", 1);
      break;
    case IMSE_OR:
      if(s->negated) sio_write(handle->sio, "Not Or ", 7);
      else sio_write(handle->sio, "Or ", 3);
      if(s->d.or.l->next) sio_write(handle->sio, "(", 1);
      rc = imap_write_key(handle, s->d.or.l, cmdno, use_literal);
      if(rc != IMR_OK) return rc;      
      if(s->d.or.l->next) sio_write(handle->sio, ") ", 2);
      else  sio_write(handle->sio, " ", 1);
      if(s->d.or.r->next) sio_write(handle->sio, "(", 1);
      rc = imap_write_key(handle, s->d.or.r, cmdno, use_literal);
      if(rc != IMR_OK) return rc;      
      if(s->d.or.r->next) sio_write(handle->sio, ")", 1);
      break;
    case IMSE_FLAG: 
      imap_write_key_flag(handle, s->negated, s->d.flag.sys_flag);
      break;
    case IMSE_STRING:
      rc = imap_write_key_string(handle, s, cmdno, use_literal);
      if(rc != IMR_OK) return rc;
      break;
    case IMSE_DATE:
      imap_write_key_date(handle, s->d.date.range, s->d.date.internal_date,
                          s->d.date.dt);
      break;
    case IMSE_SIZE:
      imap_write_key_size(handle, s->negated, s->d.size);
      break;
    case IMSE_SEQUENCE:
      imap_write_key_sequence(handle, s->negated,
                              s->d.seq.uid, s->d.seq.string);
      break;
    }
    s = s->next;
    if(s) sio_write(handle->sio, " ", 1);
  }
    
  return IMR_OK;
}

static gboolean
execute_flag_only_search(ImapMboxHandle *h, ImapSearchKey *s,
                         ImapSearchCb cb, void *cb_arg,
                         ImapResponse *rc);

/** Searches the mailbox and calls the specified callback for all
    messages matching the search key. It tries not to search too many
    messages at once to avoid session timeouts. The limits on batch
    lengths are empirical.

    There is one known problem that is due to the fact that we do not
    separate between easy (flag) and expensive (body) searches: the
    search will be repeated on every flag change. What we (but also
    IMAP servers!) could do is to execute the quick search first to
    get an idea which messages should be searched for the expensive
    parts. This also coincides with splitting searches into mutable
    (flags) and immutable terms (anything else).
 */
ImapResponse
imap_search_exec_unlocked(ImapMboxHandle *h, gboolean uid, 
			  ImapSearchKey *s, ImapSearchCb cb, void *cb_arg)
{
  int can_do_literals =
    imap_mbox_handle_can_do(h, IMCAP_LITERAL);

  /* We cannot use ESEARCH for UID searches easily. See the imapext
     thread starting at:
     http://www.imc.org/ietf-imapext/mail-archive/msg03946.html
  */
  int can_do_esearch = !uid && imap_mbox_handle_can_do(h, IMCAP_ESEARCH);
  ImapResponse ir = IMR_OK;
  ImapCmdTag tag;
  ImapSearchCb ocb;
  void *oarg;
  unsigned cmdno;
  const gchar *cmd_string;
  gboolean split;

  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);

  if(!s)
    return IMR_BAD;

  if(execute_flag_only_search(h, s, cb, cb_arg, &ir))
    return ir;

  if(can_do_esearch)
    cmd_string = "Search return (all)";
  else
    cmd_string = "Search";

  ocb  = h->search_cb;  h->search_cb  = (ImapSearchCb)cb;
  oarg = h->search_arg; h->search_arg = cb_arg;
  
  if (!imap_handle_idle_disable(h)) return IMR_SEVERED;

  split = imap_search_checks(s, IMSE_SEQUENCE);
  if(split) {
    unsigned delta, lo;

    if(imap_search_checks_body(s)) {
      static const unsigned BODY_TO_SEARCH_AT_ONCE   = 500000;
      delta = BODY_TO_SEARCH_AT_ONCE;
    } else if(imap_search_checks(s, IMSE_STRING)) {
      static const unsigned HEADER_TO_SEARCH_AT_ONCE = 2000;
      delta = HEADER_TO_SEARCH_AT_ONCE;
    } else {
      static const unsigned UIDS_TO_SEARCH_AT_ONCE   = 100000;
      delta = UIDS_TO_SEARCH_AT_ONCE;
    }

    /* This loop may take long time to execute. Consider providing some
       feedback to the user... */
    for(lo = 1; ir == IMR_OK && lo<=h->exists; lo += delta) {
      unsigned hi = lo + delta-1;
      if(hi>h->exists)
	hi = h->exists;
      cmdno = imap_make_tag(tag);
      sio_printf(h->sio, "%s%s %s %u:%u ", tag, uid ? " UID" : "", cmd_string,
               lo, hi);
      if( (ir=imap_write_key(h, s, cmdno, can_do_literals)) == IMR_OK) {
	net_client_siobuf_flush(h->sio, NULL);
	ir = imap_cmd_process_untagged(h, cmdno);
      } else break;
    }
  } else { /* no split */
    cmdno = imap_make_tag(tag);
    sio_printf(h->sio, "%s%s %s ", tag, uid ? " UID" : "", cmd_string);
    if( (ir=imap_write_key(h, s, cmdno, can_do_literals)) == IMR_OK) {
      net_client_siobuf_flush(h->sio, NULL);
      ir = imap_cmd_process_untagged(h, cmdno);
    }
  }
  h->search_cb  = ocb;
  h->search_arg = oarg;
  imap_handle_idle_enable(h, 30);
  /* Set disconnected state here if necessary? */
  return ir;
}

ImapResponse
imap_search_exec(ImapMboxHandle *h, gboolean uid, 
		 ImapSearchKey *s, ImapSearchCb cb, void *cb_arg)
{
  ImapResponse rc;

  g_object_ref(h);
  g_mutex_lock(&h->mutex);
  rc = imap_search_exec_unlocked(h, uid, s, cb, cb_arg);
  g_mutex_unlock(&h->mutex);
  g_object_unref(h);

  return rc;
}

/* == optimized branch for flag-only searches..  Since we know most of
   the flags most of the time, there is no point in repeating some
   flag-only searches over and over. We just run a search optimized
   for exactly this kind of searches */
static gboolean
collect_needed_flags(ImapSearchKey *s, ImapMsgFlag *flag)
{
  gboolean res;
  switch(s->type) {
  case IMSE_NOT: 
    res = collect_needed_flags(s->d.not, flag);
    break;
  case IMSE_OR:
    res = collect_needed_flags(s->d.or.l, flag) &&
      collect_needed_flags(s->d.or.r, flag);
    break;
  case IMSE_FLAG:
    *flag |= s->d.flag.sys_flag; res = TRUE; break;
  case IMSE_SEQUENCE:
      if(s->d.seq.uid) /* we do not collect uids yet */
          return FALSE;
  default:
    return FALSE;
  }
  if(res && s->next)
    res = collect_needed_flags(s->next, flag);
  return res;
}

static gboolean
search_key_matches(ImapSearchKey *s, ImapMsgFlag flag)
{
  gboolean res;
  switch(s->type) {
  case IMSE_NOT: 
    res =
      !search_key_matches(s->d.not, flag);
    break;
  case IMSE_OR:
    res = (search_key_matches(s->d.or.l, flag) ||
           search_key_matches(s->d.or.r, flag));
    break;
  case IMSE_FLAG:
    res = (flag & s->d.flag.sys_flag);
    break;
  default: 
    return FALSE;
  }
  if(s->negated)
    res = !res;
  if(res && s->next)
    res = search_key_matches(s->next, flag);  
  return res;
}

static gboolean
execute_flag_only_search(ImapMboxHandle *h, ImapSearchKey *s,
                         ImapSearchCb cb, void *cb_arg,
                         ImapResponse *rc)
{
  ImapMsgFlag needed_flags = 0;
  unsigned i;
  
  if(!collect_needed_flags(s, &needed_flags))
    return FALSE;
  
  if( (*rc = imap_assure_needed_flags(h, needed_flags)) != IMR_OK)
    return FALSE;
  
  for(i=0; i<h->exists; i++) {
    ImapFlagCache *f =
      &g_array_index(h->flag_cache, ImapFlagCache, i);
    if(search_key_matches(s, f->flag_values))
      cb(h, i+1, cb_arg);
  }
  return TRUE;
}
