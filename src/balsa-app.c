/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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

#include <sys/stat.h>
#include <unistd.h>

#include "balsa-app.h"
#include "balsa-index.h"
#include "balsa-init.h"
#include "local-mailbox.h"
#include "misc.h"
#include "mailbox.h"
#include "save-restore.h"



/* Global application structure */
struct BalsaApplication balsa_app;


/* prototypes */
static void load_global_settings ();
static int mailboxes_init ();
static void setup_local_mailboxes ();
static void my_special_mailbox ();
static gint read_signature ();


static gint check_for_new_messages ();

void
init_balsa_app (int argc, char *argv[])
{
  /* include linkage for the c-client library */
#include "linkage.c"


  /* 
   * initalize application structure before ALL ELSE 
   * to some reasonable defaults
   */
  balsa_app.real_name = NULL;
  balsa_app.username = NIL;
  balsa_app.hostname = NIL;
  balsa_app.organization = NULL;
  balsa_app.local_mail_directory = NULL;
  balsa_app.smtp_server = NULL;
  balsa_app.auth_mailbox = NULL;
  balsa_app.current_mailbox = NULL;
  balsa_app.mailbox_list = NULL;
  balsa_app.current_index = NULL;
  balsa_app.addressbook_list = NULL;

  balsa_app.new_messages_timer = 0;
  balsa_app.new_messages = 0;

  balsa_app.check_mail_timer = 0;

  /* GUI settings */
  balsa_app.mw_width = MW_DEFAULT_WIDTH;
  balsa_app.mw_height = MW_DEFAULT_HEIGHT;
  balsa_app.toolbar_style = GTK_TOOLBAR_BOTH;


  restore_global_settings ();
  mailboxes_init ();
  load_local_mailboxes ();
  my_special_mailbox ();
  read_signature ();

  /* start timers */
  balsa_app.new_messages_timer = gtk_timeout_add (5, check_for_new_messages, NULL);
  balsa_app.check_mail_timer = gtk_timeout_add (5 * 60 * 1000, current_mailbox_check, NULL);
}

static gint
read_signature ()
{
  int fd, ret;
  struct stat stats;
  gchar path[PATH_MAX];
  sprintf (path, "%s/.signature", getenv ("HOME"));
  fd = open (path, O_RDONLY);
  if (fd == -1)
    {
      perror ("error opening signature file");
      return FALSE;
    }
  ret = fstat (fd, &stats);
  if (ret != 0)
    {
      perror ("error doing fstat on signature");
      close (fd);
      return FALSE;
    }
  balsa_app.signature = g_new (gchar, stats.st_size);
  ret = read (fd, balsa_app.signature, stats.st_size);
  if (ret > 0)
    balsa_app.signature[ret - 1] = '\0';
  close (fd);
  return TRUE;
}


static gint
check_for_new_messages ()
{
  if (!balsa_app.current_mailbox)
    return TRUE;

  if (balsa_app.current_mailbox->stream->lock)
    {
      if (balsa_app.debug)
	fprintf (stderr, "Lock exists, waiting\n");
    }
  else if (balsa_app.new_messages > 0 && balsa_app.current_index)
    {
      balsa_index_append_new_messages (BALSA_INDEX (balsa_app.current_index));
      balsa_app.new_messages = 0;
    }

  return TRUE;
}


static gint
mailboxes_init (void)
{
  gchar **mailboxes;
  gint num = 0;
  gint i = 0;

  if (gnome_config_get_string ("/balsa/Global/Accounts"))
    gnome_config_get_vector ("/balsa/Global/Accounts", &num, &mailboxes);

  for (i = 0; num > i; i++)
    {
      if (balsa_app.debug)
	fprintf (stderr, "Loaded mailbox: %s\n", mailboxes[i]);
      load_mailboxes (mailboxes[i]);
    }

  return 1;
}


/* This is where you can hard-code your own
 * mailbox for testing!
 */
static void
my_special_mailbox ()
{
#if 0
  MailboxNNTP *nntp;
  MailboxPOP3 *pop3;
  MailboxMBX *mbx;
  MailboxMH *mh;


  nntp = (MailboxNNTP *) mailbox_new (MAILBOX_NNTP);
  nntp->name = g_strdup ("COLA");
  nntp->user = g_strdup ("");
  nntp->passwd = g_strdup ("");
  nntp->server = g_strdup ("news.serv.net");
  nntp->newsgroup = g_strdup ("comp.os.linux.announce");
  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, nntp);

  pop3 = (MailboxPOP3 *) mailbox_new (MAILBOX_POP3);
  pop3->name = g_strdup ("MyPOP Box");
  pop3->user = g_strdup ("pavlov");
  pop3->passwd = g_strdup ("");
  pop3->server = g_strdup ("venus");
  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, pop3);

  mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
  mh->name = g_strdup ("gnome-list");
  mh->path = g_strdup ("gnome");
  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mh);

  mbx = (MailboxMBX *) mailbox_new (MAILBOX_MBX);
  mbx->name = g_strdup ("gnome-list-mbx");
  mbx->path = g_strdup ("/home/pavlov/gnomecvs.mbx");
  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbx);

  mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
  mh->name = g_strdup ("Gnome CVS");
  mh->path = g_strdup ("gnomecvs");
  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mh);

  mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
  mh->name = g_strdup ("gtk-list");
  mh->path = g_strdup ("gtk+");
  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mh);

#endif
}
