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

#ifndef LIBMUTT

#define DT_MASK		0x0f
#define DT_BOOL		1 /* boolean option */
#define DT_NUM		2 /* a number */
#define DT_STR		3 /* a string */
#define DT_PATH		4 /* a pathname */
#define DT_QUAD		5 /* quad-option (yes/no/ask-yes/ask-no) */
#define DT_SORT		6 /* sorting methods */
#define DT_RX		7 /* regular expressions */
#define DT_MAGIC	8 /* mailbox type */
#define DT_SYN		9 /* synonym for another variable */

#define DTYPE(x) ((x) & DT_MASK)

/* subtypes */
#define DT_SUBTYPE_MASK	0xf0
#define DT_SORT_ALIAS	0x10
#define DT_SORT_BROWSER 0x20

/* flags to parse_set() */
#define M_SET_INV	(1<<0)	/* default is to invert all vars */
#define M_SET_UNSET	(1<<1)	/* default is to unset all vars */
#define M_SET_RESET	(1<<2)	/* default is to reset all vars to default */

/* forced redraw/resort types */
#define R_NONE		0
#define R_INDEX		(1<<0)
#define R_PAGER		(1<<1)
#define R_RESORT	(1<<2)	/* resort the mailbox */
#define R_RESORT_SUB	(1<<3)	/* resort subthreads */
#define R_BOTH		(R_INDEX | R_PAGER)
#define R_RESORT_BOTH	(R_RESORT | R_RESORT_SUB)

struct option_t
{
  char *option;
  short type;
  short flags;
  unsigned long data;
  unsigned long init; /* initial value */
};

#define UL (unsigned long)

#ifndef ISPELL
#define ISPELL "ispell"
#endif

struct option_t MuttVars[] = {
  { "abort_nosubject",	DT_QUAD, R_NONE, OPT_SUBJECT, M_ASKYES },
  { "abort_unmodified",	DT_QUAD, R_NONE, OPT_ABORT, M_YES },
  { "alias_file",	DT_PATH, R_NONE, UL &AliasFile, UL "~/.muttrc" },
  { "alias_format",	DT_STR,  R_NONE, UL &AliasFmt, UL "%2n %t %-10a   %r" },
  { "allow_8bit",	DT_BOOL, R_NONE, OPTALLOW8BIT, 1 },
  { "alternates",	DT_RX,	 R_BOTH, UL &Alternates, 0 },
  { "arrow_cursor",	DT_BOOL, R_BOTH, OPTARROWCURSOR, 0 },
  { "ascii_chars",	DT_BOOL, R_BOTH, OPTASCIICHARS, 0 },
  { "askbcc",		DT_BOOL, R_NONE, OPTASKBCC, 0 },
  { "askcc",		DT_BOOL, R_NONE, OPTASKCC, 0 },
  { "attribution",	DT_STR,	 R_NONE, UL &Attribution, UL "On %d, %n wrote:" },
  { "autoedit",		DT_BOOL, R_NONE, OPTAUTOEDIT, 0 },
  { "auto_tag",		DT_BOOL, R_NONE, OPTAUTOTAG, 0 },
  { "beep",		DT_BOOL, R_NONE, OPTBEEP, 1 },
  { "beep_new",		DT_BOOL, R_NONE, OPTBEEPNEW, 0 },
  { "charset",		DT_STR,	 R_NONE, UL &Charset, UL "iso-8859-1" },
  { "check_new",	DT_BOOL, R_NONE, OPTCHECKNEW, 1 },
  { "confirmappend",	DT_BOOL, R_NONE, OPTCONFIRMAPPEND, 1 },
  { "confirmcreate",	DT_BOOL, R_NONE, OPTCONFIRMCREATE, 1 },
  { "copy",		DT_QUAD, R_NONE, OPT_COPY, M_YES },
  { "date_format",	DT_STR,	 R_BOTH, UL &DateFmt, UL "!%a, %b %d, %Y at %I:%M:%S%p %Z" },
  { "delete_format",	DT_STR,  R_NONE, UL &DeleteFmt, UL "[-- Attachment from %u deleted %<%b %d %T %Y> --]" },
  { "default_hook",	DT_STR,	 R_NONE, UL &DefaultHook, UL "~f %s !~P | (~P ~C %s)" },
  { "delete",		DT_QUAD, R_NONE, OPT_DELETE, M_ASKYES },
  { "dsn_notify",	DT_STR,	 R_NONE, UL &DsnNotify, UL "" },
  { "dsn_return",	DT_STR,	 R_NONE, UL &DsnReturn, UL "" },
  { "edit_headers",	DT_BOOL, R_NONE, OPTEDITHDRS, 0 },
  { "edit_hdrs",	DT_SYN,  R_NONE, UL "edit_headers", 0 },
  { "editor",		DT_PATH, R_NONE, UL &Editor, 0 },
  { "escape",		DT_STR,	 R_NONE, UL &EscChar, UL "~" },
  { "fast_reply",	DT_BOOL, R_NONE, OPTFASTREPLY, 0 },
  { "fcc_attach",	DT_BOOL, R_NONE, OPTFCCATTACH, 1 },
  { "folder",		DT_PATH, R_NONE, UL &Maildir, UL "~/Mail" },
  { "folder_format",	DT_STR,	 R_NONE, UL &FolderFormat, UL "%N %F %2l %-8.8u %-8.8g %8s %d %f" },
  { "followup_to",	DT_BOOL, R_NONE, OPTFOLLOWUPTO, 1 },
  { "force_name",	DT_BOOL, R_NONE, OPTFORCENAME, 0 },
  { "forward_decode",	DT_BOOL, R_NONE, OPTFORWDECODE, 1 },
  { "forw_decode",	DT_SYN,  R_NONE, UL "forward_decode", 0 },
  { "forward_format",	DT_STR,	 R_NONE, UL &ForwFmt, UL "[%a: %s]" },
  { "forw_format",	DT_SYN,  R_NONE, UL "forward_format", 0 },
  { "forward_quote",	DT_BOOL, R_NONE, OPTFORWQUOTE, 0 },
  { "forw_quote",	DT_SYN,  R_NONE, UL "forward_quote", 0 },
  { "hdr_format",	DT_SYN,  R_NONE, UL "index_format", 0 },
  { "hdrs",		DT_BOOL, R_NONE, OPTHDRS, 1 },
  { "header",		DT_BOOL, R_NONE, OPTHEADER, 0 },
  { "help",		DT_BOOL, R_BOTH, OPTHELP, 1 },
  { "history",		DT_NUM,	 R_NONE, UL &HistSize, 10 },
  { "hostname",		DT_STR,	 R_NONE, UL &Fqdn, 0 },
#ifdef USE_IMAP
  { "imap_user",	DT_STR,  R_NONE, UL &ImapUser, UL 0 },
  { "imap_pass", 	DT_STR,  R_NONE, UL &ImapPass, UL 0 },
  { "imap_checkinterval", 	DT_NUM,	 R_NONE, UL &ImapCheckTime, 0 },
#endif
  { "in_reply_to",	DT_STR,	 R_NONE, UL &InReplyTo, UL "%i; from %n on %{!%a, %b %d, %Y at %I:%M:%S%p %Z}" },
  { "include",		DT_QUAD, R_NONE, OPT_INCLUDE, M_ASKYES },
  { "indent_string",	DT_STR,	 R_NONE, UL &Prefix, UL "> " },
  { "indent_str",	DT_SYN,  R_NONE, UL "indent_string", 0 },
  { "index_format",	DT_STR,	 R_BOTH, UL &HdrFmt, UL "%4C %Z %{%b %d} %-15.15L (%4l) %s" },
  { "ignore_list_reply_to", DT_BOOL, R_NONE, OPTIGNORELISTREPLYTO, 0 },
  { "ispell",		DT_PATH, R_NONE, UL &Ispell, UL ISPELL },
  { "locale",		DT_STR,  R_BOTH, UL &Locale, UL "C" },
  { "mail_check",	DT_NUM,  R_NONE, UL &BuffyTimeout, 5 },
  { "mailcap_path",	DT_STR,	 R_NONE, UL &MailcapPath, 0 },
  { "mark_old",		DT_BOOL, R_BOTH, OPTMARKOLD, 1 },
  { "markers",		DT_BOOL, R_PAGER, OPTMARKERS, 1 },
  { "mask",		DT_RX,	 R_NONE, UL &Mask, UL "^(\\.\\.$|[^.])" },
  { "mbox",		DT_PATH, R_BOTH, UL &Inbox, UL "~/mbox" },
  { "mbox_type",	DT_MAGIC,R_NONE, UL &DefaultMagic, M_MBOX },
  { "metoo",		DT_BOOL, R_NONE, OPTMETOO, 0 },
  { "menu_scroll",	DT_BOOL, R_NONE, OPTMENUSCROLL, 0 },
  { "meta_key",		DT_BOOL, R_NONE, OPTMETAKEY, 0 },
  { "mime_forward",	DT_QUAD, R_NONE, OPT_MIMEFWD, 0 },
  { "mime_forward_decode", DT_BOOL, R_NONE, OPTMIMEFORWDECODE, 0 },
  { "mime_fwd",		DT_SYN,  R_NONE, UL "mime_forward", 0 },
  { "move",		DT_QUAD, R_NONE, OPT_MOVE, M_ASKNO },
  { "message_format",	DT_STR,	 R_NONE, UL &MsgFmt, UL "%s" },
  { "msg_format",	DT_SYN,  R_NONE, UL "message_format", 0 },
  { "pager",		DT_PATH, R_NONE, UL &Pager, UL "builtin" },
  { "pager_context",	DT_NUM,	 R_NONE, UL &PagerContext, 0 },
  { "pager_format",	DT_STR,	 R_PAGER, UL &PagerFmt, UL "-%S- %C/%m: %-20.20n   %s" },
  { "pager_index_lines",DT_NUM,	 R_PAGER, UL &PagerIndexLines, 0 },
  { "pager_stop",	DT_BOOL, R_NONE, OPTPAGERSTOP, 0 },

  

  
  
  
  { "pipe_split",	DT_BOOL, R_NONE, OPTPIPESPLIT, 0 },
  { "pipe_decode",	DT_BOOL, R_NONE, OPTPIPEDECODE, 0 },
  { "pipe_sep",		DT_STR,	 R_NONE, UL &PipeSep, UL "\n" },
#ifdef USE_POP
  { "pop_delete",	DT_BOOL, R_NONE, OPTPOPDELETE, 0 },
  { "pop_host",		DT_STR,	 R_NONE, UL &PopHost, UL "" },
  { "pop_port",		DT_NUM,	 R_NONE, UL &PopPort, 110 },
  { "pop_pass",		DT_STR,	 R_NONE, UL &PopPass, UL "" },
  { "pop_user",		DT_STR,	 R_NONE, UL &PopUser, UL "" },
#endif /* USE_POP */
  { "post_indent_string",DT_STR, R_NONE, UL &PostIndentString, UL "" },
  { "post_indent_str",  DT_SYN,  R_NONE, UL "post_indent_string", 0 },
  { "postpone",		DT_QUAD, R_NONE, OPT_POSTPONE, M_ASKYES },
  { "postponed",	DT_PATH, R_NONE, UL &Postponed, UL "~/postponed" },
  { "print",		DT_QUAD, R_NONE, OPT_PRINT, M_ASKNO },
  { "print_command",	DT_PATH, R_NONE, UL &PrintCmd, UL "lpr" },
  { "print_cmd",	DT_SYN,  R_NONE, UL "print_command", 0 },
  { "prompt_after",	DT_BOOL, R_NONE, OPTPROMPTAFTER, 1 },
  { "query_command",	DT_PATH, R_NONE, UL &QueryCmd, UL "" },
  { "quit",		DT_QUAD, R_NONE, OPT_QUIT, M_YES },
  { "quote_regexp",	DT_RX,	 R_PAGER, UL &QuoteRegexp, UL "^([ \t]*[|>:}#])+" },
  { "reply_regexp",	DT_RX,	 R_INDEX|R_RESORT, UL &ReplyRegexp, UL "^(re([\\[0-9\\]+])*|aw):[ \t]*" },
  { "read_inc",		DT_NUM,	 R_NONE, UL &ReadInc, 10 },
  { "read_only",	DT_BOOL, R_NONE, OPTREADONLY, 0 },
  { "realname",		DT_STR,	 R_BOTH, UL &Realname, 0 },
  { "recall",		DT_QUAD, R_NONE, OPT_RECALL, M_ASKYES },
  { "record",		DT_PATH, R_NONE, UL &Outbox, UL "" },
  { "reply_to",		DT_QUAD, R_NONE, OPT_REPLYTO, M_ASKYES },
  { "resolve",		DT_BOOL, R_NONE, OPTRESOLVE, 1 },
  { "reverse_alias",	DT_BOOL, R_BOTH, OPTREVALIAS, 0 },
  { "reverse_name",	DT_BOOL, R_BOTH, OPTREVNAME, 0 },
  { "save_address",	DT_BOOL, R_NONE, OPTSAVEADDRESS, 0 },
  { "save_empty",	DT_BOOL, R_NONE, OPTSAVEEMPTY, 1 },
  { "save_name",	DT_BOOL, R_NONE, OPTSAVENAME, 0 },
  { "sendmail",		DT_PATH, R_NONE, UL &Sendmail, UL SENDMAIL " -oem -oi" },
  { "sendmail_wait",	DT_NUM,  R_NONE, UL &SendmailWait, 0 },
  { "shell",		DT_PATH, R_NONE, UL &Shell, 0 },
  { "sig_dashes",	DT_BOOL, R_NONE, OPTSIGDASHES, 1 },
  { "signature",	DT_PATH, R_NONE, UL &Signature, UL "~/.signature" },
  { "simple_search",	DT_STR,	 R_NONE, UL &SimpleSearch, UL "~f %s | ~s %s" },
  { "smart_wrap",	DT_BOOL, R_PAGER, OPTWRAP, 1 },
  { "sort",		DT_SORT, R_INDEX|R_RESORT, UL &Sort, SORT_DATE },
  { "sort_alias",	DT_SORT|DT_SORT_ALIAS,	R_NONE,	UL &SortAlias, SORT_ALIAS },
  { "sort_aux",		DT_SORT, R_INDEX|R_RESORT_BOTH, UL &SortAux, SORT_DATE },
  { "sort_browser",	DT_SORT|DT_SORT_BROWSER, R_NONE, UL &BrowserSort, SORT_SUBJECT },
  { "sort_re",		DT_BOOL, R_INDEX|R_RESORT_BOTH, OPTSORTRE, 1 },
  { "spoolfile",	DT_PATH, R_NONE, UL &Spoolfile, 0 },
  { "status_chars",	DT_STR,	 R_BOTH, UL &StChars, UL "-*%" },
  { "status_format",	DT_STR,	 R_BOTH, UL &StatusString, UL "-%r-Mutt: %f [Msgs:%?M?%M/?%m%?n? New:%n?%?o? Old:%o?%?d? Del:%d?%?F? Flag:%F?%?t? Tag:%t?%?p? Post:%p?%?b? Inc:%b?%?l? %l?]---(%s/%S)-%>-(%P)---" },
  { "status_on_top",	DT_BOOL, R_BOTH, OPTSTATUSONTOP, 0 },
  { "strict_threads",	DT_BOOL, R_RESORT|R_INDEX, OPTSTRICTTHREADS, 0 },
  { "suspend",		DT_BOOL, R_NONE, OPTSUSPEND, 1 },
  { "thorough_search",	DT_BOOL, R_NONE, OPTTHOROUGHSRC, 0 },
  { "tilde",		DT_BOOL, R_PAGER, OPTTILDE, 0 },
  { "timeout",		DT_NUM,	 R_NONE, UL &Timeout, 600 },
  { "tmpdir",		DT_PATH, R_NONE, UL &Tempdir, 0 },
  { "to_chars",		DT_STR,	 R_BOTH, UL &Tochars, UL " +TCF" },
  { "use_8bitmime",	DT_BOOL, R_NONE, OPTUSE8BITMIME, 0 },
  { "use_domain",	DT_BOOL, R_NONE, OPTUSEDOMAIN, 1 },
  { "use_from",		DT_BOOL, R_NONE, OPTUSEFROM, 1 },
  { "use_mailcap",	DT_QUAD, R_NONE, OPT_USEMAILCAP, 1 },
  { "visual",		DT_PATH, R_NONE, UL &Visual, 0 },
  { "wait_key",		DT_BOOL, R_NONE, OPTWAITKEY, 1 },
  { "wrap_search",	DT_BOOL, R_NONE, OPTWRAPSEARCH, 1 },
  { "write_inc",	DT_NUM,	 R_NONE, UL &WriteInc, 10 },
  { NULL }
};

const struct mapping_t SortMethods[] = {
  { "date",		SORT_DATE },
  { "date-sent",	SORT_DATE },
  { "date-received",	SORT_RECEIVED },
  { "mailbox-order",	SORT_ORDER },
  { "subject",		SORT_SUBJECT },
  { "from",		SORT_FROM },
  { "size",		SORT_SIZE },
  { "threads",		SORT_THREADS },
  { "to",		SORT_TO },
  { "score",		SORT_SCORE },
  { NULL,		0 }
};

const struct mapping_t SortBrowserMethods[] = {
  { "alpha",	SORT_SUBJECT },
  { "date",	SORT_DATE },
  { "size",	SORT_SIZE },
  { "unsorted",	SORT_ORDER },
  { NULL }
};

const struct mapping_t SortAliasMethods[] = {
  { "alias",	SORT_ALIAS },
  { "address",	SORT_ADDRESS },
  { "unsorted", SORT_ORDER },
  { NULL }
};

/* functions used to parse commands in a rc file */

static int parse_list (BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int parse_unlist (BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int parse_alias (BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int parse_unalias (BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int parse_ignore (BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int parse_unignore (BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int parse_source (BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int parse_set (BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int parse_my_hdr (BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int parse_unmy_hdr (BUFFER *, BUFFER *, unsigned long, BUFFER *);

struct command_t
{
  char *name;
  int (*func) (BUFFER *, BUFFER *, unsigned long, BUFFER *);
  unsigned long data;
};

struct command_t Commands[] = {
  { "alias",		parse_alias,		0 },
  { "auto_view",	parse_list,		UL &AutoViewList },
  { "alternative_order",	parse_list,	UL &AlternativeOrderList},
  { "bind",		mutt_parse_bind,	0 },
#ifdef HAVE_COLOR
  { "color",		mutt_parse_color,	0 },
  { "uncolor",		mutt_parse_uncolor,	0 },
#endif
  { "fcc-hook",		mutt_parse_hook,	M_FCCHOOK },
  { "fcc-save-hook",	mutt_parse_hook,	M_FCCHOOK | M_SAVEHOOK },
  { "folder-hook",	mutt_parse_hook,	M_FOLDERHOOK },
  { "hdr_order",	parse_list,		UL &HeaderOrderList },
  { "ignore",		parse_ignore,		0 },
  { "lists",		parse_list,		UL &MailLists },
  { "macro",		mutt_parse_macro,	0 },
  { "mailboxes",	mutt_parse_mailboxes,	0 },
  { "mbox-hook",	mutt_parse_hook,	M_MBOXHOOK },
  { "mono",		mutt_parse_mono,	0 },
  { "my_hdr",		parse_my_hdr,		0 },
  { "push",		mutt_parse_push,	0 },
  { "reset",		parse_set,		M_SET_RESET },
  { "save-hook",	mutt_parse_hook,	M_SAVEHOOK },
  { "score",		mutt_parse_score,	0 },
  { "send-hook",	mutt_parse_hook,	M_SENDHOOK },
  { "set",		parse_set,		0 },
  { "source",		parse_source,		0 },
  { "toggle",		parse_set,		M_SET_INV },
  { "unalias",		parse_unalias,		0 },
  { "unignore",		parse_unignore,		0 },
  { "unlists",		parse_unlist,		UL &MailLists },
  { "unmy_hdr",		parse_unmy_hdr,		0 },
  { "unscore",		mutt_parse_unscore,	0 },
  { "unset",		parse_set,		M_SET_UNSET },
  { NULL }
};
#endif
