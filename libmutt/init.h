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
  { "attach_format",	DT_STR,  R_NONE, UL &AttachFormat, UL "%u%D%t%2n %T%.40d%> [%.7m/%.10M, %.6e, %s] " },
  { "attach_split",	DT_BOOL, R_NONE, OPTATTACHSPLIT, 1 },
  { "attach_sep",	DT_STR,	 R_NONE, UL &AttachSep, UL "\n" },
  { "attribution",	DT_STR,	 R_NONE, UL &Attribution, UL "On %d, %n wrote:" },
  { "autoedit",		DT_BOOL, R_NONE, OPTAUTOEDIT, 0 },
  { "auto_tag",		DT_BOOL, R_NONE, OPTAUTOTAG, 0 },
  { "beep",		DT_BOOL, R_NONE, OPTBEEP, 1 },
  { "beep_new",		DT_BOOL, R_NONE, OPTBEEPNEW, 0 },
  { "bounce_delivered", DT_BOOL, R_NONE, OPTBOUNCEDELIVERED, 1 },
  { "charset",		DT_STR,	 R_NONE, UL &Charset, UL "iso-8859-1" },
  { "check_new",	DT_BOOL, R_NONE, OPTCHECKNEW, 1 },
  { "collapse_unread",	DT_BOOL, R_NONE, OPTCOLLAPSEUNREAD, 1 },
  { "uncollapse_jump", 	DT_BOOL, R_NONE, OPTUNCOLLAPSEJUMP, 0 },
  { "confirmappend",	DT_BOOL, R_NONE, OPTCONFIRMAPPEND, 1 },
  { "confirmcreate",	DT_BOOL, R_NONE, OPTCONFIRMCREATE, 1 },
  { "copy",		DT_QUAD, R_NONE, OPT_COPY, M_YES },
  { "date_format",	DT_STR,	 R_BOTH, UL &DateFmt, UL "!%a, %b %d, %Y at %I:%M:%S%p %Z" },
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
  { "forward_weed",	DT_BOOL, R_NONE, OPTFORWWEEDHEADER, 1 },
  { "forw_weed",	DT_SYN,  R_NONE, UL "forward_weed", 0 }, 
  { "forward_format",	DT_STR,	 R_NONE, UL &ForwFmt, UL "[%a: %s]" },
  { "forw_format",	DT_SYN,  R_NONE, UL "forward_format", 0 },
  { "forward_quote",	DT_BOOL, R_NONE, OPTFORWQUOTE, 0 },
  { "forw_quote",	DT_SYN,  R_NONE, UL "forward_quote", 0 },
  { "hdr_format",	DT_SYN,  R_NONE, UL "index_format", 0 },
  { "hdrs",		DT_BOOL, R_NONE, OPTHDRS, 1 },
  { "header",		DT_BOOL, R_NONE, OPTHEADER, 0 },
  { "help",		DT_BOOL, R_BOTH, OPTHELP, 1 },
  { "hidden_host",	DT_BOOL, R_NONE, OPTHIDDENHOST, 0 },
  { "history",		DT_NUM,	 R_NONE, UL &HistSize, 10 },
  { "hostname",		DT_STR,	 R_NONE, UL &Fqdn, 0 },
#ifdef USE_IMAP
  { "imap_authenticators", DT_STR, R_NONE, UL &ImapAuthenticators, UL 0 },
  /*
  ** .pp
  ** This is a colon-delimited list of authentication methods mutt may
  ** attempt to use to log in to an IMAP server, in the order mutt should
  ** try them.  Authentication methods are either 'login' or the right
  ** side of an IMAP 'AUTH=xxx' capability string, eg 'digest-md5',
  ** 'gssapi' or 'cram-md5'. This parameter is case-insensitive. If this
  ** parameter is unset (the default) mutt will try all available methods,
  ** in order from most-secure to least-secure.
  ** .pp
  ** Example: set imap_authenticators="gssapi:cram-md5:login"
  ** .pp
  ** \fBNote:\fP Mutt will only fall back to other authentication methods if
  ** the previous methods are unavailable. If a method is available but
  ** authentication fails, mutt will not connect to the IMAP server.
  */
  { "imap_delim_chars",         DT_STR, R_NONE, UL &ImapDelimChars, UL "/." },
  /*
  ** .pp
  ** This contains the list of characters which you would like to treat
  ** as folder separators for displaying IMAP paths. In particular it
  ** helps in using the '=' shortcut for your \fIfolder\fP variable.
  */
# ifdef USE_SSL
  { "imap_force_ssl",           DT_BOOL, R_NONE, OPTIMAPFORCESSL, 0 },
  /*
  ** .pp
  ** If this variable is set, Mutt will always use SSL when
  ** connecting to IMAP servers.
  */
# endif
{ "imap_list_subscribed",     DT_BOOL, R_NONE, OPTIMAPLSUB, 0 },
  /*
  ** .pp
  ** This variable configures whether IMAP folder browsing will look for
  ** only subscribed folders or all folders.  This can be toggled in the
  ** IMAP browser with the \fItoggle-subscribed\fP command.
  */
  { "imap_pass",        DT_STR,  R_NONE, UL &ImapPass, UL 0 },
  /*
  ** .pp
  ** Specifies the password for your IMAP account.  If unset, Mutt will
  ** prompt you for your password when you invoke the fetch-mail function.
  ** \fBWarning\fP: you should only use this option when you are on a
  ** fairly secure machine, because the superuser can read your muttrc even
  ** if you are the only one who can read the file.
  */
  { "imap_passive",             DT_BOOL, R_NONE, OPTIMAPPASSIVE, 1 },
  /*
  ** .pp
  ** When set, mutt will not open new IMAP connections to check for new
  ** mail.  Mutt will only check for new mail over existing IMAP
  ** connections.  This is useful if you don't want to be prompted to
  ** user/password pairs on mutt invocation, or if opening the connection
  ** is slow.
  */
 { "imap_peek", DT_BOOL, R_NONE, OPTIMAPPEEK, 1 },
  /*
  ** .pp
  ** If set, mutt will avoid implicitly marking your mail as read whenever
  ** you fetch a message from the server. This is generally a good thing,
  ** but can make closing an IMAP folder somewhat slower. This option
  ** exists to appease spead freaks.
  */
  { "imap_servernoise",         DT_BOOL, R_NONE, OPTIMAPSERVERNOISE, 1 },
  /*
  ** .pp
  ** When set, mutt will display warning messages from the IMAP
  ** server as error messages. Since these messages are often
  ** harmless, or generated due to configuration problems on the
  ** server which are out of the users' hands, you may wish to suppress
  ** them at some point.
  */
  { "imap_user",        DT_STR,  R_NONE, UL &ImapUser, UL 0 },
  /*
  ** .pp
  ** Your login name on the IMAP server.
  ** .pp
  ** This variable defaults to your user name on the local machine.
  */
#endif
  { "implicit_autoview", DT_BOOL,R_NONE, OPTIMPLICITAUTOVIEW, 0},
  { "in_reply_to",	DT_STR,	 R_NONE, UL &InReplyTo, UL "%i; from %a on %{!%a, %b %d, %Y at %I:%M:%S%p %Z}" },
  { "include",		DT_QUAD, R_NONE, OPT_INCLUDE, M_ASKYES },
  { "indent_string",	DT_STR,	 R_NONE, UL &Prefix, UL "> " },
  { "indent_str",	DT_SYN,  R_NONE, UL "indent_string", 0 },
  { "index_format",	DT_STR,	 R_BOTH, UL &HdrFmt, UL "%4C %Z %{%b %d} %-15.15L (%4l) %s" },
  { "ignore_list_reply_to", DT_BOOL, R_NONE, OPTIGNORELISTREPLYTO, 0 },
  { "ispell",		DT_PATH, R_NONE, UL &Ispell, UL ISPELL },
  { "locale",		DT_STR,  R_BOTH, UL &Locale, UL "C" },
  { "mail_check",	DT_NUM,  R_NONE, UL &BuffyTimeout, 5 },
  { "mailcap_path",	DT_STR,	 R_NONE, UL &MailcapPath, 0 },
  { "mailcap_sanitize", DT_BOOL, R_NONE, OPTMAILCAPSANITIZE, 1 },
  { "mark_old",		DT_BOOL, R_BOTH, OPTMARKOLD, 1 },
  { "markers",		DT_BOOL, R_PAGER, OPTMARKERS, 1 },
  { "mask",		DT_RX,	 R_NONE, UL &Mask, UL "!^\\.[^.]" },
  { "mbox",		DT_PATH, R_BOTH, UL &Inbox, UL "~/mbox" },
  { "mbox_type",	DT_MAGIC,R_NONE, UL &DefaultMagic, M_MBOX },
  { "metoo",		DT_BOOL, R_NONE, OPTMETOO, 0 },
  { "menu_scroll",	DT_BOOL, R_NONE, OPTMENUSCROLL, 0 },
  { "meta_key",		DT_BOOL, R_NONE, OPTMETAKEY, 0 },
  { "mh_purge",		DT_BOOL, R_NONE, OPTMHPURGE, 0 },
  { "mime_forward",	DT_QUAD, R_NONE, OPT_MIMEFWD, M_NO },
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

  

#ifdef _PGPPATH

  { "pgp_autosign",	DT_BOOL, R_NONE, OPTPGPAUTOSIGN, 0 },
  { "pgp_autoencrypt",	DT_BOOL, R_NONE, OPTPGPAUTOENCRYPT, 0 },
  { "pgp_encryptself",	DT_BOOL, R_NONE, OPTPGPENCRYPTSELF, 1 },
  { "pgp_long_ids",	DT_BOOL, R_NONE, OPTPGPLONGIDS, 0 },
  { "pgp_replyencrypt",	DT_BOOL, R_NONE, OPTPGPREPLYENCRYPT, 0 },
  { "pgp_replysign",	DT_BOOL, R_NONE, OPTPGPREPLYSIGN, 0 },
  { "pgp_sign_as",	DT_STR,	 R_NONE, UL &PgpSignAs, 0 },
  { "pgp_sign_micalg",	DT_STR,	 R_NONE, UL &PgpSignMicalg, UL "pgp-md5" },
  { "pgp_strict_enc",	DT_BOOL, R_NONE, OPTPGPSTRICTENC, 1 },
  { "pgp_timeout",	DT_NUM,	 R_NONE, UL &PgpTimeout, 300 },
  { "pgp_verify_sig",	DT_QUAD, R_NONE, OPT_VERIFYSIG, M_YES },

  { "pgp_v2",		DT_PATH, R_NONE, UL &PgpV2, 0 },
  { "pgp_v2_language",	DT_STR,	 R_NONE, UL &PgpV2Language, UL "en" },
  { "pgp_v2_pubring",	DT_PATH, R_NONE, UL &PgpV2Pubring, 0 },
  { "pgp_v2_secring",	DT_PATH, R_NONE, UL &PgpV2Secring, 0 },  

  { "pgp_v5",		DT_PATH, R_NONE, UL &PgpV3, 0 },
  { "pgp_v5_language",	DT_STR,	 R_NONE, UL &PgpV3Language, 0 },
  { "pgp_v5_pubring",	DT_PATH, R_NONE, UL &PgpV3Pubring, 0 },
  { "pgp_v5_secring",	DT_PATH, R_NONE, UL &PgpV3Secring, 0 },

  { "pgp_v6",		DT_PATH, R_NONE, UL &PgpV6, 0 },
  { "pgp_v6_language",	DT_STR,	 R_NONE, UL &PgpV6Language, 0 },
  { "pgp_v6_pubring",	DT_PATH, R_NONE, UL &PgpV6Pubring, 0 },
  { "pgp_v6_secring",	DT_PATH, R_NONE, UL &PgpV6Secring, 0 },

  { "pgp_gpg",		DT_PATH, R_NONE, UL &PgpGpg, 0 },
  
# ifdef HAVE_PGP2
  { "pgp_default_version",	DT_STR, R_NONE, UL &PgpDefaultVersion, UL "pgp2" },
# else
#  ifdef HAVE_PGP5
  { "pgp_default_version",	DT_STR, R_NONE, UL &PgpDefaultVersion, UL "pgp5" },
# else
#   ifdef HAVE_PGP6
  { "pgp_default_version",      DT_STR, R_NONE, UL &PgpDefaultVersion, UL "pgp6" },
# else
#   ifdef HAVE_GPG
  { "pgp_default_version",	DT_STR,	R_NONE, UL &PgpDefaultVersion, UL "gpg" },
#     endif
#   endif
#  endif
# endif
  { "pgp_receive_version", 	DT_STR,	R_NONE, UL &PgpReceiveVersion, UL "default" },
  { "pgp_send_version",		DT_STR,	R_NONE, UL &PgpSendVersion, UL "default" },
  { "pgp_key_version",		DT_STR, R_NONE, UL &PgpKeyVersion, UL "default" },

  { "forward_decrypt",	DT_BOOL, R_NONE, OPTFORWDECRYPT, 1 },
  { "forw_decrypt",	DT_SYN,  R_NONE, UL "forward_decrypt", 0 },
#endif /* _PGPPATH */
  
  { "pipe_split",	DT_BOOL, R_NONE, OPTPIPESPLIT, 0 },
  { "pipe_decode",	DT_BOOL, R_NONE, OPTPIPEDECODE, 0 },
  { "pipe_sep",		DT_STR,	 R_NONE, UL &PipeSep, UL "\n" },
#ifdef USE_POP
  { "pop_delete",	DT_BOOL, R_NONE, OPTPOPDELETE, 0 },
  { "pop_host",		DT_STR,	 R_NONE, UL &PopHost, UL "" },
  { "pop_last",		DT_BOOL, R_NONE, OPTPOPLAST, 0 },
  { "pop_port",		DT_NUM,	 R_NONE, UL &PopPort, 110 },
  { "pop_pass",		DT_STR,	 R_NONE, UL &PopPass, UL "" },
  { "pop_user",		DT_STR,	 R_NONE, UL &PopUser, UL "" },
  { "pop_use_apop",     DT_BOOL, R_NONE, OPTPOPAPOP, 0 },
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
  { "reply_self",	DT_BOOL, R_NONE, OPTREPLYSELF, 0 },
  { "reply_to",		DT_QUAD, R_NONE, OPT_REPLYTO, M_ASKYES },
  { "resolve",		DT_BOOL, R_NONE, OPTRESOLVE, 1 },
  { "reverse_alias",	DT_BOOL, R_BOTH, OPTREVALIAS, 0 },
  { "reverse_name",	DT_BOOL, R_BOTH, OPTREVNAME, 0 },
  { "save_address",	DT_BOOL, R_NONE, OPTSAVEADDRESS, 0 },
  { "save_empty",	DT_BOOL, R_NONE, OPTSAVEEMPTY, 1 },
  { "save_name",	DT_BOOL, R_NONE, OPTSAVENAME, 0 },
  { "send_charset",	DT_STR,  R_NONE, UL &SendCharset, UL "us-ascii:iso-8859-1:utf-8" },
  /*
  ** .pp
  ** A list of character sets for outgoing messages. Mutt will use the
  ** first character set into which the text can be converted exactly.
  ** If your ``$$charset'' is not iso-8859-1 and recipients may not
  ** understand UTF-8, it is advisable to include in the list an
  ** appropriate widely used standard character set (such as
  ** iso-8859-2, koi8-r or iso-2022-jp) either instead of or after
  ** "iso-8859-1".
  */
  { "sendmail",		DT_PATH, R_NONE, UL &Sendmail, UL SENDMAIL " -oem -oi" },
  { "sendmail_wait",	DT_NUM,  R_NONE, UL &SendmailWait, 0 },
  { "shell",		DT_PATH, R_NONE, UL &Shell, 0 },
  { "sig_dashes",	DT_BOOL, R_NONE, OPTSIGDASHES, 1 },
  { "signature",	DT_PATH, R_NONE, UL &Signature, UL "~/.signature" },
  { "simple_search",	DT_STR,	 R_NONE, UL &SimpleSearch, UL "~f %s | ~s %s" },
  { "smart_wrap",	DT_BOOL, R_PAGER, OPTWRAP, 1 },
  { "smileys",		DT_RX,	 R_PAGER, UL &Smileys, UL "(>From )|(:[-^]?[][)(><}{|/DP])" },
  { "sort",		DT_SORT, R_INDEX|R_RESORT, UL &Sort, SORT_DATE },
  { "sort_alias",	DT_SORT|DT_SORT_ALIAS,	R_NONE,	UL &SortAlias, SORT_ALIAS },
  { "sort_aux",		DT_SORT, R_INDEX|R_RESORT_BOTH, UL &SortAux, SORT_DATE },
  { "sort_browser",	DT_SORT|DT_SORT_BROWSER, R_NONE, UL &BrowserSort, SORT_SUBJECT },
  { "sort_re",		DT_BOOL, R_INDEX|R_RESORT_BOTH, OPTSORTRE, 1 },
  { "spoolfile",	DT_PATH, R_NONE, UL &Spoolfile, 0 },
  { "status_chars",	DT_STR,	 R_BOTH, UL &StChars, UL "-*%A" },
  { "status_format",	DT_STR,	 R_BOTH, UL &Status, UL "-%r-Mutt: %f [Msgs:%?M?%M/?%m%?n? New:%n?%?o? Old:%o?%?d? Del:%d?%?F? Flag:%F?%?t? Tag:%t?%?p? Post:%p?%?b? Inc:%b?%?l? %l?]---(%s/%S)-%>-(%P)---" },
  { "status_on_top",	DT_BOOL, R_BOTH, OPTSTATUSONTOP, 0 },
  { "strict_threads",	DT_BOOL, R_RESORT|R_INDEX, OPTSTRICTTHREADS, 0 },
  { "suspend",		DT_BOOL, R_NONE, OPTSUSPEND, 1 },
  { "text_flowed",      DT_BOOL, R_NONE, OPTTEXTFLOWED,  0 },
  /*
  ** .pp
  ** When set, mutt will generate text/plain; format=flowed attachments.
  ** This format is easier to handle for some mailing software, and generally
  ** just looks like ordinary text.  To actually make use of this format's 
  ** features, you'll need support in your editor.
  ** .pp
  ** Note that $$indent_string is ignored when this option is set.
  */
  { "thorough_search",	DT_BOOL, R_NONE, OPTTHOROUGHSRC, 0 },
  { "tilde",		DT_BOOL, R_PAGER, OPTTILDE, 0 },
  { "timeout",		DT_NUM,	 R_NONE, UL &Timeout, 600 },
  { "tmpdir",		DT_PATH, R_NONE, UL &Tempdir, 0 },
  { "to_chars",		DT_STR,	 R_BOTH, UL &Tochars, UL " +TCF" },
#ifdef USE_SOCKET
  { "tunnel",		 DT_STR, R_NONE, UL &Tunnel, UL 0 },
  /*
  ** .pp
  ** Setting this variable will cause mutt to open a pipe to a command
  ** instead of a raw socket. You may be able to use this to set up 
  ** preauthenticated connections to your IMAP/POP3 server. Example:
  ** .pp
  ** tunnel="ssh -q mailhost.net /usr/local/libexec/imapd"
  ** .pp
  ** NOTE: For this example to work you must be able to log in to the remote
  ** machine without having to enter a password.
  */  
#endif
  { "use_8bitmime",	DT_BOOL, R_NONE, OPTUSE8BITMIME, 0 },
  { "use_domain",	DT_BOOL, R_NONE, OPTUSEDOMAIN, 1 },
  { "use_from",		DT_BOOL, R_NONE, OPTUSEFROM, 1 },
  { "visual",		DT_PATH, R_NONE, UL &Visual, 0 },
  { "wait_key",		DT_BOOL, R_NONE, OPTWAITKEY, 1 },
  { "wrap_search",	DT_BOOL, R_NONE, OPTWRAPSEARCH, 1 },
  { "write_inc",	DT_NUM,	 R_NONE, UL &WriteInc, 10 },
  { "write_bcc",	DT_BOOL, R_NONE, OPTWRITEBCC, 1},
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
  { "exec",		mutt_parse_exec,	0 },
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
#ifdef _PGPPATH
  { "pgp-hook",		mutt_parse_hook,	M_PGPHOOK },
#endif /* _PGPPATH */
  { "push",		mutt_parse_push,	0 },
  { "reset",		parse_set,		M_SET_RESET },
  { "save-hook",	mutt_parse_hook,	M_SAVEHOOK },
  { "score",		mutt_parse_score,	0 },
  { "send-hook",	mutt_parse_hook,	M_SENDHOOK },
  { "set",		parse_set,		0 },
  { "source",		parse_source,		0 },
  { "toggle",		parse_set,		M_SET_INV },
  { "unalias",		parse_unalias,		0 },
  { "unhdr_order",	parse_unlist,		UL &HeaderOrderList },
  { "unignore",		parse_unignore,		0 },
  { "unlists",		parse_unlist,		UL &MailLists },
  { "unmono",		mutt_parse_unmono,	0 },
  { "unmy_hdr",		parse_unmy_hdr,		0 },
  { "unscore",		mutt_parse_unscore,	0 },
  { "unset",		parse_set,		M_SET_UNSET },
  { NULL }
};
#endif
