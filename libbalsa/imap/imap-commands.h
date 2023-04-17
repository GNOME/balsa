#ifndef __IMAP_COMMANDS_H__
#define __IMAP_COMMANDS_H__ 1
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

#include <gmime/gmime.h>
#include "imap-handle.h"
#include "imap_search.h"

/* Any-State */
/* int imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap); */
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

typedef enum {
  IMA_STAGE_NEW_MSG,
  IMA_STAGE_PASS_DATA
} ImapAppendMultiStage;

typedef size_t (*ImapAppendMultiFunc)(char*, size_t,
				      ImapAppendMultiStage stage,
				      ImapMsgFlags *flags, void*);
ImapResponse imap_mbox_append_multi(ImapMboxHandle *handle,
				    const char *mbox,
				    ImapAppendMultiFunc dump_cb,
				    void* arg,
				    ImapSequence *uid_range);
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
ImapResponse imap_mbox_expunge_a(ImapMboxHandle *h);

ImapResponse imap_mbox_store_flag(ImapMboxHandle *r, unsigned cnt,
                                  unsigned *seqno, ImapMsgFlag flg,
                                  gboolean state);
ImapResponse imap_mbox_store_flag_a(ImapMboxHandle *r,
                                    unsigned        cnt,
                                    unsigned       *seqno,
                                    ImapMsgFlag     flg,
                                    gboolean        state);

ImapResponse imap_mbox_handle_copy(ImapMboxHandle* handle,
				   unsigned cnt, unsigned *seqno,
				   const gchar *dest,
				   ImapSequence *ret_sequence);

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

typedef void (*ImapFetchBodyCb)(unsigned seqno, const char *buf,
				size_t buflen, void* arg);

ImapResponse imap_mbox_handle_fetch_rfc822(ImapMboxHandle* handle,
					   unsigned cnt, unsigned *seqno,
					   gboolean peek_only,
                                           ImapFetchBodyCb cb,
					   void *cb_data);

ImapResponse imap_mbox_handle_fetch_rfc822_uid(ImapMboxHandle* handle,
                                               unsigned uid, gboolean peek,
                                               FILE *fl);

ImapResponse imap_mbox_handle_fetch_body(ImapMboxHandle* handle, 
                                         unsigned seqno, 
                                         const char *section,
                                         gboolean peek_only,
                                         ImapFetchBodyOptions options,
                                         ImapFetchBodyCb body_handler,
                                         void *arg);

/* Experimental/Expansion */
ImapResponse imap_handle_starttls(ImapMboxHandle *handle, GError **error);
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

ImapResponse imap_mbox_complete_msgids(ImapMboxHandle *handle,
				       GPtrArray *msgids,
				       unsigned first_seqno_to_fetch);

/* RFC 2087: Quota */
ImapResponse imap_mbox_get_quota(ImapMboxHandle* handle, const char* mbox,
                                 gulong* max, gulong* used);

/* RFC 4314: ACL's */
ImapResponse imap_mbox_get_my_rights(ImapMboxHandle* handle,
				     ImapAclType* my_rights,
				     gboolean force_update);
ImapResponse imap_mbox_get_acl(ImapMboxHandle* handle, const char* mbox,
                               GList** acls);

#endif /* __IMAP_COMMANDS_H__ */
