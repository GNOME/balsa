#ifndef __IMAP_FETCH_H__
#define __IMAP_FETCH_H__

ImapResponse imap_mbox_handle_fetch(ImapMboxHandle* handle, const gchar *seq, 
                                    const gchar* headers[]);
ImapResponse imap_mbox_handle_fetch_env(ImapMboxHandle* handle,
                                        const gchar *seq);
ImapResponse imap_mbox_handle_fetch_body(ImapMboxHandle* handle,
                                        const gchar *seq);

#endif /* __IMAP_FETCH_H__ */
