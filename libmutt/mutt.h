/*
 * Copyright (C) 1996-8 Michael R. Elkins <me@cs.hmc.edu>
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
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */ 

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>

#include "rfc822.h"
#include "hash.h"

#define TRUE 1
#define FALSE 0

#define HUGE_STRING	5120
#define LONG_STRING     1024
#define STRING          256
#define SHORT_STRING    128

/* flags for mutt_copy_header() */
#define CH_UPDATE	1      /* update the status and x-status fields? */
#define CH_WEED		(1<<1) /* weed the headers? */
#define CH_DECODE	(1<<2) /* do RFC1522 decoding? */
#define CH_XMIT		(1<<3) /* transmitting this message? */
#define CH_FROM		(1<<4) /* retain the "From " message separator? */
#define CH_PREFIX	(1<<5) /* use Prefix string? */
#define CH_NOSTATUS	(1<<6) /* supress the status and x-status fields */
#define CH_REORDER	(1<<7) /* Re-order output of headers */
#define CH_NONEWLINE	(1<<8) /* don't output terminating newline */
#define CH_MIME		(1<<9) /* ignore MIME fields */
#define CH_UPDATE_LEN	(1<<10) /* update Lines: and Content-Length: */
#define CH_TXTPLAIN	(1<<11) /* generate text/plain MIME headers */

/* flags for mutt_enter_string() */
#define  M_ALIAS 1      /* do alias "completion" by calling up the alias-menu */
#define  M_FILE  (1<<1) /* do file completion */
#define  M_EFILE (1<<2) /* do file completion, plus incoming folders */
#define  M_CMD   (1<<3) /* do completion on previous word */
#define  M_PASS  (1<<4) /* password mode (no echo) */
#define  M_CLEAR (1<<5) /* clear input if printable character is pressed */

/* flags for mutt_extract_token() */
#define M_EQUAL		1	/* treat '=' as a special */
#define M_CONDENSE	(1<<1)	/* ^(char) to control chars (macros) */
#define M_SPACE		(1<<2)  /* don't treat whitespace as a terminator */
#define M_QUOTE		(1<<3)	/* don't interpret quotes */
#define M_PATTERN	(1<<4)	/* !)|~ are terminators (for patterns) */
#define M_COMMENT	(1<<5)	/* don't reap comments */
#define M_NULL		(1<<6)  /* don't return NULL when \0 is reached */

/* flags for _mutt_system() */
#define M_DETACH_PROCESS	1	/* detach subprocess from group */

/* flags for _mutt_make_string() */
#define M_FORCESUBJ	(1<<0) /* print the subject even if it didn't change */
#define M_TREE		(1<<1) /* draw the thread tree */
#define M_MAKEPRINT	(1<<2) /* make sure that all chars are printable */

/* types for mutt_add_hook() */
#define M_FOLDERHOOK	1
#define M_MBOXHOOK	(1<<1)
#define M_SENDHOOK	(1<<2)
#define M_FCCHOOK	(1<<3)
#define M_SAVEHOOK	(1<<4)

enum
{
  /* modes for mutt_view_attachment() */
  M_REGULAR = 1,
  M_MAILCAP,
  M_AS_TEXT,

  /* action codes used by mutt_set_flag() and mutt_pattern_function() */
  M_ALL,
  M_NONE,
  M_NEW,
  M_OLD,
  M_REPLIED,
  M_READ,
  M_UNREAD,
  M_DELETE,
  M_UNDELETE,
  M_DELETED,
  M_FLAG,
  M_TAG,
  M_UNTAG,
  M_LIMIT,
  M_EXPIRED,
  M_SUPERSEDED,

  /* actions for mutt_pattern_comp/mutt_pattern_exec */
  M_AND,
  M_OR,
  M_TO,
  M_CC,
  M_SUBJECT,
  M_FROM,
  M_DATE,
  M_DATE_RECEIVED,
  M_ID,
  M_BODY,
  M_HEADER,
  M_SENDER,
  M_MESSAGE,
  M_SCORE,
  M_REFERENCE,
  M_RECIPIENT,
  M_LIST,
  M_PERSONAL_RECIP,
  M_PERSONAL_FROM,
  M_ADDRESS,

  /* Options for Mailcap lookup */
  M_EDIT,
  M_COMPOSE,
  M_PRINT,
  M_AUTOVIEW,

  /* Options for mutt_save_attachment */
  M_SAVE_APPEND
};

/* possible arguments to set_quadoption() */
enum
{
  M_NO,
  M_YES,
  M_ASKNO,
  M_ASKYES
};

/* quad-option vars */
enum
{


  OPT_USEMAILCAP,
  OPT_PRINT,
  OPT_INCLUDE,
  OPT_DELETE,
  OPT_MOVE,
  OPT_COPY,
  OPT_POSTPONE,
  OPT_REPLYTO,
  OPT_ABORT,
  OPT_RECALL,
  OPT_SUBJECT
};

/* flags to ci_send_message() */
#define SENDREPLY	(1<<0)
#define SENDGROUPREPLY	(1<<1)
#define SENDLISTREPLY	(1<<2)
#define SENDFORWARD	(1<<3)
#define SENDPOSTPONED	(1<<4)
#define SENDBATCH	(1<<5)
#define SENDMAILX	(1<<6)
#define SENDKEY		(1<<7)

/* boolean vars */
enum
{
  OPTPROMPTAFTER,
  OPTSTATUSONTOP,
  OPTALLOW8BIT,
  OPTASCIICHARS,
  OPTMETOO,
  OPTEDITHDRS,
  OPTARROWCURSOR,
  OPTASKCC,
  OPTHEADER,
  OPTREVALIAS,
  OPTREVNAME,
  OPTFORCENAME,
  OPTSAVEEMPTY,
  OPTPAGERSTOP,
  OPTSIGDASHES,
  OPTASKBCC,
  OPTAUTOEDIT,
  OPTMARKOLD,
  OPTCONFIRMCREATE,
  OPTCONFIRMAPPEND,
  OPTPOPDELETE,
  OPTSAVENAME,
  OPTTHOROUGHSRC,
  OPTTILDE,
  OPTMIMEFWD,
  OPTMARKERS,
  OPTFCCATTACH,
  OPTATTACHSPLIT,
  OPTPIPESPLIT,
  OPTPIPEDECODE,
  OPTREADONLY,
  OPTRESOLVE,
  OPTSTRICTTHREADS,
  OPTAUTOTAG,
  OPTBEEP,
  OPTHELP,
  OPTHDRS,
  OPTWEED,
  OPTWRAP,
  OPTCHECKNEW,
  OPTFASTREPLY,
  OPTWAITKEY,
  OPTIGNORELISTREPLYTO,
  OPTSAVEADDRESS,
  OPTSUSPEND,
  OPTSORTRE,
  OPTUSEDOMAIN,
  OPTUSEFROM,
  OPTUSE8BITMIME,
  OPTFORWDECODE,
  OPTFORWQUOTE,
  OPTBEEPNEW,
  OPTMENUSCROLL,	/* scroll menu instead of implicit next-page */
  OPTMETAKEY,		/* interpret ALT-x as ESC-x */
  OPTAUXSORT,		/* (pseudo) using auxillary sort function */
  OPTFORCEREFRESH,	/* (pseudo) refresh even during macros */
  OPTLOCALES,		/* (pseudo) set if user has valid locale definition */
  OPTNOCURSES,		/* (pseudo) when sending in batch mode */
  OPTNEEDREDRAW,	/* (pseudo) to notify caller of a submenu */
  OPTSEARCHREVERSE,	/* (pseudo) used by ci_search_command */
  OPTMSGERR,		/* (pseudo) used by mutt_error/mutt_message */
  OPTSEARCHINVALID,	/* (pseudo) used to invalidate the search pat */
  OPTSIGNALSBLOCKED,	/* (pseudo) using by mutt_block_signals () */
  OPTNEEDRESORT,	/* (pseudo) used to force a re-sort */
  OPTVIEWATTACH,	/* (pseudo) signals that we are viewing attachments */
  OPTFORCEREDRAWINDEX,	/* (pseudo) used to force a redraw in the main index */
  OPTFORCEREDRAWPAGER,	/* (pseudo) used to force a redraw in the pager */
  OPTSORTSUBTHREADS,	/* (pseudo) used when $sort_aux changes */
  OPTNEEDRESCORE,	/* (pseudo) set when the `score' command is used */






  OPTMAX
};

#define mutt_bit_alloc(n) calloc ((n + 7) / 8, sizeof (char))
#define mutt_bit_set(v,n) v[n/8] |= (1 << (n % 8))
#define mutt_bit_unset(v,n) v[n/8] &= ~(1 << (n % 8))
#define mutt_bit_toggle(v,n) v[n/8] ^= (1 << (n % 8))
#define mutt_bit_isset(v,n) (v[n/8] & (1 << (n % 8)))

#define set_option(x) mutt_bit_set(Options,x)
#define unset_option(x) mutt_bit_unset(Options,x)
#define toggle_option(x) mutt_bit_toggle(Options,x)
#define option(x) mutt_bit_isset(Options,x)

/* Bit fields for ``Signals'' */
#define S_INTERRUPT (1<<1)
#define S_SIGWINCH  (1<<2)

typedef struct list_t
{
  char *data;
  struct list_t *next;
} LIST;

#define mutt_new_list() safe_calloc (1, sizeof (LIST))
void mutt_add_to_list (LIST **, const char *);
void mutt_free_list (LIST **);
int mutt_matches_ignore (const char *, LIST *);

/* add an element to a list */
LIST *mutt_add_list (LIST *, const char *);

void mutt_init (int, LIST *);

typedef struct alias
{
  char *name;
  ADDRESS *addr;
  struct alias *next;
} ALIAS;

typedef struct envelope
{
  ADDRESS *return_path;
  ADDRESS *from;
  ADDRESS *to;
  ADDRESS *cc;
  ADDRESS *bcc;
  ADDRESS *sender;
  ADDRESS *reply_to;
  ADDRESS *mail_followup_to;
  char *subject;
  char *real_subj;		/* offset of the real subject */
  char *message_id;
  char *supersedes;
  LIST *references;		/* message references (in reverse order) */
  LIST *userhdrs;		/* user defined headers */
} ENVELOPE;

typedef struct parameter
{
  char *attribute;
  char *value;
  struct parameter *next;
} PARAMETER;

/* Information that helps in determing the Content-* of an attachment */
typedef struct content
{
  long hibin;              /* 8-bit characters */
  long lobin;              /* unprintable 7-bit chars (eg., control chars) */
  long ascii;              /* number of ascii chars */
  long linemax;            /* length of the longest line in the file */
  unsigned int space : 1;  /* whitespace at the end of lines? */
  unsigned int binary : 1; /* long lines, or CR not in CRLF pair */
  unsigned int from : 1;   /* has a line beginning with "From "? */
  unsigned int dot : 1;    /* has a line consisting of a single dot? */
} CONTENT;

typedef struct body
{
  char *subtype;                /* content-type subtype */
  PARAMETER *parameter;         /* parameters of the content-type */
  char *description;            /* content-description */
  char *form_name;		/* Content-Disposition form-data name param */
  long hdr_offset;              /* offset in stream where the headers begin.
				 * this info is used when invoking metamail,
				 * where we need to send the headers of the
				 * attachment
				 */
  long offset;                  /* offset where the actual data begins */
  long length;                  /* length (in bytes) of attachment */
  char *filename;               /* when sending a message, this is the file
				 * to which this structure refers
				 */
  char *d_filename;		/* filename to be used for the 
				 * content-disposition header.
				 * If NULL, filename is used 
				 * instead.
				 */
  CONTENT *content;             /* structure used to store detailed info about
				 * the content of the attachment.  this is used
				 * to determine what content-transfer-encoding
				 * is required when sending mail.
				 */
  struct body *next;            /* next attachment in the list */
  struct body *parts;           /* parts of a multipart or message/rfc822 */
  struct header *hdr;		/* header information for message/rfc822 */

  unsigned int type : 3;        /* content-type primary type */
  unsigned int encoding : 3;    /* content-transfer-encoding */
  unsigned int disposition : 2; /* content-disposition */
  unsigned int use_disp : 1;    /* Content-Disposition field printed? */
  unsigned int unlink : 1;      /* flag to indicate the the file named by
				 * "filename" should be unlink()ed before
				 * free()ing this structure
				 */
  unsigned int tagged : 1;

} BODY;

typedef struct header
{
  unsigned int mime : 1;    /* has a Mime-Version header? */
  unsigned int mailcap : 1; /* requires mailcap to display? */
  unsigned int flagged : 1; /* marked important? */
  unsigned int tagged : 1;
  unsigned int deleted : 1;
  unsigned int changed : 1;
  unsigned int old : 1;
  unsigned int read : 1;
  unsigned int expired : 1; /* already expired? */
  unsigned int superseded : 1; /* got superseded? */

  unsigned int pgp : 3;
  unsigned int replied : 1;
  unsigned int subject_changed : 1; /* used for threading */
  unsigned int display_subject : 1; /* used for threading */
  unsigned int fake_thread : 1;     /* no ref matched, but subject did */
  unsigned int threaded : 1;        /* message has been threaded */

  /* timezone of the sender of this message */
  unsigned int zhours : 5;
  unsigned int zminutes : 6;
  unsigned int zoccident : 1;

  /* bits used for caching when searching */
  unsigned int searched : 1;
  unsigned int matched : 1;

  time_t date_sent;     /* time when the message was sent (UTC) */
  time_t received;      /* time when the message was placed in the mailbox */
  long offset;          /* where in the stream does this message begin? */
  int lines;		/* how many lines in the body of this message? */
  int index;		/* the absolute (unsorted) message number */
  int msgno;		/* number displayed to the user */
  int virtual;		/* virtual message number */
  int score;
  ENVELOPE *env;	/* envelope information */
  BODY *content;	/* list of MIME parts */
  char *path;
  
  /* the following are used for threading support */
  struct header *parent;
  struct header *child;  /* decendants of this message */
  struct header *next;   /* next message in this thread */
  struct header *prev;   /* previous message in thread */
  struct header *last_sort; /* last message in subthread, for secondary SORT_LAST */
  char *tree;            /* character string to print thread tree */

} HEADER;

typedef struct
{
  char *path;
  FILE *fp;
  time_t mtime;
  off_t size;
  HEADER **hdrs;
  HEADER *tree;			/* top of thread tree */
  HASH *id_hash;		/* hash table by msg id */
  HASH *subj_hash;		/* hash table by subject */
  int *v2r;			/* mapping from virtual to real msgno */
  int hdrmax;			/* number of pointers in hdrs */
  int msgcount;			/* number of messages in the mailbox */
  int vcount;			/* the number of virtual messages */
  int tagged;			/* how many messages are tagged? */
  int new;			/* how many new messages? */
  int unread;			/* how many unread messages? */
  int deleted;			/* how many deleted messages */
  int flagged;			/* how many flagged messages */
  int msgnotreadyet;		/* which msg "new" in pager, -1 if none */
#ifdef USE_IMAP
  void *data;			/* driver specific data */
  int fd;
#endif /* USE_IMAP */

  short magic;			/* mailbox type */

  unsigned int locked : 1;	/* is the mailbox locked? */
  unsigned int changed : 1;	/* mailbox has been modified */
  unsigned int readonly : 1;    /* don't allow changes to the mailbox */
  unsigned int dontwrite : 1;   /* dont write the mailbox on close */
  unsigned int append : 1;	/* mailbox is opened in append mode */
  unsigned int setgid : 1;
  unsigned int quiet : 1;	/* inhibit status messages? */
  unsigned int revsort : 1;	/* mailbox sorted in reverse? */

} CONTEXT;

typedef struct attachptr
{
  BODY *content;
  char *tree;
  int level;
} ATTACHPTR;

typedef struct
{
  FILE *fpin;
  FILE *fpout;
  char *prefix;
  int flags;
} STATE;

/* flags for the STATE struct */
#define M_DISPLAY	(1<<0) /* output is displayed to the user */





#define state_puts(x,y) fputs(x,(y)->fpout)
#define state_putc(x,y) fputc(x,(y)->fpout)

#include "protos.h"
#include "globals.h"
