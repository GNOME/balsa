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

#define _POSIX_SOURCE 1
#include <string.h>
#include <time.h>

#include "siobuf.h"
#include "imap-handle.h"
#include "imap_private.h"

struct ImapSearchKey_ {
  struct ImapSearchKey_ *next; /* message must match all the conditions 
                                * on the list. */
  enum {
    IMSE_NOT, IMSE_OR, IMSE_FLAG, IMSE_STRING, IMSE_DATE, IMSE_SIZE
  } type;
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
    struct { time_t dt; unsigned internal_date:1; unsigned range:2; } date;
    /* IMSE_SIZE */
    size_t size;
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
  int i;
  s->next = NULL;
  s->type = IMSE_STRING;
  s->negated = negated;
  s->d.string.hdr = hdr;
  s->d.string.usr = (user_hdr && *user_hdr) ? g_strdup(user_hdr) : NULL;
  s->d.string.s = g_strdup(string);
  if(s->d.string.usr)
    for(i=strlen(s->d.string.usr)-1; i>=0; i--)
      if(!IS_ATOM_CHAR(s->d.string.usr[i])) s->d.string.usr[i] = '_';
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
    }
    s = s->next;
    g_free(t);
  }
}

void
imap_search_key_set_next(ImapSearchKey *list, ImapSearchKey *next)
{
  if(list->next) g_warning("I may be loosing my tail now!\n");
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
      sio_printf(handle->sio, "{%u}\r\n", len);
      imap_handle_flush(handle);
      do
        rc = imap_cmd_step(handle, cmdno);
      while(rc == IMR_UNTAGGED);
      if (rc != IMR_RESPOND) {
        fprintf(stderr, "%s(): unexpected response:\n", __FUNCTION__);
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
        else if(*s == '"') sio_write(handle->sio, "\\\\", 2);
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
  /* january is there twice - it is not a mispelling! */
  static const char *month[] = 
    { "Jan", "Jan", "Feb", "Mar", "Apr", "May",
      "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  struct tm date;
  if(!internal) sio_write(handle->sio, "SENT", 4);
  switch(range) {
  case IMSE_D_BEFORE : sio_write(handle->sio, "BEFORE ", 7); break;
  case IMSE_D_ON     : sio_write(handle->sio, "ON ",     3); break;
  default: /* which is -2, the only remaining option */
  case IMSE_D_SINCE  : sio_write(handle->sio, "SINCE " , 6); break;
  }
  localtime_r(&tm, &date);
  sio_printf(handle->sio, "%02d-%s-%04d",
             date.tm_mday, month[date.tm_mon], date.tm_year + 1900);
}

static void
imap_write_key_size(ImapMboxHandle *handle, gboolean negate, size_t size)
{
  if(negate) sio_printf(handle->sio, "NOT LARGER %u", (unsigned)size);
  else       sio_printf(handle->sio, "LARGER %u", (unsigned)size);
}

/* private.  */
ImapResponse
imap_write_key(ImapMboxHandle *handle, ImapSearchKey *s, unsigned cmdno,
               int use_literal)
{
  ImapResponse rc;
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

ImapResponse
imap_search_exec(ImapMboxHandle *h, ImapSearchKey *s,
                 ImapSearchCb cb, void *cb_arg)
{
  int can_do_literals =
    imap_mbox_handle_can_do(h, IMCAP_LITERAL);
  ImapResponse ir;
  ImapCmdTag tag;
  ImapSearchCb ocb;
  void *oarg;
  unsigned cmdno;

  IMAP_REQUIRED_STATE1(h, IMHS_SELECTED, IMR_BAD);

  if(!s)
    return IMR_BAD;

  if(execute_flag_only_search(h, s, cb, cb_arg, &ir))
    return ir;

  ocb  = h->search_cb;  h->search_cb  = (ImapSearchCb)cb;
  oarg = h->search_arg; h->search_arg = cb_arg;
  
  cmdno = imap_make_tag(tag);
  sio_printf(h->sio, "%s Search ", tag);
  if( (ir=imap_write_key(h, s, cmdno, can_do_literals)) == IMR_OK) {
    sio_write(h->sio, "\r\n", 2);
    imap_handle_flush(h);
    do
      ir = imap_cmd_step(h, cmdno);
    while(ir == IMR_UNTAGGED);
  }
  h->search_cb  = ocb;
  h->search_arg = oarg;
  /* Set disconnected state here if necessary? */
  return ir;
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
