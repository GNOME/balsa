#ifndef __IMAP_COMMANDS_H__
#define __IMAP_COMMANDS_H__ 1

/* Any-State */
int imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap);
ImapResult imap_mbox_handle_noop(ImapMboxHandle *r);

/* Non-Authenticated State */

/* Authenticated State */
ImapResult imap_mbox_select(ImapMboxHandle* handle, const char* mbox,
			    gboolean *readonly_mbox);
ImapResult imap_mbox_examine(ImapMboxHandle* handle, const char* mbox);
ImapResult imap_mbox_create(ImapMboxHandle* handle, const char* new_mbox);
ImapResult imap_mbox_delete(ImapMboxHandle* handle, const char* mbox);
ImapResult imap_mbox_rename(ImapMboxHandle* handle,
			    const char* old_mbox,
			    const char* new_mbox);
ImapResult imap_mbox_subscribe(ImapMboxHandle* handle,
			       const char* mbox, gboolean subscribe);
ImapResult imap_mbox_list(ImapMboxHandle *r, const char*what, const char*how);
ImapResult imap_mbox_lsub(ImapMboxHandle *r, const char*what, const char*how);
ImapResult imap_mbox_append(ImapMboxHandle *handle, const char* mbox,
			    ImapMsgFlags flags,
			    size_t len, const char *msgtext);

/* Selected State */
ImapResult imap_mbox_search(ImapMboxHandle *h, const char* query);
ImapResult imap_mbox_uid_search(ImapMboxHandle *h, const char* query);
ImapResult imap_mbox_store_flag(ImapMboxHandle *r, int seq,
                                ImapMsgFlag flg, gboolean state);

/* Experimental/Expansion */
ImapResult imap_mbox_scan(ImapMboxHandle *r, const char*what, const char*str);
ImapResult imap_mbox_thread(ImapMboxHandle *h, const char *how);
#endif /* __IMAP_COMMANDS_H__ */
