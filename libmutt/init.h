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

#include "sort.h"
#include "buffy.h"

#define DT_MASK		0x0f
#define DT_BOOL		1 /* boolean option */
#define DT_NUM		2 /* a number */
#define DT_STR		3 /* a string */
#define DT_PATH		4 /* a pathname */
#define DT_QUAD		5 /* quad-option (yes/no/ask-yes/ask-no) */
#define DT_SORT		6 /* sorting methods */
#define DT_RX		7 /* regular expressions */
#define DT_MAGIC	8 /* mailbox type */

#define DTYPE(x) ((x) & DT_MASK)

/* subtypes */
#define DT_SUBTYPE_MASK	0xf0
#define DT_SORT_ALIAS	0x10

/* flags to parse_set() */
#define M_SET_INV	1	/* default is to invert all vars */
#define M_SET_UNSET	2	/* default is to unset all vars */

/* forced redraw types */
#define R_NONE			0
#define R_INDEX			(1<<0)
#define R_PAGER			(1<<1)
#define R_BOTH			(R_INDEX | R_PAGER)

struct option_t
{
  char *option;
  short type;
  short redraw;
  unsigned long data;
  size_t size; /* DT_STR and DT_PATH */
};

#define S_DECL(x) (unsigned long)x,sizeof(x)
#define P_DECL(x) (unsigned long)x,0
#define I_DECL(x) x,0

struct option_t MuttVars[] = {
  { "abort_nosubject",	DT_QUAD, R_NONE,	I_DECL(OPT_SUBJECT) },
  { "abort_unmodified",	DT_QUAD, R_NONE,	I_DECL(OPT_ABORT) },
  { "alias_file",	DT_PATH, R_NONE,	S_DECL(AliasFile) },
  { "alias_format",	DT_STR,  R_NONE,	S_DECL(AliasFmt) },
  { "allow_8bit",	DT_BOOL, R_NONE,	I_DECL(OPTALLOW8BIT) },
  { "alternates",	DT_RX,	 R_BOTH,	P_DECL(&Alternates) },
  { "arrow_cursor",	DT_BOOL, R_BOTH,	I_DECL(OPTARROWCURSOR) },
  { "ascii_chars",	DT_BOOL, R_BOTH,	I_DECL(OPTASCIICHARS) },
  { "askbcc",		DT_BOOL, R_NONE,	I_DECL(OPTASKBCC) },
  { "askcc",		DT_BOOL, R_NONE,	I_DECL(OPTASKCC) },
  { "attach_split",	DT_BOOL, R_NONE,	I_DECL(OPTATTACHSPLIT) },
  { "attach_sep",	DT_STR,	 R_NONE,	S_DECL(AttachSep) },
  { "attribution",	DT_STR,	 R_NONE,	S_DECL(Attribution) },
  { "autoedit",		DT_BOOL, R_NONE,	I_DECL(OPTAUTOEDIT) },
  { "auto_tag",		DT_BOOL, R_NONE,	I_DECL(OPTAUTOTAG) },
  { "beep",		DT_BOOL, R_NONE,	I_DECL(OPTBEEP) },
  { "beep_new",		DT_BOOL, R_NONE,	I_DECL(OPTBEEPNEW) },
  { "charset",		DT_STR,	 R_NONE,	S_DECL(Charset) },
  { "check_new",	DT_BOOL, R_NONE,	I_DECL(OPTCHECKNEW) },
  { "confirmappend",	DT_BOOL, R_NONE,	I_DECL(OPTCONFIRMAPPEND) },
  { "confirmcreate",	DT_BOOL, R_NONE,	I_DECL(OPTCONFIRMCREATE) },
  { "copy",		DT_QUAD, R_NONE,	I_DECL(OPT_COPY) },
  { "date_format",	DT_STR,	 R_BOTH,	S_DECL(DateFmt) },
  { "decode_format",	DT_STR,	 R_NONE,	S_DECL(DecodeFmt) },
  { "default_hook",	DT_STR,	 R_NONE,	S_DECL(DefaultHook) },
  { "delete",		DT_QUAD, R_NONE,	I_DECL(OPT_DELETE) },
  { "dsn_notify",	DT_STR,	 R_NONE,	S_DECL (DsnNotify) },
  { "dsn_return",	DT_STR,	 R_NONE,	S_DECL (DsnReturn) },
  { "edit_hdrs",	DT_BOOL, R_NONE,	I_DECL(OPTEDITHDRS) },
  { "editor",		DT_PATH, R_NONE,	S_DECL(Editor) },
  { "escape",		DT_STR,	 R_NONE,	S_DECL(EscChar) },
  { "fast_reply",	DT_BOOL, R_NONE,	I_DECL(OPTFASTREPLY) },
  { "fcc_attach",	DT_BOOL, R_NONE,	I_DECL(OPTFCCATTACH) },
  { "folder",		DT_PATH, R_NONE,	S_DECL(Maildir) },
  { "force_name",	DT_BOOL, R_NONE,	I_DECL(OPTFORCENAME) },
  { "forw_decode",	DT_BOOL, R_NONE,	I_DECL(OPTFORWDECODE) },
  { "forw_format",	DT_STR,	 R_NONE,	S_DECL(ForwFmt) },
  { "forw_quote",	DT_BOOL, R_NONE,	I_DECL(OPTFORWQUOTE) },
  { "hdr_format",	DT_STR,	 R_BOTH,	S_DECL(HdrFmt) },
  { "hdrs",		DT_BOOL, R_NONE,	I_DECL(OPTHDRS) },
  { "header",		DT_BOOL, R_NONE,	I_DECL(OPTHEADER) },
  { "help",		DT_BOOL, R_BOTH,	I_DECL(OPTHELP) },
  { "history",		DT_NUM,	 R_NONE,	P_DECL(&HistSize) },
  { "hostname",		DT_STR,	 R_NONE,	S_DECL(Fqdn) },
  { "in_reply_to",	DT_STR,	 R_NONE,	S_DECL(InReplyTo) },
  { "include",		DT_QUAD, R_NONE,	I_DECL(OPT_INCLUDE) },
  { "indent_str",	DT_STR,	 R_NONE,	S_DECL(Prefix) },
  { "ignore_list_reply_to", DT_BOOL, R_NONE,	I_DECL(OPTIGNORELISTREPLYTO) },
  { "ispell",		DT_PATH, R_NONE,	S_DECL(Ispell) },
  { "locale",		DT_STR,  R_BOTH,	S_DECL(Locale) },
  { "mail_check",	DT_NUM,  R_NONE,	P_DECL(&BuffyTimeout) },
  { "mailcap_path",	DT_STR,	 R_NONE,	S_DECL(MailcapPath) },
  { "mark_old",		DT_BOOL, R_BOTH,	I_DECL(OPTMARKOLD) },
  { "markers",		DT_BOOL, R_PAGER,	I_DECL(OPTMARKERS) },
  { "mask",		DT_RX,	 R_NONE,	P_DECL(&Mask) },
  { "mbox",		DT_PATH, R_BOTH,	S_DECL(Inbox) },
  { "mbox_type",	DT_MAGIC,R_NONE,	P_DECL(&DefaultMagic) },
  { "metoo",		DT_BOOL, R_NONE,	I_DECL(OPTMETOO) },
  { "menu_scroll",	DT_BOOL, R_NONE,	I_DECL(OPTMENUSCROLL) },
  { "meta_key",		DT_BOOL, R_NONE,	I_DECL(OPTMETAKEY) },
  { "mime_fwd",		DT_BOOL, R_NONE,	I_DECL(OPTMIMEFWD) },
  { "move",		DT_QUAD, R_NONE,	I_DECL(OPT_MOVE) },
  { "msg_format",	DT_STR,	 R_NONE,	S_DECL(MsgFmt) },
  { "pager",		DT_PATH, R_NONE,	S_DECL(Pager) },
  { "pager_context",	DT_NUM,	 R_NONE,	P_DECL(&PagerContext) },
  { "pager_format",	DT_STR,	 R_PAGER,	S_DECL(PagerFmt) },
  { "pager_index_lines",DT_NUM,	 R_PAGER,	P_DECL(&PagerIndexLines) },
  { "pager_stop",	DT_BOOL, R_NONE,	I_DECL(OPTPAGERSTOP) },


  { "pipe_split",	DT_BOOL, R_NONE,	I_DECL(OPTPIPESPLIT) },
  { "pipe_decode",	DT_BOOL, R_NONE,	I_DECL(OPTPIPEDECODE) },
  { "pipe_sep",		DT_STR,	 R_NONE,	S_DECL(PipeSep) },
#ifdef USE_POP
  { "pop_delete",	DT_BOOL, R_NONE,	I_DECL(OPTPOPDELETE) },
  { "pop_host",		DT_STR,	 R_NONE,	S_DECL(PopHost) },
  { "pop_port",		DT_NUM,	 R_NONE,	P_DECL(&PopPort) },
  { "pop_pass",		DT_STR,	 R_NONE,	S_DECL(PopPass) },
  { "pop_user",		DT_STR,	 R_NONE,	S_DECL(PopUser) },
#endif /* USE_POP */
  { "post_indent_str",	DT_STR,	 R_NONE,	S_DECL(PostIndentString) },
  { "postpone",		DT_QUAD, R_NONE,	I_DECL(OPT_POSTPONE) },
  { "postponed",	DT_PATH, R_NONE,	S_DECL(Postponed) },
  { "print",		DT_QUAD, R_NONE,	I_DECL(OPT_PRINT) },
  { "print_cmd",	DT_PATH, R_NONE,	S_DECL(PrintCmd) },
  { "prompt_after",	DT_BOOL, R_NONE,	I_DECL(OPTPROMPTAFTER) },
  { "quote_regexp",	DT_RX,	 R_PAGER,	P_DECL(&QuoteRegexp) },
  { "reply_regexp",	DT_RX,	 R_INDEX,	P_DECL(&ReplyRegexp) },
  { "read_inc",		DT_NUM,	 R_NONE,	P_DECL(&ReadInc) },
  { "read_only",	DT_BOOL, R_NONE,	I_DECL(OPTREADONLY) },
  { "realname",		DT_STR,	 R_BOTH,	S_DECL(Realname) },
  { "recall",		DT_QUAD, R_NONE,	I_DECL(OPT_RECALL) },
  { "record",		DT_PATH, R_NONE,	S_DECL(Outbox) },
  { "references",	DT_NUM,	 R_NONE,	P_DECL(&TrimRef) },
  { "reply_to",		DT_QUAD, R_NONE,	I_DECL(OPT_REPLYTO) },
  { "resolve",		DT_BOOL, R_NONE,	I_DECL(OPTRESOLVE) },
  { "reverse_alias",	DT_BOOL, R_BOTH,	I_DECL(OPTREVALIAS) },
  { "reverse_name",	DT_BOOL, R_BOTH,	I_DECL(OPTREVNAME) },
  { "save_address",	DT_BOOL, R_NONE,	I_DECL(OPTSAVEADDRESS) },
  { "save_empty",	DT_BOOL, R_NONE,	I_DECL(OPTSAVEEMPTY) },
  { "save_name",	DT_BOOL, R_NONE,	I_DECL(OPTSAVENAME) },
  { "sendmail",		DT_PATH, R_NONE,	S_DECL(Sendmail) },
  { "sendmail_bounce",	DT_PATH, R_NONE,	S_DECL(SendmailBounce) },
  { "shell",		DT_PATH, R_NONE,	S_DECL(Shell) },
  { "sig_dashes",	DT_BOOL, R_NONE,	I_DECL(OPTSIGDASHES) },
  { "signature",	DT_PATH, R_NONE,	S_DECL(Signature) },
  { "simple_search",	DT_STR,	 R_NONE,	S_DECL(SimpleSearch) },
  { "smart_wrap",	DT_BOOL, R_PAGER,	I_DECL(OPTWRAP) },
  { "sort",		DT_SORT, R_INDEX,	P_DECL(&Sort) },
  { "sort_alias",	DT_SORT|DT_SORT_ALIAS,	R_NONE,	P_DECL(&SortAlias) },
  { "sort_aux",		DT_SORT, R_INDEX,	P_DECL(&SortAux) },
  { "sort_browser",	DT_SORT, R_NONE,	P_DECL(&BrowserSort) },
  { "sort_re",		DT_BOOL, R_INDEX,	I_DECL(OPTSORTRE) },
  { "spoolfile",	DT_PATH, R_NONE,	S_DECL(Spoolfile) },
  { "status_chars",	DT_STR,	 R_BOTH,	S_DECL(StChars) },
  { "status_format",	DT_STR,	 R_BOTH,	S_DECL(Status) },
  { "status_on_top",	DT_BOOL, R_BOTH,	I_DECL(OPTSTATUSONTOP) },
  { "strict_threads",	DT_BOOL, R_INDEX,	I_DECL(OPTSTRICTTHREADS) },
  { "suspend",		DT_BOOL, R_NONE,	I_DECL(OPTSUSPEND) },
  { "thorough_search",	DT_BOOL, R_NONE,	I_DECL(OPTTHOROUGHSRC) },
  { "tilde",		DT_BOOL, R_PAGER,	I_DECL(OPTTILDE) },
  { "timeout",		DT_NUM,	 R_NONE,	P_DECL(&Timeout) },
  { "tmpdir",		DT_PATH, R_NONE,	S_DECL(Tempdir) },
  { "to_chars",		DT_STR,	 R_BOTH,	S_DECL(Tochars) },
  { "use_8bitmime",	DT_BOOL, R_NONE,	I_DECL(OPTUSE8BITMIME) },
  { "use_domain",	DT_BOOL, R_NONE,	I_DECL(OPTUSEDOMAIN) },
  { "use_from",		DT_BOOL, R_NONE,	I_DECL(OPTUSEFROM) },
  { "use_mailcap",	DT_QUAD, R_NONE,	I_DECL(OPT_USEMAILCAP) },
  { "visual",		DT_PATH, R_NONE,	S_DECL(Visual) },
  { "wait_key",		DT_BOOL, R_NONE,	I_DECL(OPTWAITKEY) },
  { "write_inc",	DT_NUM,	 R_NONE,	P_DECL(&WriteInc) },
  { NULL }
};

const struct mapping_t SortMethods[] = {
  { "date",		SORT_DATE },
  { "date-sent",	SORT_DATE },
  { "date-received",	SORT_RECEIVED },
  { "mailbox-order",	SORT_ORDER },
  { "subject",		SORT_SUBJECT },
  { "alpha",		SORT_SUBJECT }, /* alphabetic sort */
  { "from",		SORT_FROM },
  { "size",		SORT_SIZE },
  { "threads",		SORT_THREADS },
  { "to",		SORT_TO },
  { "score",		SORT_SCORE },
  { NULL,		0 }
};

const struct mapping_t SortAliasMethods[] = {
  { "alias",	SORT_ALIAS },
  { "address",	SORT_ADDRESS },
  { "unsorted", SORT_ORDER },
  { NULL }
};

/* functions used to parse commands in a rc file */

static int parse_list (const char *, unsigned long, char *, size_t);
static int parse_unlist (const char *, unsigned long, char *, size_t);
static int parse_alias (const char *, unsigned long, char *, size_t);
static int parse_unalias (const char *, unsigned long, char *, size_t);
static int parse_ignore (const char *, unsigned long, char *, size_t);
static int parse_unignore (const char *, unsigned long, char *, size_t);
static int parse_source (const char *, unsigned long, char *, size_t);
static int parse_set (const char *, unsigned long, char *, size_t);
static int parse_my_hdr (const char *, unsigned long, char *, size_t);
static int parse_unmy_hdr (const char *, unsigned long, char *, size_t);

struct command_t
{
  char *name;
  int (*func) (const char *, unsigned long, char *, size_t);
  unsigned long data;
};

struct command_t Commands[] = {
  { "alias",		parse_alias,		0 },
  { "auto_view",	parse_list,		(unsigned long) &AutoViewList },
  { "bind",		mutt_parse_bind,	0 },
#ifdef HAVE_COLOR
  { "color",		mutt_parse_color,	0 },
#endif
  { "fcc-hook",		mutt_parse_hook,	M_FCCHOOK },
  { "fcc-save-hook",	mutt_parse_hook,	M_FCCHOOK | M_SAVEHOOK },
  { "folder-hook",	mutt_parse_hook,	M_FOLDERHOOK },
  { "hdr_order",	parse_list,		(unsigned long) &HeaderOrderList },
  { "ignore",		parse_ignore,		0 },
  { "lists",		parse_list,		(unsigned long) &MailLists },
  { "macro",		mutt_parse_macro,	0 },
  { "mailboxes",	mutt_parse_mailboxes,	0 },
  { "mbox-hook",	mutt_parse_hook,	M_MBOXHOOK },
  { "mono",		mutt_parse_mono,	0 },
  { "my_hdr",		parse_my_hdr,		0 },
  { "push",		mutt_parse_push,	0 },
  { "save-hook",	mutt_parse_hook,	M_SAVEHOOK },
  { "score",		mutt_parse_score,	0 },
  { "send-hook",	mutt_parse_hook,	M_SENDHOOK },
  { "set",		parse_set,		0 },
  { "source",		parse_source,		0 },
  { "toggle",		parse_set,		M_SET_INV },
  { "unalias",		parse_unalias,		0 },
  { "unignore",		parse_unignore,		0 },
  { "unlists",		parse_unlist,		(unsigned long) &MailLists },
  { "unmy_hdr",		parse_unmy_hdr,		0 },
  { "unscore",		mutt_parse_unscore,	0 },
  { "unset",		parse_set,		M_SET_UNSET },
  { NULL }
};
