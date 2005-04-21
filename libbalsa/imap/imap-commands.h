#ifndef __IMAP_COMMANDS_H__
#define __IMAP_COMMANDS_H__ 1
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

#include <gmime/gmime.h>
#include "imap-handle.h"

/* Any-State */
int imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap);
ImapResponse imap_mbox_handle_noop(ImapMboxHandle *r);

/* Non-Authenticated State */

/* Authenticated State */
ImapResponse imap_mbox_select(ImapMboxHandle* handle, const char *mbox,
                              gboolean *readonly_mbox);
ImapResponse imap_mbox_examine(ImapMboxHandle* handle, const char* mbox);
ImapResponse imap_mbox_create(ImapMboxHandle* handle, const char* new_mbox);
ImapResponse imap_mbox_delete(ImapMboxHandle* handle, const char* mbox);
ImapResponse imap_mbox_rename(ImapMboxHandle* handle,
			    const char* old_mbox,
			    const char* new_mbox);
ImapResponse imap_mbox_subscribe(ImapMboxHandle* handle,
                                 const char* mbox, gboolean subscribe);
ImapResponse imap_mbox_list(ImapMboxHandle *r, const char*what);
ImapResponse imap_mbox_lsub(ImapMboxHandle *r, const char*what);
typedef enum {
  IMSTAT_MESSAGES = 0,
  IMSTAT_RECENT,
  IMSTAT_UIDNEXT,
  IMSTAT_UIDVALIDITY,
  IMSTAT_UNSEEN,
  IMSTAT_NONE
} ImapStatusItem;
struct ImapStatusResult {
  ImapStatusItem item;
  unsigned       result;
};
ImapResponse imap_mbox_status(ImapMboxHandle *r, const char*what, 
                              struct ImapStatusResult *res);
typedef size_t (*ImapAppendFunc)(char*, size_t, void*);
ImapResponse imap_mbox_append(ImapMboxHandle *handle, const char *mbox,
                              ImapMsgFlags flags, size_t sz, 
                              ImapAppendFunc dump_cb,  void* arg);
#ifdef USE_IMAP_APPEND_STR /* not used currently */
ImapResponse imap_mbox_append_str(ImapMboxHandle *handle, const char *mbox,
                              ImapMsgFlags flags, size_t sz, char *txt);
#endif
ImapResponse imap_mbox_append_stream(ImapMboxHandle * handle,
				     const char *mbox, ImapMsgFlags flags,
				     GMimeStream * stream, ssize_t len);

/* Selected State */
ImapResponse imap_mbox_close(ImapMboxHandle *h);
ImapResult imap_mbox_search(ImapMboxHandle *h, const char* query);
ImapResponse imap_mbox_noop(ImapMboxHandle *r);
ImapResponse imap_mbox_expunge(ImapMboxHandle* h);

ImapResponse imap_mbox_store_flag(ImapMboxHandle *r, unsigned cnt,
                                  unsigned *seqno, ImapMsgFlag flg,
                                  gboolean state);
ImapResponse imap_mbox_handle_copy(ImapMboxHandle* handle, unsigned cnt,
                                   unsigned *seqno, const gchar *dest);

ImapResponse imap_mbox_find_unseen(ImapMboxHandle * h, unsigned *msgcnt,
				   unsigned **msgs);
gboolean imap_mbox_handle_msgno_has_flags(ImapMboxHandle *h, unsigned msgno,
                                          ImapMsgFlag flag_set,
                                          ImapMsgFlag flag_unset);

ImapResponse imap_mbox_handle_fetch_range(ImapMboxHandle* handle,
                                          unsigned lo, unsigned hi,
                                          ImapFetchType ift);
ImapResponse imap_mbox_handle_fetch_set(ImapMboxHandle* handle,
                                        unsigned *set, unsigned cnt,
                                        ImapFetchType ift);

ImapResponse imap_mbox_handle_fetch_rfc822(ImapMboxHandle* handle,
                                           unsigned seqno, FILE *fl);
ImapResponse imap_mbox_handle_fetch_rfc822_uid(ImapMboxHandle* handle,
                                               unsigned uid, FILE *fl);
typedef void (*ImapFetchBodyCb)(const char *buf, int buflen, void* arg);
ImapResponse imap_mbox_handle_fetch_body(ImapMboxHandle* handle, 
                                         unsigned seqno, 
                                         const char *section,
                                         ImapFetchBodyOptions options,
                                         ImapFetchBodyCb body_handler,
                                         void *arg);

/* Experimental/Expansion */
ImapResponse imap_handle_starttls(ImapMboxHandle *handle);
ImapResponse imap_mbox_scan(ImapMboxHandle *r, const char*what,
                            const char*str);
ImapResponse imap_mbox_unselect(ImapMboxHandle *h);
ImapResponse imap_mbox_thread(ImapMboxHandle *h, const char *how,
                              ImapSearchKey *filter);

ImapResponse imap_mbox_uid_search(ImapMboxHandle *handle, ImapSearchKey *key,
                                  void (*cb)(unsigned uid, void *),
                                  void *cb_data);

ImapResponse imap_mbox_sort_msgno(ImapMboxHandle *handle, ImapSortKey key,
                                  int ascending, unsigned int *msgno,
				  unsigned cnt);
ImapResponse imap_mbox_sort_filter(ImapMboxHandle *handle, ImapSortKey key,
                                   int ascending, ImapSearchKey *filter);
ImapResponse imap_mbox_filter_msgnos(ImapMboxHandle * handle,
                                     ImapSearchKey *filter,
                                     GHashTable * msgnos);

#endif /* __IMAP_COMMANDS_H__ */
