/* (c) Pawel Salek, 2004 */

#include <string.h>

#include "siobuf.h"
#include "imap-handle.h"
#include "imap_private.h"

struct ImapSearchKey_ {
  struct ImapSearchKey_ *next; /* message must match all the conditions 
                                * on the list. */
  enum {
    IMSE_OR, IMSE_FLAG, IMSE_STRING, IMSE_DATE, IMSE_SIZE
  } type;
  union {
    /* IMSE_OR */
    struct { struct ImapSearchKey_ *l, *r; } or;
    /* IMSE_FLAG */
    struct { 
      ImapMsgFlag sys_flag; /* only system flags supported ATM */
    } flag;
    /* IMSE_STRING */
    struct {
      char *s;
      ImapSearchHeader hdr;
    } string;
    /* IMSE_DATE: FIXME */
    time_t date;
    /* IMSE_SIZE: FIXME */
    size_t size;
  } d;
  unsigned negated:1;
};

ImapSearchKey*
imap_search_key_new_or(unsigned negated, ImapSearchKey *a, ImapSearchKey *b,
                       ImapSearchKey *next)
{
  ImapSearchKey *s = g_new(ImapSearchKey,1);
  s->next = next;
  s->type = IMSE_OR;
  s->negated = negated;
  s->d.or.l = a;
  s->d.or.r = b;
  return s;
}

ImapSearchKey*
imap_search_key_new_flag(unsigned negated, ImapMsgFlag flg,
                         ImapSearchKey *next)
{
  ImapSearchKey *s = g_new(ImapSearchKey,1);
  s->next = next;
  s->type = IMSE_FLAG;
  s->negated = negated;
  s->d.flag.sys_flag = flg;
  return s;
}

ImapSearchKey*
imap_search_key_new_string(unsigned negated, ImapSearchHeader hdr,
                           const char *string, ImapSearchKey *next)
{
  ImapSearchKey *s = g_new(ImapSearchKey,1);
  s->next = next;
  s->type = IMSE_STRING;
  s->negated = negated;
  s->d.string.hdr = hdr;
  s->d.string.s = g_strdup(string);
  return s;
}

void
imap_search_key_free(ImapSearchKey *s)
{
  while(s) {
    ImapSearchKey *t = s;
    switch(s->type) {
    case IMSE_OR:
      imap_search_key_free(s->d.or.l);
      imap_search_key_free(s->d.or.r);
      break;
    case IMSE_FLAG: break;
    case IMSE_STRING: g_free(s->d.string.s); break;
    case IMSE_DATE:
    case IMSE_SIZE: break;
    }
    s = s->next;
    g_free(t);
  }
}

static void
imap_write_key_flag(ImapMboxHandle *handle, unsigned negated, 
                    ImapMsgFlag flag)
{
  const char *s;
  if(negated) sio_write(handle->sio, " Un", 3);
  else sio_write(handle->sio, " ", 1);
  switch(flag) {
  default:
  case IMSGF_SEEN:     s = "seen";     break;
  case IMSGF_ANSWERED: s = "answered"; break;
  case IMSGF_FLAGGED:  s = "flagged";  break;
  case IMSGF_DELETED:  s = "deleted";  break;
  case IMSGF_DRAFT:    s = "draft";    break;
  }
  sio_write(handle->sio, s, strlen(s));
}

static void
imap_write_key_string(ImapMboxHandle *handle, unsigned negated,
                      ImapSearchHeader hdr, const char* str,
                      int use_literal)
{
  const char *s;
  if(negated) sio_write(handle->sio, " Not", 4);
  switch(hdr) {
  case IMSE_S_BCC:     s= "Bcc"; break;
  case IMSE_S_BODY:    s= "Body"; break;
  case IMSE_S_CC:      s= "Cc"; break;
  default: g_warning("Unknown header type!");
  case IMSE_S_FROM:    s= "From"; break;
  case IMSE_S_SUBJECT: s= "Subject"; break;
  case IMSE_S_TEXT:    s= "Text"; break;
  case IMSE_S_TO:      s= "To"; break;
  }
  sio_printf(handle->sio, " %s ", s);

  /* Here comes the difficult part: writing the string. If the server
     does not support LITERAL+, we have to either use quoting or use
     synchronizing literals which are somewhat painful. That's the
     life! For the moment, we replace non US-ASCII characters with
     spaces. */
  if(use_literal)
    sio_printf(handle->sio, "{%u+}\r\n%s", strlen(str), str);
  else {
    sio_write(handle->sio, "\"", 1);
    while(*str) {
      if(*str & 0x80) sio_write(handle->sio, " ", 1);
      else if(*str == '"') sio_write(handle->sio, "\\\"", 2);
      else if(*str == '"') sio_write(handle->sio, "\\\\", 2);
      else sio_write(handle->sio, str, 1);
      str++;
    }
    sio_write(handle->sio, "\"", 1);
  }
}

/* private */
ImapResponse
imap_write_key(ImapMboxHandle *handle, ImapSearchKey *s,
               int use_literal)
{
  while(s) {
    switch(s->type) {
    case IMSE_OR:
      if(s->negated) sio_write(handle->sio, " Not Or", 7);
      else sio_write(handle->sio, " Or", 3);
      imap_write_key(handle, s->d.or.l, use_literal); /* FIXME check result */
      imap_write_key(handle, s->d.or.r, use_literal);
      break;
    case IMSE_FLAG: 
      imap_write_key_flag(handle, s->negated, s->d.flag.sys_flag);
      break;
    case IMSE_STRING:
      imap_write_key_string(handle, s->negated, s->d.string.hdr,
                            s->d.string.s, use_literal); break;
    case IMSE_DATE: /* FIXME */
    case IMSE_SIZE:  /* FIXME */ break;
    }
    s = s->next;
  }
    
  return IMR_OK;
}

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

  if(!IMAP_MBOX_IS_SELECTED(h))
    return IMR_BAD;

  ocb  = h->search_cb;  h->search_cb  = (ImapSearchCb)cb;
  oarg = h->search_arg; h->search_arg = cb_arg;
  
  cmdno = imap_make_tag(tag);
  sio_printf(h->sio, "%s Search", tag);
  if( (ir=imap_write_key(h, s, can_do_literals)) == IMR_OK) {
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
