#ifndef __IMAP_H__
#define __IMAP_H__ 1

#include <time.h>

typedef enum {
  IMAP_SUCCESS = 0,  /* action succeeded*/
  IMAP_NOMEM,          /* no memory */
  IMAP_CONNECT_FAILED, /* transport level connect failed */
  IMAP_PROTOCOL_ERROR, /* usually unexpected server response */
  IMAP_AUTH_FAILURE, /* authentication failure */
  IMAP_AUTH_UNAVAIL, /* no supported authentication method available */
  IMAP_SELECT_FAILED /* SELECT command failed */
} ImapResult;

typedef enum { 
  IMMBXF_NOINFERIORS=0,
  IMMBXF_NOSELECT, 
  IMMBXF_MARKED,
  IMMBXF_UNMARKED,
  IMMBXF_LAST
} ImapMboxFlag;
typedef char ImapMboxFlags[IMMBXF_LAST];

/* see section 2.3.2 of draft-crispin-imapv-20.txt */
typedef enum {
  IMSGF_SEEN = 0,
  IMSGF_ANSWERED,
  IMSGF_FLAGGED,
  IMSGF_DELETED,
  IMSGF_DRAFT,
  IMSGF_RECENT,
  IMSGF_LAST
} ImapMsgFlag;

typedef char ImapMsgFlags[IMSGF_LAST];

#define IMSG_FLAG_SEEN(flags)     (flags[IMSGF_SEEN])
#define IMSG_FLAG_ANSWERED(flags) (flags[IMSGF_ANSWERED])
#define IMSG_FLAG_FLAGGED(flags)  (flags[IMSGF_FLAGGED])
#define IMSG_FLAG_DELETED(flags)  (flags[IMSGF_DELETED])
#define IMSG_FLAG_DRAFT(flags)    (flags[IMSGF_DRAFT])
#define IMSG_FLAG_RECENT(flags)   (flags[IMSGF_RECENT])

/* ImapMessage: Structure used to pass around and cache IMAP message data.
 */
typedef guint32 ImapUID;
typedef time_t ImapDate;

typedef struct ImapAddress_ {
    char *addr_name;
    char *addr_adl;
    char *addr_mailbox;
    char *addr_host;
} ImapAddress;

typedef struct ImapEnvelope_ {
  ImapDate date;
  gchar *subject; /* mime encoded, 7-bit subject as fetched from server */
  ImapAddress *from;
  ImapAddress *sender;
  ImapAddress *replyto;
  GList *to_list;
  GList *cc_list;
  GList *bcc_list;
  gchar *in_reply_to;
  gchar *message_id;
} ImapEnvelope;

typedef struct ImapMessage_ {
  ImapUID      uid;
  ImapMsgFlags flags;
  ImapEnvelope *envelope;
  ImapDate     internal_date;
  /* rfc822, rfc822.header, rfc822.text, body, body structure, body
     section have yet to be implemented. Currently, they will be ignored 
  */
  gchar *body;
  gchar *fetched_header_fields;
} ImapMessage;

typedef struct _ImapMboxHandle      ImapMboxHandle;

ImapEnvelope *imap_envelope_new(void);
void imap_envelope_free(ImapEnvelope *);

ImapMessage *imap_message_new(void);
void imap_message_free(ImapMessage *);

const char *lbi_strerror(ImapResult rc);

#endif /* __IMAP_H__ */
