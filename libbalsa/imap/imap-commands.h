#ifndef __IMAP_COMMANDS_H__
#define __IMAP_COMMANDS_H__ 1

#include <gmime/gmime.h>
#include "imap-handle.h"

/* Any-State */
int imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap);
ImapResult imap_mbox_handle_noop(ImapMboxHandle *r);

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
typedef size_t (*ImapAppendFunc)(char*, size_t, void*);
ImapResponse imap_mbox_append(ImapMboxHandle *handle, const char *mbox,
                              ImapMsgFlags flags, size_t sz, 
                              ImapAppendFunc dump_cb,  void* arg);
#if USE_IMAP_APPEND_STR /* not used currently */
ImapResponse imap_mbox_append_str(ImapMboxHandle *handle, const char *mbox,
                              ImapMsgFlags flags, size_t sz, char *txt);
#endif
ImapResponse imap_mbox_append_stream(ImapMboxHandle * handle,
				     const char *mbox, ImapMsgFlags flags,
				     GMimeStream * stream, ssize_t len);

/* Selected State */
ImapResult imap_mbox_search(ImapMboxHandle *h, const char* query);
ImapResponse imap_mbox_noop(ImapMboxHandle *r);
ImapResponse imap_mbox_expunge(ImapMboxHandle* h);

ImapResponse imap_mbox_store_flag(ImapMboxHandle *r, unsigned seq,
                                  ImapMsgFlag flg, gboolean state);
ImapResponse imap_mbox_store_flag_m(ImapMboxHandle* h, unsigned msgcnt,
                                    unsigned *seqno, ImapMsgFlag flg, 
                                    gboolean state);
ImapResponse imap_mbox_handle_copy(ImapMboxHandle* handle, unsigned seqno,
                                   const gchar *dest);

ImapResponse imap_mbox_handle_fetch_range(ImapMboxHandle* handle,
                                          unsigned lo, unsigned hi,
                                          ImapFetchType ift);
ImapResponse imap_mbox_handle_fetch_set(ImapMboxHandle* handle,
                                        unsigned *set, unsigned cnt,
                                        ImapFetchType ift);

ImapResponse imap_mbox_handle_fetch_structure(ImapMboxHandle* handle,
                                              unsigned seqno);
ImapResponse imap_mbox_handle_fetch_rfc822(ImapMboxHandle* handle,
                                           unsigned seqno, FILE *fl);
ImapResponse imap_mbox_handle_fetch_rfc822_uid(ImapMboxHandle* handle,
                                               unsigned uid, FILE *fl);
typedef void (*ImapFetchBodyCb)(const char *buf, int buflen, void* arg);
ImapResponse imap_mbox_handle_fetch_body(ImapMboxHandle* handle, 
                                         unsigned seqno, 
                                         const char *section,
                                         ImapFetchBodyCb body_handler,
                                         void *arg);

/* Experimental/Expansion */
ImapResponse imap_handle_starttls(ImapMboxHandle *handle);
ImapResult imap_mbox_scan(ImapMboxHandle *r, const char*what, const char*str);
ImapResponse imap_mbox_unselect(ImapMboxHandle *h);
ImapResult imap_mbox_thread(ImapMboxHandle *h, const char *how,
                            ImapSearchKey *filter);

ImapResponse imap_mbox_uid_search(ImapMboxHandle *handle, ImapSearchKey *key,
                                  void (*cb)(unsigned uid, void *),
                                  void *cb_data);

ImapResponse imap_mbox_sort_msgno(ImapMboxHandle *handle, ImapSortKey key,
                                  int ascending, int *msgno, unsigned cnt);
ImapResponse imap_mbox_sort_filter(ImapMboxHandle *handle, ImapSortKey key,
                                   int ascending, ImapSearchKey *filter);

#endif /* __IMAP_COMMANDS_H__ */
