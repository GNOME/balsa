#ifndef __IMAP_H__
#define __IMAP_H__ 1
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

#include <glib-object.h>

G_DECLARE_FINAL_TYPE(ImapMboxHandle,
                     imap_mbox_handle,
                     IMAP,
                     MBOX_HANDLE,
                     GObject);

/* connection states, as defined in rfc-2060, 3 */
typedef enum {
  IMHS_DISCONNECTED,
  IMHS_CONNECTED, /* non authenticated */
  IMHS_AUTHENTICATED,
  IMHS_SELECTED
} ImapConnectionState;


typedef enum {
  IMAP_SUCCESS = 0,    /* action succeeded*/
  IMAP_NOMEM,          /* no memory */
  IMAP_CONNECT_FAILED, /* transport level connect failed */
  IMAP_PROTOCOL_ERROR, /* usually unexpected server response */
  IMAP_AUTH_FAILURE,   /* authentication failure */
  IMAP_AUTH_CANCELLED, /* authentication cancelled, do not retry */
  IMAP_AUTH_UNAVAIL,   /* no supported authentication method available */
  IMAP_UNSECURE,       /* secure connection was requested but could not
                        * be established. */
  IMAP_SELECT_FAILED /* SELECT command failed */
} ImapResult;

typedef enum { 
  IMLIST_MARKED = 0,
  IMLIST_UNMARKED,
  IMLIST_NOSELECT, 
  IMLIST_NOINFERIORS,
  IMLIST_HASCHILDREN,
  IMLIST_HASNOCHILDREN,
  IMLIST_LAST
} ImapMboxFlag;

typedef unsigned ImapMboxFlags;
#define IMAP_MBOX_HAS_FLAG(flg,f) ((flg) &  (1 << (f)))
#define IMAP_MBOX_SET_FLAG(flg,f) ((flg) |= (1 << (f)))

/* see section 2.3.2 of draft-crispin-imapv-20.txt */
typedef enum {
  IMSGF_SEEN     = 1 << 0,
  IMSGF_ANSWERED = 1 << 1,
  IMSGF_FLAGGED  = 1 << 2,
  IMSGF_DELETED  = 1 << 3,
  IMSGF_DRAFT    = 1 << 4,
  IMSGF_RECENT   = 1 << 5
} ImapMsgFlag;

typedef unsigned ImapMsgFlags;

#define IMSG_FLAG_SEEN(flags)     ((flags)&IMSGF_SEEN)
#define IMSG_FLAG_ANSWERED(flags) ((flags)&IMSGF_ANSWERED)
#define IMSG_FLAG_FLAGGED(flags)  ((flags)&IMSGF_FLAGGED)
#define IMSG_FLAG_DELETED(flags)  ((flags)&IMSGF_DELETED)
#define IMSG_FLAG_DRAFT(flags)    ((flags)&IMSGF_DRAFT)
#define IMSG_FLAG_RECENT(flags)   ((flags)&IMSGF_RECENT)

#define IMSG_FLAG_SET(flags, flag) do{ (flags) |= (flag); }while(0)
#define IMSG_FLAG_UNSET(flags, flag) do{ (flags) &= ~(flag); }while(0)

#define IMAP_FLAGS_EMPTY (0)

typedef enum {
  IMSO_MSGNO,
  IMSO_ARRIVAL,
  IMSO_CC,
  IMSO_DATE,
  IMSO_FROM,
  IMSO_SIZE,
  IMSO_SUBJECT,
  IMSO_TO
} ImapSortKey;

/* ImapMessage: Structure used to pass around and cache IMAP message data.
 */
typedef unsigned ImapUID;
typedef time_t ImapDate;

/* ImapAddress is an address as seen by IMAP/RFC2822.
   ImapAddress conventions:
   - mailbox == NULL: begin group (See RFC2822, sec 3.4), name
     contains group's name.

   - name == NULL and mailbox == NULL: end group.
   other cases are ordinary e-mail mailboxes.
*/
typedef struct ImapAddress_ ImapAddress;
struct ImapAddress_ {
  gchar * name;      /* comment */
  gchar * addr_spec; /* the "real" mailbox */
  ImapAddress *next;
};

ImapAddress *imap_address_new(gchar *name, gchar *addr_spec);
void imap_address_free(ImapAddress* addr);

typedef struct ImapEnvelope_ {
  ImapDate date;  /* as in the message header */
  gchar *subject; /* mime encoded, 7-bit subject as fetched from server */
  ImapAddress *from;
  ImapAddress *sender;
  ImapAddress *replyto;
  ImapAddress *to;
  ImapAddress *cc;
  ImapAddress *bcc;
  gchar *in_reply_to;
  gchar *message_id;
} ImapEnvelope;

/* header fetching types */
typedef enum {
  IMFETCH_BODYSTRUCT = 1<<0,
  IMFETCH_ENV        = 1<<1,
  IMFETCH_FLAGS      = 1<<2,
  IMFETCH_RFC822SIZE = 1<<3,
  IMFETCH_UID        = 1<<4,
  IMFETCH_CONTENT_TYPE = 1<<5,
  IMFETCH_REFERENCES   = 1<<6,
  IMFETCH_LIST_POST    = 1<<7,
  IMFETCH_RFC822HEADERS = 1<<8,
  IMFETCH_RFC822HEADERS_SELECTED = 1<<9, /* non-overlapping with ENV. */
  IMFETCH_HEADER_MASK  = (IMFETCH_CONTENT_TYPE | IMFETCH_REFERENCES |
                          IMFETCH_LIST_POST)
} ImapFetchType;

/* body fetching option */
typedef enum {
  IMFB_NONE,
  IMFB_HEADER, /* preceed with BODY[section.HEADER] */
  IMFB_MIME    /* preceed with BODY[section.MIME]   */
} ImapFetchBodyOptions;

typedef struct ImapBody_ ImapBody;

typedef enum { IMBMEDIA_MULTIPART,
               IMBMEDIA_APPLICATION,
               IMBMEDIA_AUDIO,
               IMBMEDIA_IMAGE, 
               IMBMEDIA_MESSAGE_RFC822,
               IMBMEDIA_MESSAGE_OTHER,
               IMBMEDIA_TEXT, 
               IMBMEDIA_OTHER } ImapMediaBasic;

typedef enum { IMBENC_UNUSED,
  IMBENC_7BIT, IMBENC_8BIT, IMBENC_BINARY, IMBENC_BASE64, 
  IMBENC_QUOTED, IMBENC_OTHER } ImapBodyEncoding;

struct ImapBodyExt1Part_ {
    char *md5;
};
typedef struct ImapBodyExt1Part_ ImapBodyExt1Part;

typedef enum {
    IMBDISP_INLINE,
    IMBDISP_ATTACHMENT,
    IMBDISP_OTHER
} ImapBodyDisposition;
struct ImapBodyExtMPart_ {
  /* GHashTable         *params; not used: we put everything in ImapBody */
  GSList	     *lang;
};
typedef struct ImapBodyExtMPart_ ImapBodyExtMPart;

struct ImapBody_ {
  ImapBodyEncoding encoding;
  ImapMediaBasic media_basic;
  gchar *media_basic_name;
  gchar *media_subtype;
  GHashTable *params;
  unsigned octets;
  unsigned lines;
  char *content_id;
  char *desc;
  ImapEnvelope *envelope;/* used only if media/basic == MESSAGE */
  ImapBodyDisposition content_dsp;
  GHashTable *dsp_params;
  char *content_dsp_other;
  char *content_uri;

  union {
    ImapBodyExt1Part onepart;
    ImapBodyExtMPart mpart;
  } ext;

  ImapBody *child; /* not null for eg. message/rfc822 */
  ImapBody *next;
};

typedef struct ImapMessage_ {
  ImapUID      uid;
  ImapMsgFlags flags;
  ImapEnvelope *envelope;
  ImapBody     *body;
  ImapDate     internal_date; /* delivery date */
  int rfc822size;
  /* rfc822, rfc822.header, rfc822.text, body, body structure, body
     section have yet to be implemented. Currently, they will be ignored.
     Almost.
  */
  gchar *fetched_header_fields;
  ImapFetchType available_headers; /* Used internally. Informs, what
                                    * fields are supposed to be
                                    * already in
                                    * fetched_header_fields. Checking
                                    * the content of the string is not
                                    * sufficient: we would keep
                                    * sending requests if the header
                                    * in question is not present. */
} ImapMessage;

ImapEnvelope *imap_envelope_new(void);
void imap_envelope_free(ImapEnvelope *);
gchar* imap_envelope_to_string(const ImapEnvelope* env);
ImapEnvelope* imap_envelope_from_string(const gchar *s);

ImapBody* imap_body_new(void);
void imap_body_free(ImapBody* body);
gchar* imap_body_to_string(const ImapBody *body);
ImapBody* imap_body_from_string(const gchar *s);

void imap_body_set_desc(ImapBody* body, char* str);
void imap_body_add_param(ImapBody *body, char *key, char *val);
void imap_body_append_part(ImapBody* body, ImapBody* sibling);
void imap_body_append_child(ImapBody* body, ImapBody* child);
void imap_body_set_id(ImapBody *body, char *id);

/* user events below usually require application's or user's
 *  intervention */
typedef enum {
  IME_GET_USER_PASS,
  IME_GET_USER,
  IME_TLS_VERIFY_ERROR,
  IME_TLS_NO_PEER_CERT,
  IME_TLS_WEAK_CIPHER,
  IME_TIMEOUT
} ImapUserEventType;

typedef void (*ImapUserCb)(ImapUserEventType ue, void *arg, ...);

ImapMessage *imap_message_new(void);
void imap_message_free(ImapMessage *);
void imap_mbox_handle_msg_deserialize(ImapMboxHandle *h, unsigned msgno,
                                      void *data);
void*        imap_message_serialize(ImapMessage *);
ImapMessage* imap_message_deserialize(void *data);
size_t imap_serialized_message_size(void *data);

/* RFC 4314: IMAP ACL's */
typedef enum {
  IMAP_ACL_NONE = 0,
  IMAP_ACL_LOOKUP = 1<<0,   /* 'l': visible to LIST, LSUB; SUBSCRIBE mailbox*/
  IMAP_ACL_READ = 1<<1,     /* 'r': SELECT the mailbox, perform STATUS */
  IMAP_ACL_SEEN = 1<<2,     /* 's': keep seen/unseen across sessions */
  IMAP_ACL_WRITE = 1<<3,    /* 'w': write */
  IMAP_ACL_INSERT = 1<<4,   /* 'i': APPEND, COPY into mailbox */
  IMAP_ACL_POST = 1<<5,     /* 'p': send mail to submission address */
  IMAP_ACL_CREATE = 1<<6,   /* 'k': CREATE, RENAME */
  IMAP_ACL_DELETE = 1<<7,   /* 'x': DELETE, RENAME */
  IMAP_ACL_DELMSG = 1<<8,   /* 't': delete messages (\DELETED flag) */
  IMAP_ACL_EXPUNGE = 1<<9,  /* 'e': EXPUNGE */
  IMAP_ACL_ADMIN = 1<<10,   /* 'a': administer (SETACL/DELETEACL/GETACL/LISTRIGHTS) */
  IMAP_ACL_OBS_CREATE = 1<<11,  /* 'c': RFC 2086 "create" */
  IMAP_ACL_OBS_DELETE = 1<<12   /* 'd': RFC 2086 "delete" */
} ImapAclType;

#define IMAP_ACL_CAN_WRITE  (IMAP_ACL_WRITE | IMAP_ACL_INSERT | IMAP_ACL_CREATE | \
                             IMAP_ACL_DELETE | IMAP_ACL_DELMSG | IMAP_ACL_EXPUNGE)
#define IMAP_ACL_OBS_CAN_WRITE (IMAP_ACL_WRITE | IMAP_ACL_INSERT | \
                                IMAP_ACL_OBS_CREATE | IMAP_ACL_OBS_DELETE)
#define IMAP_RIGHTS_CAN_WRITE(rights) (((rights) & IMAP_ACL_CAN_WRITE) \
                                       == IMAP_ACL_CAN_WRITE \
                                       || ((rights) & IMAP_ACL_OBS_CAN_WRITE) \
                                       == IMAP_ACL_OBS_CAN_WRITE)

typedef struct {
  gchar* uid;
  ImapAclType acl;
} ImapUserAclType;

void imap_user_acl_free(ImapUserAclType *acl);

#endif /* __IMAP_H__ */
