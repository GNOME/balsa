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

WHERE void (*mutt_error) (const char *, ...);

WHERE CONTEXT *Context;

WHERE char Errorbuf[SHORT_STRING];

WHERE char *AliasFile;
WHERE char *AliasFmt;
WHERE char *AttachSep;
WHERE char *Attribution;
WHERE char *AttachFormat;
WHERE char *Charset;
WHERE char *DefaultHook;
WHERE char *DateFmt;
WHERE char *DsnNotify;
WHERE char *DsnReturn;
WHERE char *Editor;
WHERE char *EscChar;
WHERE char *FolderFormat;
WHERE char *ForwFmt;
WHERE char *Fqdn;
WHERE char *HdrFmt;
WHERE char *Homedir;
WHERE char *Hostname;
#ifdef USE_IMAP
WHERE char *ImapAuthenticators INITVAL (NULL);
WHERE char *ImapPass INITVAL (NULL);
WHERE char *ImapUser INITVAL (NULL);
WHERE short ImapCheckTimeout;
/* BALSA: set initial value to something sensible */
WHERE char *ImapDelimChars INITVAL ("/.");
WHERE char *ImapHomeNamespace INITVAL (NULL);
WHERE char *ImapPreconnect INITVAL (NULL);
#endif
WHERE char *InReplyTo;
WHERE char *Inbox;
WHERE char *Ispell;
WHERE char *Locale;
WHERE char *MailcapPath;
WHERE char *Maildir;
WHERE char *MhFlagged;
WHERE char *MhReplied;
WHERE char *MhUnseen;
WHERE char *MsgFmt;
#ifdef USE_SOCKET
WHERE char *Preconnect INITVAL (NULL);
WHERE char *Tunnel INITVAL (NULL);
#endif /* USE_SOCKET */

WHERE char *Muttrc INITVAL (NULL);
WHERE char *Outbox;
WHERE char *Pager;
WHERE char *PagerFmt;
WHERE char *PipeSep;
#ifdef USE_POP
WHERE char *PopHost;
WHERE char *PopPass;
WHERE char *PopUser;
WHERE char *PopUID;
#endif
WHERE char *PostIndentString;
WHERE char *Postponed;
WHERE char *Prefix;
WHERE char *PrintCmd;
WHERE char *QueryCmd;
WHERE char *Realname;
WHERE char *SendCharset;
WHERE char *Sendmail;
WHERE char *Shell;
WHERE char *Signature;
WHERE char *SimpleSearch;
WHERE char *Spoolfile;
#if defined(USE_SSL) || defined(USE_NSS)
WHERE char *SslCertFile INITVAL (NULL);
WHERE char *SslEntropyFile INITVAL (NULL);
#endif
WHERE char *StChars;
/*WHERE char *Status; */
WHERE char *Tempdir;
WHERE char *Tochars;
WHERE char *Username;
/*WHERE char *Visual; */

WHERE char *LastFolder;

WHERE LIST *AutoViewList INITVAL(0);
WHERE LIST *AlternativeOrderList INITVAL(0);
WHERE LIST *HeaderOrderList INITVAL(0);
WHERE LIST *Ignore INITVAL(0);
WHERE LIST *UnIgnore INITVAL(0);
WHERE LIST *MailLists INITVAL(0);

#ifdef USE_IMAP
WHERE time_t ImapLastCheck INITVAL(0);
#endif

/* bit vector for boolean variables */
#ifdef GLOBALS_C
unsigned char Options[(OPTMAX + 7)/8];
#else
extern unsigned char Options[];
#endif

/* bit vector for the yes/no/ask variable type */
WHERE unsigned long QuadOptions INITVAL (0);

WHERE unsigned short Counter INITVAL (0);

WHERE short ConnectTimeout;
WHERE short HistSize;
WHERE short PagerContext;
WHERE short PagerIndexLines;
WHERE short PopPort;
WHERE short ReadInc;
WHERE short SendmailWait;
WHERE short Timeout;
WHERE short WriteInc;

#ifdef USE_IMAP
WHERE short ImapKeepalive;
#endif


/* flags for received signals */
WHERE volatile sig_atomic_t SigAlrm INITVAL (0);
WHERE volatile sig_atomic_t SigInt INITVAL (0);
WHERE volatile sig_atomic_t SigWinch INITVAL (0);

WHERE int CurrentMenu;

WHERE ALIAS *Aliases INITVAL (0);
WHERE LIST *UserHeader INITVAL (0);

#ifdef DEBUG
WHERE FILE *debugfile INITVAL (0);
WHERE int debuglevel INITVAL (0);
#endif

#ifdef GLOBALS_C
const char *Weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char *Months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", "ERR" };

const char *BodyTypes[] = { "x-unknown", "audio", "application", "image", "message", "model", "multipart", "text", "video" };
const char *BodyEncodings[] = { "x-unknown", "7bit", "8bit", "quoted-printable", "base64", "binary", "x-uuencoded" };
#else
extern const char *Weekdays[];
extern const char *Months[];
#endif

#ifdef GLOBALS_C
/* so that global vars get included */ 
#include "mx.h"
#include "mutt_regex.h"
#include "buffy.h"
#include "sort.h"
#ifdef _PGPPATH
#include "pgp.h"
#endif
#endif /* GLOBALS_C */
