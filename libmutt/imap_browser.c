/*
 * imap_browser Copyright (C) 1998 Grant McDorman <grant@isgtec.com>
 * Mutt Copyright (C) 1996-8 Michael R. Elkins <me@cs.hmc.edu>
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

#include <memory.h>
#include <stdlib.h>
#include <string.h>

#include "mutt.h"
#include "mutt_curses.h"
#include "mutt_menu.h"
#include "sort.h"
#include "mailbox.h"

#include "imap.h"

#define NOINFR  0x01
#define FDUMMY  0x02
#define FMRKTMP 0x03
#define FFDIR   0x04

struct imap_folder
{
  char *name;
  char *desc;
  int  fflags;
  int  messages;
  int  unseen;
  int  recent;
  int  uid;
};

struct browser_state
{
  struct imap_folder *entry;
  short entrylen; /* number of real entries */
  short entrymax;  /* max entry */
};

static struct mapping_t FolderHelp[] = {
  { "Exit",  OP_EXIT },
  { "Select", OP_CHANGE_DIRECTORY },
  { "Mask",  OP_ENTER_MASK },
  { "Help",  OP_HELP },
  { NULL }
};

typedef struct folder_t
{
  const char *name;
  int new;
} FOLDER;

static char LastDir[_POSIX_PATH_MAX] = "";

static REGEXP ImapMask;

#define IMAP_SORT_ORDER    0x01
#define IMAP_SORT_NAME     0x02
#define IMAP_SORT_MESSAGES 0x03
#define IMAP_SORT_UNSEEN   0x04

static int ImapBrowserSort = IMAP_SORT_NAME;

/* Frees up the memory allocated for the local-global variables.  */
static void destroy_state (struct browser_state *state)
{
  int c;

  for (c = 0; c < state->entrylen; c++)
  {
    safe_free ((void **) &((state->entry)[c].name));
    safe_free ((void **) &((state->entry)[c].desc));
  }
  safe_free ((void **) &state->entry);
}

static int browser_compare_subject (const void *a, const void *b)
{
  struct imap_folder *pa = (struct imap_folder *) a;
  struct imap_folder *pb = (struct imap_folder *) b;

  int r = strcmp (pa->name, pb->name);

  return ((ImapBrowserSort & SORT_REVERSE) ? -r : r);
}

static int browser_compare_messages (const void *a, const void *b)
{
  struct imap_folder *pa = (struct imap_folder *) a;
  struct imap_folder *pb = (struct imap_folder *) b;

  int r = (pa->messages - pb->messages);

  return ((ImapBrowserSort & SORT_REVERSE) ? -r : r);
}

static int browser_compare_unseen (const void *a, const void *b)
{
  struct imap_folder *pa = (struct imap_folder *) a;
  struct imap_folder *pb = (struct imap_folder *) b;

  int r = (pa->unseen - pb->unseen);

  return ((ImapBrowserSort & SORT_REVERSE) ? -r : r);
}

static void browser_sort (struct browser_state *state)
{
  int (*f) (const void *, const void *);

  switch (ImapBrowserSort & SORT_MASK)
  {
    case IMAP_SORT_ORDER:
      return;
    case IMAP_SORT_MESSAGES:
      f = browser_compare_messages;
      break;
    case IMAP_SORT_UNSEEN:
      f = browser_compare_unseen;
      break;
    case IMAP_SORT_NAME:
    default:
      f = browser_compare_subject;
      break;
  }
  qsort (state->entry, state->entrylen, sizeof (struct imap_folder), f);
}


static void add_folder (MUTTMENU *m, struct browser_state *state,
			const char *name, int fflags)
{
    char * cp;
    int len;
    int isdir = 0;
    
    if (fflags == (FDUMMY))
      return;           /* place holder only, no messages - ignore it */

    len = strlen(name);
    
    cp = strchr(name, '/');

    
    if (cp) {
        /* a directory */
        int i;

        isdir = 1;
        len = cp - name;    /* include slash */

        for (i = 0; i < state->entrylen; i++) {
            if (strncmp((state->entry)[i].name, name, len+1) == 0) {
                return; /* already have an entry */
            }
        }

    }

  if (state->entrylen == state->entrymax)
  {
    /* need to allocate more space */
    safe_realloc ((void **) &state->entry,
		  sizeof (struct imap_folder) * (state->entrymax += 256));
    if (m)
      m->data = state->entry;
  }

  len++;
  (state->entry)[state->entrylen].recent = 0;
  (state->entry)[state->entrylen].unseen = -1;
  (state->entry)[state->entrylen].messages = -1;
  (state->entry)[state->entrylen].name = safe_malloc(len);
  strncpy((state->entry)[state->entrylen].name, name, len);
  (state->entry)[state->entrylen].name[len] = '\0';
  (state->entry)[state->entrylen].fflags = fflags |
      (isdir ? FFDIR : 0);
  (state->entry)[state->entrylen].desc = safe_malloc(len + 24);
  if (isdir) {
    snprintf((state->entry)[state->entrylen].desc,
             len + 24,
             "%c           %s",
             ' ',       /* future read-only */
             (state->entry)[state->entrylen].name);
  } else {
    snprintf((state->entry)[state->entrylen].desc,
             len + 24,
             "%c    ?    ? %s",
             ' ',       /* future read-only */
             (state->entry)[state->entrylen].name);
  }
  (state->entrylen)++;
}

static void init_state (struct browser_state *state, MUTTMENU *menu)
{
  state->entrylen = 0;
  state->entrymax = 256;
  state->entry = (struct imap_folder *) safe_malloc (sizeof (struct imap_folder) * state->entrymax);
  if (menu)
    menu->data = state->entry;
}

static int examine_directory (MUTTMENU *menu, struct browser_state *state,
			      const char *d)
{
    char * param;
    char *p;
    char flags[127];
    char *fname;
    int len, fflags;
    CONTEXT ctx;
    char seq[16];
    char buf[LONG_STRING];
    int complete = 0;
    char * dir;
    int i;

    init_state (state, menu);

    /*
     * Open IMAP server
     */
    memset(&ctx, 0, sizeof ctx);
    ctx.path = (char *) d;
    if (imap_open_mailbox(&ctx) != 0) {
        mutt_error("Couldn't connect");
        return - 1;
    }
    dir = strchr(d, '}');
    if (dir == NULL)
      return -1;
    dir ++;
    if (*dir == '\0')
    {
      dir = "";
    }
    /*
     * Send LIST
     */
    imap_make_sequence(seq, sizeof(seq), &ctx);
    snprintf(buf, sizeof(buf), "%s LIST \"%s\" \"*\"\r\n", seq, dir);
    mutt_socket_write (ctx->conn, buf);
    FOREVER
    {
        if (mutt_socket_read_line_d(buf, sizeof buf, ctx->conn) == -1) {
            mutt_error("Communication error on LIST");
            break;
        }
        if (strncmp(buf, seq, strlen(seq)) == 0)
        {
            complete = 1;
            break;
        }
        if (strncmp(buf, "* LIST", 6) != 0) {
            mutt_error("Unexpected response from server");
            mutt_error(buf);
            break;
        }


        fflags = 0;

        param = buf + 7;      /* strlen ("* LIST ") */
        if (*param != '(')  	{
            mutt_error("Missing flags in LIST response");
            break;
        }

        param++;
        if ((p = strchr(param, ')')) == NULL) 	{
            mutt_error("Unterminated flag list in LIST response");
            break;
        }

        len = p - param;
        if (len > 126) {
            mutt_error("Flag list too long in LIST response");
            break;
        }

        strncpy(flags, param, len);
        flags[len] = '\0';
        param = p;

        if ((p = strtok(flags, " ")) != NULL)
          do {
              if (!strcasecmp(p, "\\Noinferiors"))
                fflags |= NOINFR;
              else
              if (!strcasecmp(p, "\\Noselect"))
                  fflags |= FDUMMY;
              else
              if (!strcasecmp(p, "\\Marked"))
                  fflags |= FMRKTMP;
          } while ((p = strtok(NULL, " ")) != NULL);

        param++;
        while (*param == ' ')
          param++;

        if ((p = strchr(param, ' ')) == NULL)	{
            mutt_error("Missing folder name in LIST response");
            break;
        }

        while (*p == ' ')
          p++;


        if (*p == '\"') {
            p++;
        }
        
        fname = p + strlen(dir);
        while (*p != '\"' && *p) {
            p++;
        }
        *p = '\0';
        if (regexec (ImapMask.rx, fname, 0, NULL, 0) == 0)
            add_folder(menu, state, fname, fflags);

    }

    /*
     * Get state information for all folders
     */
    for (i = 0; i < state->entrylen; i++) {
/*
 * Get folder state
 */
        if ((state->entry)[i].fflags & FFDIR) {
            continue;
        }

        imap_make_sequence(seq, sizeof(seq), &ctx);
        snprintf(buf, sizeof(buf), "%s STATUS \"%s%s\" (MESSAGES UNSEEN RECENT UIDVALIDITY)\r\n", seq,
                 dir, (state->entry)[i].name);
        mutt_socket_write (ctx->conn, buf);

    FOREVER
    {
        if (mutt_socket_read_line_d(buf, sizeof buf, ctx->conn) == -1) {
            mutt_error("Communication error on STATUS");
            break;
        }

        if (strncmp(buf, seq, strlen(seq)) == 0)
        {
            complete = 1;
            break;
        }
        if (strncmp(buf, "* STATUS ", 9) != 0) {
            mutt_error("Unexpected STATUS response");
            continue;
        }


        p = buf + 8;    /* strlen("* STATUS"); */
        while (*p == ' ' && *p)
          p++;

        if (*p == '"') {
            p = strchr(p+1, '"');
            if (p == NULL) {
                mutt_error("Invalid STATUS response: No closing quote");
                continue;
            }
            p++;
        } else {
            while (*p != ' ' && *p) {
                p++;
            }
        }

        while (*p == ' ' && *p)
          p++;
        if (p == NULL || *p == '\0') {
            mutt_error("Invalid STATUS response: No status");
            continue;
        }


        if (*p != '(') {
            mutt_error("Invalid STATUS response: No '('");
            continue;
        }
        param = p + 1;
        p = strrchr(param, ')');
        if (p == NULL || p == param + 1) {
            mutt_error("Invalid STATUS response: No ')'");
            continue;
        }
        *p = '\0';

        p = strtok(param, " ");
        while (p != NULL) {
            char *p1;

            if (!strcasecmp(p, "MESSAGES")) {
                p = strtok(NULL, " ");
                if (p == NULL) {
                    mutt_error("Missing MESSAGES value in STATUS response");
                    continue;
                }
                (state->entry)[i].messages = strtoul(p, &p1, 10);
                if (*p1) {
                    mutt_error("Non-numeric MESSAGES value in STATUS response");
                }
            } else if (!strcasecmp(p, "UNSEEN")) {
                p = strtok(NULL, " ");
                if (p == NULL) {
                    mutt_error("Missing UNSEEN value in STATUS response");
                    continue;
                }
                (state->entry)[i].unseen = strtoul(p, &p1, 10);
                if (*p1) {
                    mutt_error("Non-numeric UNSEEN value in STATUS response");
                }
            } else if (!strcasecmp(p, "RECENT")) {
                p = strtok(NULL, " ");
                if (p == NULL) {
                    mutt_error("Missing RECENT value in STATUS response");
                    continue;
                }
                (state->entry)[i].recent = strtoul(p, &p1, 10);
                if (*p1) {
                    mutt_error("Non-numeric RECENT value in STATUS response");
                }
            } else if (!strcasecmp(p, "UIDNEXT") ||
                       !strcasecmp(p, "UID-NEXT")) {
                p = strtok(NULL, " ");
                if (p == NULL) {
                    mutt_error("Missing UIDNEXT value in STATUS response");
                    continue;
                }
                (state->entry)[i].uid = strtoul(p, &p1, 10);
                if (*p1) {
                    mutt_error("Non-numeric UIDNEXT value in STATUS response");
                }
            } else if (!strcasecmp(p, "UIDVALIDITY") ||
                       !strcasecmp(p, "UID-VALIDITY")) {
                p = strtok(NULL, " ");
                if (p == NULL) {
                    mutt_error("Missing UIDVALIDITY value in STATUS response");
                    continue;
                }
                (state->entry)[i].uid = strtoul(p, &p1, 10);
                if (*p1) {
                    mutt_error("Non-numeric UIDVALIDITY value in STATUS response");
                }
            } else {
                mutt_error("Unknown STATUS parameter");
            }
            p = strtok(NULL, " ");
        }
        snprintf((state->entry)[i].desc, 24 + strlen((state->entry)[i].name),
                 "%c%c%4d %4d %s",
                 ' ',       /* future read-only */
                 (state->entry)[i].recent > 0 ? '*' : ' ',
                 (state->entry)[i].messages,
                 (state->entry)[i].unseen,
                 (state->entry)[i].name);
    }
    }

    imap_close_connection(&ctx);
    browser_sort (state);
    return complete ? 0 : -1;

}

static int select_file_search (MUTTMENU *menu, regex_t *re, int n)
{
  return (regexec (re, ((struct imap_folder *) menu->data)[n].name, 0, NULL, 0));
}

static void folder_entry (char *s, size_t slen, MUTTMENU *menu, int num)
{
  snprintf (s, slen, "%2d %s", num + 1, ((struct imap_folder *) menu->data)[num].desc);
}

static void init_menu (struct browser_state *state, MUTTMENU *menu, char *title,
		       size_t titlelen)
{

  menu->current = 0;
  menu->top = 0;
  menu->max = state->entrylen;
    snprintf (title, titlelen, "Directory [%s], File mask: %s",
	      LastDir, ImapMask.pattern);
  menu->redraw = REDRAW_FULL;
}

void mutt_select_imap_file (char * selected, int maxlen)
{
  char buf[STRING];
  char helpstr[SHORT_STRING];
  char title[STRING];
  struct browser_state state;
  MUTTMENU *menu;
  int i;
  char *cp;

  if (ImapMask.pattern == NULL) {
    regex_t *rx = (regex_t *) safe_malloc (sizeof (regex_t));
    REGCOMP (rx, ".*", REG_NOSUB | mutt_which_case (".*"));
    ImapMask.pattern = safe_strdup (".*");
    ImapMask.rx = rx;
  }

  *selected = '\0';

  strcpy(buf, LastDir);
  if (mutt_get_field ("List IMAP server: ", buf, sizeof (buf), 0) != 0)
    return;

  memset (&state, 0, sizeof (struct browser_state));
  
  if (examine_directory (NULL, &state, buf) == -1)
    return;
    
  strcpy(LastDir, buf);

  menu = mutt_new_menu ();
  menu->menu = MENU_FOLDER;
  menu->make_entry = folder_entry;
  menu->search = select_file_search;
  menu->title = title;
  menu->data = state.entry;

  menu->help = mutt_compile_help (helpstr, sizeof (helpstr), MENU_FOLDER, FolderHelp);

  init_menu (&state, menu, title, sizeof (title));

  FOREVER
  {
    switch (i = mutt_menuLoop (menu))
    {
      case OP_GENERIC_SELECT_ENTRY:

	if (!state.entrylen)
	{
	  mutt_error ("No files match the file mask");
	  break;
	}
        
        cp = state.entry[menu->current].name;
        i = strlen(cp);
        if (i > 0 && cp[i - 1] == '/') {
            /* directory - select it */
            strcpy(buf, LastDir);
            strcat(buf, cp);
	    if (examine_directory (menu, &state, buf) == 0) {
              strcpy(LastDir, buf);
	      init_menu (&state, menu, title, sizeof (title));
            }
	    else
	    {
	      mutt_error ("Error scanning directory.");
	      mutt_menuDestroy (&menu);
	      return;
            }
            
            break;
        }
        
        strncpy(selected, LastDir, maxlen);
        strncat(selected, state.entry[menu->current].name,
                maxlen - strlen(LastDir));
        selected[maxlen - 1] = '\0';
        

	/* Fall through to OP_EXIT */

      case OP_EXIT:

	destroy_state (&state);
	mutt_menuDestroy (&menu);
	return;

      case OP_CHANGE_DIRECTORY:
        strcpy(buf, LastDir);
	if (mutt_get_field ("Directory: ", buf, sizeof (buf), 0) == 0)
	{
	    if (examine_directory (menu, &state, buf) == 0)
            {
              strcpy(LastDir, buf);
	      init_menu (&state, menu, title, sizeof (title));
            }
	    else
	    {
	      mutt_error ("Error scanning directory.");
	      mutt_menuDestroy (&menu);
	      return;
            }
        }
	break;
	
      case OP_ENTER_MASK:

	strfcpy (buf, ImapMask.pattern, sizeof (buf));
	if (mutt_get_field ("File Mask: ", buf, sizeof (buf), 0) == 0)
	{
	  regex_t *rx = (regex_t *) safe_malloc (sizeof (regex_t));
	  int err;

	  /* assume that the user wants to see everything */
	  if (!buf[0])
	    strfcpy (buf, ".", sizeof (buf));

	  if ((err = REGCOMP (rx, buf, REG_NOSUB | mutt_which_case (buf))) != 0)
	  {
	    regerror (err, rx, buf, sizeof (buf));
	    regfree (rx);
	    safe_free ((void **) &rx);
	    mutt_error ("%s", buf);
	  }
	  else
	  {
	    safe_free ((void **) &ImapMask.pattern);
	    regfree (ImapMask.rx);
	    safe_free ((void **) &ImapMask.rx);
	    ImapMask.pattern = safe_strdup (buf);
	    ImapMask.rx = rx;

	    destroy_state (&state);
	    if (examine_directory (menu, &state, LastDir) == 0)
	      init_menu (&state, menu, title, sizeof (title));
	    else
	    {
	      mutt_error ("Error scanning directory.");
	      mutt_menuDestroy (&menu);
	      return;
	    }
	  }
	}
	MAYBE_REDRAW (menu->redraw);
	break;

      case OP_SORT:
      case OP_SORT_REVERSE:

	{
	  int reverse = 0;

	  move (LINES - 1, 0);
	  if (i == OP_SORT_REVERSE)
	  {
	    reverse = SORT_REVERSE;
	    addstr ("Reverse ");
	  }
	  addstr ("Sort by (a)lpha, (m)essages, (u)nread or do(n)'t sort? ");
	  clrtoeol ();

	  while ((i = mutt_getch ()) != EOF && i != 'a'
                 && i != 'm'
                 && i != 'u'
		 && i != 'n')
	  {
	    if (i == ERR || CI_is_return (i))
	      break;
	    else
	      BEEP ();
	  }

	  if (i != EOF)
	  {
	    switch (i)
	    {
	      case 'a': 
	        ImapBrowserSort = reverse | IMAP_SORT_NAME;
		break;
	      case 'n': 
	        ImapBrowserSort = IMAP_SORT_ORDER;
		break;
              case 'm':
                ImapBrowserSort = reverse | IMAP_SORT_MESSAGES;
                break;
              case 'u':
                ImapBrowserSort = reverse | IMAP_SORT_UNSEEN;
                break;
	    }
	    browser_sort (&state);
	    menu->redraw = REDRAW_FULL;
	  }
	}

	break;

      case OP_CHECK_NEW:

	destroy_state (&state);
        if (examine_directory (menu, &state, LastDir) == -1)
	  return;
	init_menu (&state, menu, title, sizeof (title));
	break;

      case OP_BROWSER_NEW_FILE:

	snprintf (buf, sizeof (buf), "%s/", LastDir);
	if (mutt_get_field ("New file name: ", buf, sizeof (buf), M_FILE) == 0)
	{
            break;
	}
/*        imap_exec(make new folder ... */
	MAYBE_REDRAW (menu->redraw);
	break;
    }
  }
  /* not reached */
}
