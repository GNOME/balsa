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

/* nifty trick I stole from ELM 2.5alpha. */
#ifdef MAIN_C
#define WHERE 
#define INITVAL(x) = x
#else
#define WHERE extern
#define INITVAL(x) 
#endif

WHERE CONTEXT *Context;

WHERE char Errorbuf[SHORT_STRING];

WHERE char AliasFile[_POSIX_PATH_MAX];
WHERE char AliasFmt[SHORT_STRING];
WHERE char AttachSep[SHORT_STRING] INITVAL({0});
WHERE char Attribution[SHORT_STRING];
WHERE char Charset[SHORT_STRING];
WHERE char DecodeFmt[SHORT_STRING];
WHERE char DefaultHook[SHORT_STRING];
WHERE char DateFmt[SHORT_STRING];
WHERE char DsnNotify[SHORT_STRING] INITVAL({0});
WHERE char DsnReturn[SHORT_STRING] INITVAL({0});
WHERE char Editor[_POSIX_PATH_MAX];
WHERE char ForwFmt[SHORT_STRING];
WHERE char Fqdn[SHORT_STRING];
WHERE char HdrFmt[SHORT_STRING];
WHERE char Homedir[_POSIX_PATH_MAX];
WHERE char Hostname[SHORT_STRING];
WHERE char InReplyTo[SHORT_STRING];
WHERE char Inbox[_POSIX_PATH_MAX];
WHERE char Ispell[_POSIX_PATH_MAX];
WHERE char LastFolder[_POSIX_PATH_MAX];
WHERE char Locale[SHORT_STRING];
WHERE char MailcapPath[LONG_STRING];
WHERE char Maildir[_POSIX_PATH_MAX];
WHERE char MsgFmt[SHORT_STRING];
WHERE char Muttrc[_POSIX_PATH_MAX] INITVAL({0});
WHERE char Outbox[_POSIX_PATH_MAX] INITVAL({0});
WHERE char Pager[_POSIX_PATH_MAX];
WHERE char PagerFmt[SHORT_STRING];
WHERE char PipeSep[SHORT_STRING] INITVAL({0});
WHERE char PostIndentString[SHORT_STRING] INITVAL({0});
WHERE char Postponed[_POSIX_PATH_MAX];
WHERE char Prefix[SHORT_STRING];
WHERE char PrintCmd[_POSIX_PATH_MAX];
WHERE char Realname[SHORT_STRING];
WHERE char Sendmail[_POSIX_PATH_MAX];
WHERE char SendmailBounce[_POSIX_PATH_MAX];
WHERE char Shell[_POSIX_PATH_MAX];
WHERE char Signature[_POSIX_PATH_MAX];
WHERE char SimpleSearch[SHORT_STRING];
WHERE char Spoolfile[_POSIX_PATH_MAX];
WHERE char StChars[4]; /* -*%\0 */
WHERE char StatusString[STRING];
WHERE char Tempdir[_POSIX_PATH_MAX];
WHERE char Tochars[8]; /* " +TCF" */
WHERE char Username[SHORT_STRING];
WHERE char Visual[_POSIX_PATH_MAX];

WHERE LIST *AutoViewList INITVAL(0);
WHERE LIST *HeaderOrderList INITVAL(0);
WHERE LIST *Ignore INITVAL(0);
WHERE LIST *UnIgnore INITVAL(0);
WHERE LIST *MailLists INITVAL(0);

#ifdef USE_POP
WHERE char PopHost[SHORT_STRING] INITVAL({0});
WHERE char PopPass[SHORT_STRING] INITVAL({0});
WHERE char PopUser[SHORT_STRING] INITVAL({0});
#endif

/* bit vector for boolean variables */
#ifdef MAIN_C
unsigned char Options[(OPTMAX + 7)/8];
#else
extern unsigned char Options[];
#endif

/* bit vector for the yes/no/ask variable type */
WHERE unsigned long QuadOptions INITVAL (0);

WHERE char EscChar[2] INITVAL ("~"); /* $escape, used for $editor=builtin */

WHERE unsigned short Counter INITVAL (0);
WHERE short HistSize INITVAL (10);
WHERE short PagerContext INITVAL (0);
WHERE short PagerIndexLines INITVAL (0);
WHERE short PopPort INITVAL (110);
WHERE short ReadInc INITVAL (10);
WHERE short ScoreDefault INITVAL (10);
WHERE short Timeout INITVAL (600); /* seconds */
WHERE short TrimRef INITVAL (10);
WHERE short WriteInc INITVAL (10);

/* vector to store received signals */
WHERE short Signals INITVAL (0);

WHERE ALIAS *Aliases INITVAL (0);
WHERE LIST *UserHeader INITVAL (0);

#ifdef DEBUG
WHERE FILE *debugfile INITVAL (0);
WHERE int debuglevel INITVAL (0);
#endif

#ifdef USE_SETGID
WHERE gid_t MailGid;
WHERE gid_t UserGid;
#endif /* USE_SETGID */

#ifdef MAIN_C
const char *Weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char *Months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", "ERR" };

const char *BodyTypes[] = { "x-unknown", "audio", "application", "image", "message", "multipart", "text", "video" };
const char *BodyEncodings[] = { "x-unknown", "7bit", "8bit", "quoted-printable", "base64", "binary" };
#else
extern const char *Weekdays[];
extern const char *Months[];
#endif

#ifdef MAIN_C
/* so that global vars get included */ 
#include "mx.h"
#include "mutt_regex.h"
#include "buffy.h"
#include "sort.h"
#endif /* MAIN_C */
