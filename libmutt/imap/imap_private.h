/*
 * Copyright (C) 1996-9 Brandon Long <blong@fiction.net>
 * Copyright (C) 1999-2000 Brendan Cully <brendan@kublai.com>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

#ifndef _IMAP_PRIVATE_H
#define _IMAP_PRIVATE_H 1

#include "imap.h"
#include "mutt_socket.h"

/* -- symbols -- */
#define IMAP_PORT 143
#define IMAP_SSL_PORT 993

/* logging levels */
#define IMAP_LOG_CMD  2
#define IMAP_LOG_LTRL 4
#define IMAP_LOG_PASS 5

/* IMAP command responses */
#define IMAP_CMD_DONE     (0)
#define IMAP_CMD_FAIL     (-1)
#define IMAP_CMD_NO       (-2)
#define IMAP_CMD_CONTINUE (1)
#define IMAP_CMD_RESPOND  (2)

/* number of entries in the hash table */
#define IMAP_CACHE_LEN 10

#define SEQLEN 5

#define IMAP_REOPEN_ALLOW    (1<<0)
#define IMAP_EXPUNGE_PENDING (1<<1)
#define IMAP_NEWMAIL_PENDING (1<<2)

/* imap_exec flags (see imap_exec) */
#define IMAP_CMD_FAIL_OK (1<<0)
#define IMAP_CMD_PASS    (1<<1)

enum
{
  IMAP_FATAL = 1,
  IMAP_BYE,
  IMAP_REOPENED
};

enum
{
  /* States */
  IMAP_DISCONNECTED = 0,
  IMAP_CONNECTED,
  IMAP_AUTHENTICATED,
  IMAP_SELECTED
};

enum
{
  /* Namespace types */
  IMAP_NS_PERSONAL = 0,
  IMAP_NS_OTHER,
  IMAP_NS_SHARED
};

/* ACL Rights */
enum
{
  IMAP_ACL_LOOKUP = 0,
  IMAP_ACL_READ,
  IMAP_ACL_SEEN,
  IMAP_ACL_WRITE,
  IMAP_ACL_INSERT,
  IMAP_ACL_POST,
  IMAP_ACL_CREATE,
  IMAP_ACL_DELETE,
  IMAP_ACL_ADMIN,

  RIGHTSMAX
};

/* Capabilities we are interested in */
enum
{
  IMAP4 = 0,
  IMAP4REV1,
  STATUS,
  ACL,				/* RFC 2086: IMAP4 ACL extension */
  NAMESPACE,                   	/* RFC 2342: IMAP4 Namespace */
  ACRAM_MD5,			/* RFC 2195: CRAM-MD5 authentication */
  AGSSAPI,			/* RFC 1731: GSSAPI authentication */
  AUTH_ANON,			/* AUTH=ANONYMOUS */
  STARTTLS,			/* RFC 2595: STARTTLS */
  LOGINDISABLED,		/*           LOGINDISABLED */

  CAPMAX
};

/* imap_conn_find flags */
#define M_IMAP_CONN_NONEW    (1<<0)
#define M_IMAP_CONN_NOSELECT (1<<1)

/* -- data structures -- */
typedef struct
{
  unsigned int uid;
  char* path;
} IMAP_CACHE;

typedef struct 
{
  int type;
  int listable;
  char *prefix;
  char delim;
  int home_namespace;
  /* We get these when we check if namespace exists - cache them */
  int noselect;			
  int noinferiors;
} IMAP_NAMESPACE_INFO;

typedef struct
{
  /* This data is specific to a CONNECTION to an IMAP server */
  CONNECTION *conn;
  unsigned char state;
  unsigned char status;
  unsigned char capabilities[(CAPMAX + 7)/8];
  char seq[SEQLEN+1];
  /* command input buffer */
  char* buf;
  unsigned int blen;
  /* let me explain capstr: SASL needs the capability string (not bits).
   * we have 3 options:
   *   1. rerun CAPABILITY inside SASL function.
   *   2. build appropriate CAPABILITY string by reverse-engineering from bits.
   *   3. keep a copy until after authentication.
   * I've chosen (3) for now. (2) might not be too bad, but it involves
   * tracking all possible capabilities. bah. (1) I don't like because
   * it's just no fun to get the same information twice */
  char* capstr;

  /* The following data is all specific to the currently SELECTED mbox */
  char delim;
  CONTEXT *ctx;
  char *mailbox;
  unsigned short check_status;
  unsigned char reopen;
  unsigned char rights[(RIGHTSMAX + 7)/8];
  unsigned int newMailCount;
  IMAP_CACHE cache[IMAP_CACHE_LEN];
  int noclose : 1;
  
  /* all folder flags - system flags AND keywords */
  LIST *flags;
} IMAP_DATA;

/* -- macros -- */
#define CTX_DATA ((IMAP_DATA *) ctx->data)

/* -- private IMAP functions -- */
/* imap.c */
int imap_create_mailbox (IMAP_DATA* idata, char* mailbox);
int imap_make_msg_set (IMAP_DATA* idata, char* buf, size_t buflen, int flag,
  int changed);
int imap_open_connection (IMAP_DATA* idata);
IMAP_DATA* imap_conn_find (const ACCOUNT* account, int flags);
time_t imap_parse_date (char* s);
int imap_parse_list_response(IMAP_DATA* idata, char** name, int* noselect,
  int* noinferiors, char* delim);
int imap_read_literal (FILE* fp, IMAP_DATA* idata, long bytes);
void imap_expunge_mailbox (IMAP_DATA* idata);
void imap_logout (IMAP_DATA* idata);

/* auth.c */
int imap_authenticate (IMAP_DATA* idata);

/* command.c */
int imap_cmd_start (IMAP_DATA* idata, const char* cmd);
int imap_cmd_step (IMAP_DATA* idata);
int imap_code (const char* s);
int imap_exec (IMAP_DATA* idata, const char* cmd, int flags);

/* message.c */
void imap_add_keywords (char* s, HEADER* keywords, LIST* mailbox_flags, size_t slen);
void imap_free_header_data (void** data);
int imap_read_headers (IMAP_DATA* idata, int msgbegin, int msgend);

/* util.c */
int imap_continue (const char* msg, const char* resp);
void imap_error (const char* where, const char* msg);
char* imap_fix_path (IMAP_DATA* idata, char* mailbox, char* path, 
  size_t plen);
int imap_get_literal_count (const char* buf, long* bytes);
char* imap_get_qualifier (char* buf);
char* imap_next_word (char* s);
void imap_qualify_path (char *dest, size_t len, IMAP_MBOX *mx, char* path);
void imap_quote_string (char* dest, size_t slen, const char* src);
void imap_unquote_string (char* s);
void imap_munge_mbox_name (char *dest, size_t dlen, const char *src);
void imap_unmunge_mbox_name (char *s);
int imap_wordcasecmp(const char *a, const char *b);

/* utf7.c */
void imap_utf7_encode (char **s);
void imap_utf7_decode (char **s);

#endif
