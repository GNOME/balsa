#ifndef __IMAP_H__
#define __IMAP_H__ 1
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

#include <time.h>

typedef enum {
  IMAP_SUCCESS = 0,  /* action succeeded*/
  IMAP_NOMEM,          /* no memory */
  IMAP_CONNECT_FAILED, /* transport level connect failed */
  IMAP_PROTOCOL_ERROR, /* usually unexpected server response */
  IMAP_AUTH_FAILURE, /* authentication failure */
  IMAP_AUTH_UNAVAIL, /* no supported authentication method available */
  IMAP_UNSECURE,     /* secure connection was requested but could not
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

/* ImapAddress is an anddress as seen by IMAP/RFC2822.
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
  GHashTable         *params;
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
} ImapMessage;

typedef struct _ImapMboxHandle      ImapMboxHandle;

ImapEnvelope *imap_envelope_new(void);
void imap_envelope_free(ImapEnvelope *);

ImapBody* imap_body_new(void);
void imap_body_free(ImapBody* body);

void imap_body_set_desc(ImapBody* body, char* str);
void imap_body_add_param(ImapBody *body, char *key, char *val);
void imap_body_append_part(ImapBody* body, ImapBody* sibling);
void imap_body_append_child(ImapBody* body, ImapBody* child);
void imap_body_set_id(ImapBody *body, char *id);


ImapMessage *imap_message_new(void);
void imap_message_free(ImapMessage *);

const char *lbi_strerror(ImapResult rc);

#endif /* __IMAP_H__ */
