/* Balsa E-Mail Client

 * This file handles Balsa's interaction with libPropList, which stores
 * Balsa's configuration information.
 *
 * This file is Copyright (C) 1998-1999 Nat Friedman
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

#include "config.h"

#include "libbalsa.h"

#include <assert.h>

#include <gnome.h>
#include <proplist.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "balsa-app.h"
#include "misc.h"
#include "save-restore.h"
#include "../libmutt/mutt.h"

static proplist_t pl_dict_add_str_str (proplist_t dict_arg, gchar * string1,
				       gchar * string2);
static gchar *pl_dict_get_str (proplist_t dict, gchar * str);
static proplist_t config_mailbox_get_by_name (gchar * name);
static proplist_t config_mailbox_get_key (proplist_t mailbox);
static gint config_mailbox_init (proplist_t mbox, gchar * key);
static gint config_mailbox_get_highest_number (proplist_t accounts);
static gchar *rot (gchar * pass);

static gchar *
rot (gchar * pass)
{
  gchar *buff;
  gint len = 0, i = 0;

  /*PKGW: let's do the assert() BEFORE we coredump... */
  assert( pass != NULL );

  len = strlen (pass);
  buff = g_strdup (pass);

  /* Used to assert( buff ); this is wrong. If g_strdup
     fails we're screwed anyway to let the upcoming coredump
     occur. */

  assert (pass != NULL);
  assert (buff != NULL);	/* TODO: using assert for this case is wrong!
				 * The error should be handled gracefully.
				 */

  for (i = 0; i < len; i++)
    {
      if ((buff[i] <= 'M' && buff[i] >= 'A')
	  || (buff[i] <= 'm' && buff[i] >= 'a'))
	buff[i] += 13;
      else if ((buff[i] <= 'Z' && buff[i] >= 'N')
	       || (buff[i] <= 'z' && buff[i] >= 'n'))
	buff[i] -= 13;
    }
  return buff;
}

/* Load the configuration from the specified file. The filename is
   taken to be relative to the user's home directory, as if "~/" had
   been prepended.  Returns TRUE on success and FALSE on failure. */
gint
config_load (gchar * user_filename)
{
  char *filename;

  /* Deallocate the internal proplist first */
  if (balsa_app.proplist != NULL)
    PLRelease (balsa_app.proplist);

  /* Construct the filename by appending 'user_filename' to the user's
     home directory. */

  filename = g_strdup_printf("%s/%s", g_get_home_dir(), user_filename);

  balsa_app.proplist = PLGetProplistWithPath (filename);

  g_free (filename);

  if (balsa_app.proplist == NULL)
    return FALSE;

  return TRUE;
}				/* config_load */

/* Save the internal configuration information into the specified
   file.  Just as with load_config, the specified filename is taken to
   be relative to the user's home directory.  Returns TRUE on success
   and FALSE on failure. */
gint
config_save (gchar * user_filename)
{
  proplist_t temp_str;
  char *filename;
  int fd;

  /* Be sure that there is data to save first */
  if (balsa_app.proplist == NULL)
    {
      fprintf (stderr, "config_save: proplist is NULL!\n");
      return FALSE;
    }

  /* Construct the filename by appending 'user_filename' to the user's
     home directory. */
  filename = g_strdup_printf("%s/%s", g_get_home_dir(), user_filename);

  /* Set the property list's filename */
  temp_str = PLMakeString (filename);
  balsa_app.proplist = PLSetFilename (balsa_app.proplist, temp_str);
  PLRelease (temp_str);

  /* There might be passwords and things in the file, so we chmod the
     file to 0600 BEFORE we write any data into it.  This involves
     creating the file independently of PLSave. */
  fd = creat (filename, S_IRUSR | S_IWUSR);
  if (fd < 0)
    {
      fprintf (stderr, "config_save: Error writing config file %s!\n",
	       filename);
      g_free (filename);
      return FALSE;
    }
  close (fd);

  if (!PLSave (balsa_app.proplist, 0))
    {
      fprintf (stderr, "config_save: Error writing %s!\n", filename);
      return FALSE;
    }

  g_free (filename);

  return TRUE;
}				/* config_save */


/* This routine inserts a new mailbox into the configuration's
 * accounts vector, modifying both the resident and on-disk
 * configurations.  The mailbox is specified by 'mailbox', and 'key'
 * actually refers to whether this is a special mailbox, such as
 * "Inbox", "Outbox", or "Trash", or whether it is a "generic"
 * mailbox.  If key is NULL, the mailbox's key is determine
 * automatically.  The function returns TRUE on success and FALSE on
 * failure.
 */
gint
config_mailbox_add (Mailbox * mailbox, char *key_arg)
{
  proplist_t mbox_dict, accounts, temp_str;
  char key[MAX_PROPLIST_KEY_LEN];

  /* Initialize the key in case it is accidentally used uninitialized */
  strcpy (key, "AnErrorOccurred");

  /* Each mailbox is stored as a Proplist dictionary of mailbox settings.
     First create the dictionary, then add it to the "accounts" section
     of the global configuration. */
  switch (mailbox->type)
    {
    case MAILBOX_MBOX:
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      /* Create a new mailbox configuration with the following entries:
         Type = local;
         Name = ... ;
         Path = ... ;
       */
      mbox_dict = pl_dict_add_str_str (NULL, "Type", "local");
      pl_dict_add_str_str (mbox_dict, "Name", mailbox->name);
      pl_dict_add_str_str (mbox_dict, "Path", MAILBOX_LOCAL (mailbox)->path);

      break;

    case MAILBOX_POP3:
      /* Create a new mailbox configuration with the following entries:
         Type = POP3;
         Name = ...;
         Username = ...;
         Password = ...;
         Server = ...;
	 Check = 0 | 1;
	 Delete =  0 | 1;
       */
      mbox_dict = pl_dict_add_str_str (NULL, "Type", "POP3");
      pl_dict_add_str_str (mbox_dict, "Name", mailbox->name);
      pl_dict_add_str_str (mbox_dict, "Username",
			   MAILBOX_POP3(mailbox)->server->user);
      /* Do not save the password if the field is NULL.  This is here
         so that asving the password to the balsarc file can be optional */
      if (MAILBOX_POP3(mailbox)->server->passwd)
	{
	  gchar *buff;
	  buff = rot (MAILBOX_POP3(mailbox)->server->passwd);
	  pl_dict_add_str_str (mbox_dict, "Password",
			       buff);
	  g_free (buff);
	}
      pl_dict_add_str_str (mbox_dict, "Server",
			   MAILBOX_POP3 (mailbox)->server->host);
      {
	char tmp[32];

	snprintf (tmp, sizeof (tmp), "%d", MAILBOX_POP3 (mailbox)->check);
	pl_dict_add_str_str (mbox_dict, "Check", tmp);

	snprintf (tmp, sizeof (tmp), "%d", MAILBOX_POP3 (mailbox)->delete_from_server);
	pl_dict_add_str_str (mbox_dict, "Delete", tmp);
      }

        if ((MAILBOX_POP3 (mailbox)->last_popped_uid) != NULL)
		pl_dict_add_str_str (mbox_dict, "LastUID", MAILBOX_POP3 (mailbox)->last_popped_uid);
	  
      break;

    case MAILBOX_IMAP:
      /* Create a new mailbox configuration with the following entries:
         Type = IMAP;
         Name = ...;
         Server = ...;
         Port = ...;
         Path = ...;
         Username = ...;
         Password = ...;
       */
      mbox_dict = pl_dict_add_str_str (NULL, "Type", "IMAP");
      pl_dict_add_str_str (mbox_dict, "Name", mailbox->name);
      pl_dict_add_str_str (mbox_dict, "Server",
			   MAILBOX_IMAP (mailbox)->server->host);

      /* Add the Port entry */
      {
	char tmp[MAX_PROPLIST_KEY_LEN];
	snprintf (tmp, sizeof (tmp), "%d", MAILBOX_IMAP(mailbox)->server->port);
	pl_dict_add_str_str (mbox_dict, "Port", tmp);
      }

      pl_dict_add_str_str (mbox_dict, "Path", MAILBOX_IMAP (mailbox)->path);
      pl_dict_add_str_str (mbox_dict, "Username",
			   MAILBOX_IMAP (mailbox)->server->user);

      if (MAILBOX_IMAP(mailbox)->server->passwd != NULL)
      {
	gchar *buff;
	buff = rot (MAILBOX_IMAP(mailbox)->server->passwd);
	pl_dict_add_str_str (mbox_dict, "Password", buff);
	g_free (buff);
      }
      break;

    default:
      fprintf (stderr, "config_mailbox_add: Unknown mailbox type!\n");
      return FALSE;
    }

  /* If the configuration file has not been started, create it */
  if (balsa_app.proplist == NULL)
    {
      /* This is the special undocumented way to create an empty dictionary */
      balsa_app.proplist =
	PLMakeDictionaryFromEntries (NULL, NULL, NULL);
    }

  /* Now, add this newly created account to the list of mailboxes in
     the configuration file. */
  temp_str = PLMakeString ("Accounts");
  accounts = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease (temp_str);

  if (accounts == NULL)
    {
      if (key_arg == NULL)
	strcpy (key, "m1");
      else
	snprintf (key, sizeof (key), "%s", key_arg);

      /* If there is no Accounts list in the global proplist, create
         one and add it to the global configuration dictionary. */
      temp_str = PLMakeString (key);
      accounts = PLMakeDictionaryFromEntries (temp_str, mbox_dict, NULL);
      PLRelease (temp_str);

      temp_str = PLMakeString ("Accounts");
      PLInsertDictionaryEntry (balsa_app.proplist, temp_str, accounts);
      PLRelease (temp_str);
    }
  else
    {
      /* Before we can add the mailbox to the configuration, we need
         to pick a unique key for it.  "Inbox", "Outbox" and "Trash"
         all have unique keys, but for all other mailboxes, we are
         simply passed NULL, meaning that we must supply the key ourselves. */
      if (key_arg == NULL)
	{
	  int mbox_max;
	
	  mbox_max = config_mailbox_get_highest_number (accounts);
	  snprintf (key, sizeof (key), "m%d", mbox_max + 1);
	}
      else
	snprintf (key, sizeof (key), "%s", key_arg);

      /* If there is already an Accounts list, just add this new mailbox */
      temp_str = PLMakeString (key);
      PLInsertDictionaryEntry (accounts, temp_str, mbox_dict);
      PLRelease (temp_str);
    }

  return config_save (BALSA_CONFIG_FILE);
}				/* config_mailbox_add */

/* Remove the specified mailbox from the list of accounts.  Note that
   the mailbox is referenced by its 'Name' field here, so you had
   better make sure those stay unique.  Returns TRUE if the mailbox
   was succesfully deleted and FALSE otherwise. */
gint
config_mailbox_delete (gchar * name)
{
  proplist_t accounts, mbox, mbox_key, temp_str;

  if (balsa_app.proplist == NULL)
    {
      fprintf (stderr, "config_mailbox_delete: No configuration loaded!\n");
      return FALSE;
    }

  /* Grab the list of accounts */
  temp_str = PLMakeString ("Accounts");
  accounts = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease (temp_str);
  if (accounts == NULL)
    return FALSE;

  /* Grab the specified mailbox */
  mbox = config_mailbox_get_by_name (name);
  if (mbox == NULL)
    return FALSE;

  /* Now grab the associated key */
  mbox_key = config_mailbox_get_key (mbox);
  if (mbox_key == NULL)
    return FALSE;

  accounts = PLRemoveDictionaryEntry (accounts, mbox_key);

  config_save (BALSA_CONFIG_FILE);
  return TRUE;
}				/* config_mailbox_delete */

/* Update the configuration information for the specified mailbox. */
gint
config_mailbox_update (Mailbox * mailbox, gchar * old_mbox_name)
{
  proplist_t mbox, mbox_key;
  gchar key[MAX_PROPLIST_KEY_LEN];


  mbox = config_mailbox_get_by_name (old_mbox_name);
  mbox_key = config_mailbox_get_key (mbox);

  if (mbox_key == NULL)
    {
      strcpy (key, "generic");
    }
  else
    {
      strcpy (key, PLGetString (mbox_key));
    }

  config_mailbox_delete (old_mbox_name);
  config_mailbox_add (mailbox, key);


  return config_save (BALSA_CONFIG_FILE);
}				/* config_mailbox_update */

/* This function initializes all the mailboxes internally, going through
   the list of all the mailboxes in the configuration file one by one. */
gint
config_mailboxes_init (void)
{
  proplist_t accounts, temp_str, mbox, key;
  int num_elements, i;

  g_assert (balsa_app.proplist != NULL);

  temp_str = PLMakeString ("Accounts");
  accounts = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease (temp_str);

  if (accounts == NULL)
    return FALSE;

  num_elements = PLGetNumberOfElements (accounts);
  for (i = 0; i < num_elements; i++)
    {
      key = PLGetArrayElement (accounts, i);
      mbox = PLGetDictionaryEntry (accounts, key);
      config_mailbox_init (mbox, PLGetString (key));
    }

  return TRUE;
}				/* config_mailboxes_init */

/* Initialize the specified mailbox, creating the internal data
   structures which represent the mailbox. */
static gint
config_mailbox_init (proplist_t mbox, gchar * key)
{
  proplist_t accounts, temp_str;
  MailboxType mailbox_type;
  Mailbox *mailbox;
  gchar *mailbox_name, *type, *field;
  GNode *node;

  g_assert (mbox != NULL);
  g_assert (key != NULL);

  mailbox_type = MAILBOX_UNKNOWN;
  mailbox = NULL;

  /* Grab the list of mailboxes */
  temp_str = PLMakeString ("Accounts");
  accounts = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease (temp_str);
  if (accounts == NULL)
    return FALSE;

  /* All mailboxes have a type and a name.  Grab those. */
  type = pl_dict_get_str (mbox, "Type");
  if (type == NULL)
    {
      fprintf (stderr, "config_mailbox_init: mailbox type not set\n");
      return FALSE;
    }
  mailbox_name = g_strdup (pl_dict_get_str (mbox, "Name"));
  if (mailbox_name == NULL)
    mailbox_name = g_strdup ("Friendly Mailbox Name");

  /* Now grab the mailbox-type-specific fields */
  if (!strcasecmp (type, "local"))	/* Local mailbox */
    {
      gchar *path;

      path = pl_dict_get_str (mbox, "Path");
      if (path == NULL)
	return FALSE;

      mailbox_type = mailbox_valid (path);
      if (mailbox_type != MAILBOX_UNKNOWN)
	{
	  mailbox = BALSA_MAILBOX(mailbox_new (mailbox_type));
	  mailbox->name = mailbox_name;
	  MAILBOX_LOCAL (mailbox)->path = g_strdup (path);
	}
      else
	{
	  fprintf (stderr, "config_mailbox_init: Cannot identify type of "
		   "local mailbox %s\n", mailbox_name);
	  return FALSE;
	}
    }
  else if (!strcasecmp (type, "POP3"))	/* POP3 mailbox */
    {
      mailbox = BALSA_MAILBOX(mailbox_new (MAILBOX_POP3));
      mailbox->name = mailbox_name;

      if ((field = pl_dict_get_str (mbox, "Username")) == NULL)
	return FALSE;
      MAILBOX_POP3 (mailbox)->server->user = g_strdup (field);

      if ((field = pl_dict_get_str (mbox, "Password")) != NULL)
	{
	  gchar *buff ;
	  buff = rot (field);
	  MAILBOX_POP3 (mailbox)->server->passwd = g_strdup (buff);
	  g_free (buff);
	}
      else
	MAILBOX_POP3 (mailbox)->server->passwd = NULL;

      if ((field = pl_dict_get_str (mbox, "Server")) == NULL)
	return FALSE;
      MAILBOX_POP3 (mailbox)->server->host = g_strdup (field);

      if ((field = pl_dict_get_str (mbox, "Check")) == NULL)
	MAILBOX_POP3 (mailbox)->check = FALSE;
      else
	MAILBOX_POP3 (mailbox)->check = atol (field);

      if ((field = pl_dict_get_str (mbox, "Delete")) == NULL)
	MAILBOX_POP3 (mailbox)->delete_from_server = FALSE;
      else
	MAILBOX_POP3 (mailbox)->delete_from_server = atol (field);

	  if ((field = pl_dict_get_str (mbox, "LastUID")) == NULL)
	MAILBOX_POP3 (mailbox)->last_popped_uid = NULL;
      else
	MAILBOX_POP3 (mailbox)->last_popped_uid = g_strdup (field);



      balsa_app.inbox_input =
	g_list_append (balsa_app.inbox_input, mailbox);
    }
  else if (!strcasecmp (type, "IMAP"))	/* IMAP Mailbox */
    {
      mailbox = BALSA_MAILBOX(mailbox_new (MAILBOX_IMAP));
      mailbox->name = mailbox_name;

      if ((field = pl_dict_get_str (mbox, "Username")) == NULL)
	return FALSE;
      MAILBOX_IMAP (mailbox)->server->user = g_strdup (field);

      if ((field = pl_dict_get_str (mbox, "Password")) != NULL)
      {
	gchar *buff;
	buff = rot (field);
	MAILBOX_IMAP (mailbox)->server->passwd = g_strdup (buff);
	g_free (buff);
      }
      else
	MAILBOX_IMAP (mailbox)->server->passwd = NULL;

      if ((field = pl_dict_get_str (mbox, "Server")) == NULL)
	return FALSE;
      MAILBOX_IMAP (mailbox)->server->host = g_strdup (field);

      if ((field = pl_dict_get_str (mbox, "Port")) == NULL)
	return FALSE;
      MAILBOX_IMAP (mailbox)->server->port = atol (field);

      if ((field = pl_dict_get_str (mbox, "Path")) == NULL)
	return FALSE;
      MAILBOX_IMAP (mailbox)->path = g_strdup (field);

    }
  else
    {
      fprintf (stderr, "config_mailbox_init: Unknown mailbox type \"%s\" "
	       "on mailbox %s\n", type, mailbox_name);
    }

  if (strcmp ("Inbox", key) == 0)
    {
      balsa_app.inbox = mailbox;
    }
  else if (strcmp ("Outbox", key) == 0)
    {
      balsa_app.outbox = mailbox;
    }
  else if (strcmp ("Sentbox", key) == 0)
    {
      balsa_app.sentbox = mailbox;
    }
  else if (strcmp ("Draftbox", key) == 0)
    {
      balsa_app.draftbox = mailbox;
    }
  else if (strcmp ("Trash", key) == 0)
    {
      balsa_app.trash = mailbox;
    }
  else
    {
      if (mailbox_type == MAILBOX_MH)
	node = g_node_new (mailbox_node_new (g_strdup (mailbox->name),
					     mailbox, TRUE));
      else
	node = g_node_new (mailbox_node_new (g_strdup (mailbox->name),
					     mailbox, FALSE));
      g_node_append (balsa_app.mailbox_nodes, node);
    }

  return TRUE;
}				/* config_mailbox_init */


/* Load Balsa's global settings */
gint
config_global_load (void)
{
  proplist_t globals, temp_str;
  gchar *field;

  g_assert (balsa_app.proplist != NULL);

  temp_str = PLMakeString ("Globals");
  globals = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease (temp_str);
  if (globals == NULL)
    {
      fprintf (stderr, "config_global_load: Global settings not "
	       "present in config file!\n");
      return FALSE;
    }

  /* user's real name */
  if ((field = pl_dict_get_str (globals, "RealName")) == NULL)
    return FALSE;
  g_free (balsa_app.address->personal);
  balsa_app.address->personal = g_strdup (field);

  /* user's email address */
  if ((field = pl_dict_get_str (globals, "Email")) == NULL)
    return FALSE;
  g_free (balsa_app.address->mailbox);
  balsa_app.address->mailbox = g_strdup (field);

  /* users's replyto address */
  g_free (balsa_app.replyto);
  if ((field = pl_dict_get_str (globals, "ReplyTo")) == NULL)
    balsa_app.replyto = g_strdup (balsa_app.address->mailbox);
  else
    balsa_app.replyto = g_strdup (field);

  /* bcc field for outgoing mails */
  g_free (balsa_app.bcc);
  if ((field = pl_dict_get_str (globals, "Bcc")) == NULL)
    ;  /* optional */
  else
    balsa_app.bcc = g_strdup (field);

  /* directory */
  if ((field = pl_dict_get_str (globals, "LocalMailDir")) == NULL)
    return FALSE;
  g_free (balsa_app.local_mail_directory);
  balsa_app.local_mail_directory = g_strdup (field);

  /* signature file path */
  g_free (balsa_app.signature_path);
  if ((field = pl_dict_get_str (globals, "SignaturePath")) == NULL)
    {
      balsa_app.signature_path = g_malloc (strlen (g_get_home_dir ()) + 12);
      sprintf (balsa_app.signature_path, "%s/.signature", g_get_home_dir ());
    }
  else
    balsa_app.signature_path = g_strdup (field);

  if ((field = pl_dict_get_str (globals, "SigSending")) == NULL)
    balsa_app.sig_sending = TRUE;
  else
   balsa_app.sig_sending = atoi ( field );

  if ((field = pl_dict_get_str (globals, "SigReply")) == NULL)
    balsa_app.sig_whenreply = TRUE;
  else
    balsa_app.sig_whenreply = atoi ( field );

  if ((field = pl_dict_get_str (globals, "SigForward")) == NULL)
    balsa_app.sig_whenforward = TRUE;
  else
    balsa_app.sig_whenforward = atoi ( field );

  /* smtp server */
  if ((field = pl_dict_get_str (globals, "SMTPServer")) == NULL)
    ;				/* an optional field for now */
  g_free (balsa_app.smtp_server);
  balsa_app.smtp_server = g_strdup (field);
 
  if ((field = pl_dict_get_str (globals, "SMTP")) == NULL)
	      balsa_app.smtp = FALSE;
   else {
       balsa_app.smtp = atoi (field);
       if (balsa_app.smtp_server==NULL)
	       balsa_app.smtp = FALSE;
   }

  /* Check mail timer */
  if ((field = pl_dict_get_str (globals, "CheckMailAuto")) == NULL)
    balsa_app.check_mail_auto = FALSE;
  else
    balsa_app.check_mail_auto = atoi (field);

  if ((field = pl_dict_get_str (globals, "CheckMailMinutes")) == NULL)
    balsa_app.check_mail_timer = 10;
  else
    balsa_app.check_mail_timer = atoi (field);

  if (balsa_app.check_mail_timer < 1 )
    balsa_app.check_mail_timer = 10;

  if( balsa_app.check_mail_auto )
    update_timer( TRUE, balsa_app.check_mail_timer );

  /* Word Wrap */
  if ((field = pl_dict_get_str (globals, "WordWrap")) == NULL)
    balsa_app.wordwrap = TRUE;
  else
    balsa_app.wordwrap = atoi (field);

  if ((field = pl_dict_get_str (globals, "WrapLength")) == NULL)
    balsa_app.wraplength = 79;
  else
    balsa_app.wraplength = atoi (field);

  if (balsa_app.wraplength < 40 )
    balsa_app.wraplength = 40;

  /* toolbar style */
  if ((field = pl_dict_get_str (globals, "ToolbarStyle")) == NULL)
    balsa_app.toolbar_style = GTK_TOOLBAR_BOTH;
  else
    balsa_app.toolbar_style = atoi (field);

  /* Progress Window Dialog */
  if ((field = pl_dict_get_str (globals, "PWindowOption")) == NULL)
    balsa_app.pwindow_option = WHILERETR;
  else
    balsa_app.pwindow_option = atoi (field);

  /* use the preview pane */
  if ((field = pl_dict_get_str (globals, "UsePreviewPane")) == NULL)
    balsa_app.previewpane = TRUE;
  else
    balsa_app.previewpane = atoi (field);
#ifdef BALSA_SHOW_INFO
  /* show mailbox content info */
  if ((field = pl_dict_get_str (globals, "ShowMailboxContentInfo")) == NULL)
    balsa_app.mblist_show_mb_content_info = TRUE;
  else
    balsa_app.mblist_show_mb_content_info = atoi (field);
#endif
  /* debugging enabled */
  if ((field = pl_dict_get_str (globals, "Debug")) == NULL)
    balsa_app.debug = FALSE;
  else
    balsa_app.debug = atoi (field);

  /* window sizes */
  if ((field = pl_dict_get_str (globals, "MainWindowWidth")) == NULL)
    balsa_app.mw_width = 640;
  else
    balsa_app.mw_width = atoi (field);

  if ((field = pl_dict_get_str (globals, "MainWindowHeight")) == NULL)
    balsa_app.mw_height = 480;
  else
    balsa_app.mw_height = atoi (field);

  if ((field = pl_dict_get_str (globals, "MailboxListWidth")) == NULL)
    balsa_app.mblist_width = 100;
  else
    balsa_app.mblist_width = atoi (field);

  /* restore column sizes from previous session */
  if ((field = pl_dict_get_str (globals, "IndexNumWidth")) == NULL)
    balsa_app.index_num_width = NUM_DEFAULT_WIDTH;
  else
    balsa_app.index_num_width = atoi (field);

  if ((field = pl_dict_get_str (globals, "IndexUnreadWidth")) == NULL)
    balsa_app.index_unread_width = UNREAD_DEFAULT_WIDTH;
  else
    balsa_app.index_unread_width = atoi (field);

  if ((field = pl_dict_get_str (globals, "IndexFlagWidth")) == NULL)
    balsa_app.index_flag_width = FLAG_DEFAULT_WIDTH;
  else
    balsa_app.index_flag_width = atoi (field);

  if ((field = pl_dict_get_str (globals, "IndexFromWidth")) == NULL)
    balsa_app.index_from_width = FROM_DEFAULT_WIDTH;
  else
    balsa_app.index_from_width = atoi (field);

  if ((field = pl_dict_get_str (globals, "IndexSubjectWidth")) == NULL)
    balsa_app.index_subject_width = SUBJECT_DEFAULT_WIDTH;
  else
    balsa_app.index_subject_width = atoi (field);

  if ((field = pl_dict_get_str (globals, "IndexDateWidth")) == NULL)
    balsa_app.index_date_width = DATE_DEFAULT_WIDTH;
  else
    balsa_app.index_date_width = atoi (field);


  /* FIXME this can be removed later */
  /* PKGW: why comment this out? Breaks my Transfer context menu. */
  if (balsa_app.mblist_width < 100)
      balsa_app.mblist_width = 170;

  if ((field = pl_dict_get_str (globals, "NotebookHeight")) == NULL)
    balsa_app.notebook_height = 170;
  else
    balsa_app.notebook_height = atoi (field);
  /* FIXME this can be removed later */
  /* PKGW see above */
  if (balsa_app.notebook_height < 100)
      balsa_app.notebook_height = 200;



  /* arp --- LeadinStr for "reply to" leadin. */
  g_free (balsa_app.quote_str);
  if ((field = pl_dict_get_str (globals, "LeadinStr")) == NULL)
    balsa_app.quote_str = g_strdup ("> ");
  else
    balsa_app.quote_str = g_strdup (field);

  /* font used to display messages */
  if ((field = pl_dict_get_str (globals, "MessageFont")) == NULL)
    balsa_app.message_font = g_strdup (DEFAULT_MESSAGE_FONT);
  else
    balsa_app.message_font = g_strdup (field);

  /* more here */
  g_free(balsa_app.charset);
  if (( field = pl_dict_get_str (globals, "Charset")) == NULL)
      balsa_app.charset = g_strdup(DEFAULT_CHARSET);
  else
      balsa_app.charset = g_strdup(field);
  mutt_set_charset (balsa_app.charset);

  if (( field = pl_dict_get_str (globals, "EncodingStyle")) == NULL)
      balsa_app.encoding_style = /*DEFAULT_ENCODING*/ 2;
  else
      balsa_app.encoding_style = atoi(field);

  if (( field = pl_dict_get_str (globals, "PrintCommand")) == NULL) 
      balsa_app.PrintCommand.PrintCommand = g_strdup("a2ps -d -q %s");
  else 
      balsa_app.PrintCommand.PrintCommand = g_strdup(field);

  if (( field = pl_dict_get_str (globals, "PrintLinesize")) == NULL)
      balsa_app.PrintCommand.linesize = DEFAULT_LINESIZE;
  else
      balsa_app.PrintCommand.linesize = atoi(field);

  if (( field = pl_dict_get_str (globals, "PrintBreakline")) == NULL )
      balsa_app.PrintCommand.breakline = FALSE;
  else
      balsa_app.PrintCommand.breakline = atoi(field);

  if (( field = pl_dict_get_str (globals, "CheckMailUponStartup")) == NULL )
	  balsa_app.check_mail_upon_startup = FALSE;
  else
	  balsa_app.check_mail_upon_startup = atoi(field);

  if (( field = pl_dict_get_str (globals, "EmptyTrash")) == NULL )
	  balsa_app.empty_trash_on_exit = FALSE;
  else
	  balsa_app.empty_trash_on_exit = atoi(field);

 return TRUE;
}				/* config_global_load */

gint
config_global_save (void)
{
  proplist_t globals, temp_str;
  char tmp[MAX_PROPLIST_KEY_LEN];


  g_assert (balsa_app.proplist != NULL);

  temp_str = PLMakeString ("Globals");
  globals = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  if (globals != NULL)
    {
      /* Out with the old */
      PLRemoveDictionaryEntry (balsa_app.proplist, temp_str);
    }
  PLRelease (temp_str);

  /* Create a new dictionary of global configurations */
  if (balsa_app.address->personal != NULL)
    globals = pl_dict_add_str_str (NULL, "RealName", balsa_app.address->personal);
  if (balsa_app.address->mailbox != NULL)
    pl_dict_add_str_str (globals, "Email", balsa_app.address->mailbox);
  if (balsa_app.replyto != NULL)
    pl_dict_add_str_str (globals, "ReplyTo", balsa_app.replyto);
  if (balsa_app.bcc != NULL)
    pl_dict_add_str_str (globals, "Bcc", balsa_app.bcc);

  if (balsa_app.local_mail_directory != NULL)
    pl_dict_add_str_str (globals, "LocalMailDir",
			 balsa_app.local_mail_directory);
  if (balsa_app.smtp_server != NULL)
    pl_dict_add_str_str (globals, "SMTPServer", balsa_app.smtp_server);

  if (balsa_app.signature_path != NULL)
    pl_dict_add_str_str (globals, "SignaturePath",
			 balsa_app.signature_path);


  {
    snprintf (tmp, sizeof (tmp), "%d", balsa_app.sig_sending);
    pl_dict_add_str_str (globals, "SigSending", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.sig_whenforward);
    pl_dict_add_str_str (globals, "SigForward", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.sig_whenreply);
    pl_dict_add_str_str (globals, "SigReply", tmp);


    snprintf (tmp, sizeof (tmp), "%d", balsa_app.toolbar_style);
    pl_dict_add_str_str (globals, "ToolbarStyle", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.pwindow_option);
    pl_dict_add_str_str (globals, "PWindowOption", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.debug);
    pl_dict_add_str_str (globals, "Debug", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.previewpane);
    pl_dict_add_str_str (globals, "UsePreviewPane", tmp);

    if (balsa_app.smtp) 
    {
      snprintf (tmp, sizeof (tmp), "%d", balsa_app.smtp);
      pl_dict_add_str_str (globals, "SMTP", tmp);
    }

#ifdef BALSA_SHOW_INFO
    snprintf (tmp, sizeof (tmp), "%d", balsa_app.mblist_show_mb_content_info);
    pl_dict_add_str_str (globals, "ShowMailboxContentInfo", tmp);
#endif
    snprintf (tmp, sizeof (tmp), "%d", balsa_app.mw_width);
    pl_dict_add_str_str (globals, "MainWindowWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.mw_height);
    pl_dict_add_str_str (globals, "MainWindowHeight", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.mblist_width);
    pl_dict_add_str_str (globals, "MailboxListWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.notebook_height);
    pl_dict_add_str_str (globals, "NotebookHeight", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.index_num_width);
    pl_dict_add_str_str (globals, "IndexNumWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.index_unread_width);
    pl_dict_add_str_str (globals, "IndexUnreadWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.index_flag_width);
    pl_dict_add_str_str (globals, "IndexFlagWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.index_from_width);
    pl_dict_add_str_str (globals, "IndexFromWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.index_subject_width);
    pl_dict_add_str_str (globals, "IndexSubjectWidth", tmp);
    
    snprintf (tmp, sizeof (tmp), "%d", balsa_app.index_date_width);
    pl_dict_add_str_str (globals, "IndexDateWidth", tmp);
    
    snprintf (tmp, sizeof (tmp), "%d", balsa_app.notebook_height);
    pl_dict_add_str_str (globals, "NotebookHeight", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.encoding_style);
    pl_dict_add_str_str (globals, "EncodingStyle", tmp);
  }

  snprintf (tmp, sizeof (tmp), "%d", balsa_app.check_mail_auto );
  pl_dict_add_str_str (globals, "CheckMailAuto", tmp);

  snprintf (tmp, sizeof (tmp), "%d", balsa_app.check_mail_timer);
  pl_dict_add_str_str (globals, "CheckMailMinutes", tmp);

  snprintf (tmp, sizeof (tmp), "%d", balsa_app.wordwrap );
  pl_dict_add_str_str (globals, "WordWrap", tmp);

  snprintf (tmp, sizeof (tmp), "%d", balsa_app.wraplength);
  pl_dict_add_str_str (globals, "WrapLength", tmp);

  /* arp --- "LeadinStr" into cfg. */
  if (balsa_app.quote_str != NULL)
    pl_dict_add_str_str (globals, "LeadinStr", balsa_app.quote_str);
  else
    pl_dict_add_str_str (globals, "LeadinStr", "> ");

  /* message font */
  if (balsa_app.message_font != NULL)
    pl_dict_add_str_str (globals, "MessageFont", balsa_app.message_font);
  else
    pl_dict_add_str_str (globals, "MessageFont", DEFAULT_MESSAGE_FONT);

  /* encoding */
  if (balsa_app.charset != NULL)
      pl_dict_add_str_str(globals, "Charset", balsa_app.charset);
  else
      pl_dict_add_str_str(globals, "Charset", DEFAULT_CHARSET);


  if (balsa_app.PrintCommand.PrintCommand != NULL)
      pl_dict_add_str_str(globals, "PrintCommand", balsa_app.PrintCommand.PrintCommand);
  else
      pl_dict_add_str_str(globals, "PrintCommand", "a2ps -d -q %s");

  
  snprintf (tmp, sizeof (tmp), "%d", balsa_app.PrintCommand.linesize);
  pl_dict_add_str_str (globals, "PrintLinesize", tmp);
  snprintf (tmp, sizeof (tmp), "%d", balsa_app.PrintCommand.breakline);
  pl_dict_add_str_str (globals, "PrintBreakline", tmp);
  
  snprintf (tmp, sizeof (tmp), "%d", balsa_app.check_mail_upon_startup);
  pl_dict_add_str_str (globals, "CheckMailUponStartup", tmp);

  snprintf ( tmp, sizeof(tmp), "%d", balsa_app.empty_trash_on_exit);
  pl_dict_add_str_str (globals, "EmptyTrash", tmp);

  /* Add it to configuration file */
  temp_str = PLMakeString ("Globals");
  PLInsertDictionaryEntry (balsa_app.proplist, temp_str, globals);
  PLRelease (temp_str);

  return config_save (BALSA_CONFIG_FILE);

}				/* config_global_save */

/* This is a little helper routine which adds a simple entry of the
   form "string1 = string2;" to a dictionary.  If dict_arg is NULL,
   a new dictionary is created and returned. */
static proplist_t
pl_dict_add_str_str (proplist_t dict_arg, gchar * string1, gchar * string2)
{
  proplist_t prop_string1, prop_string2, dict;

  dict = dict_arg;
  prop_string1 = PLMakeString (string1);
  prop_string2 = PLMakeString (string2);

  /* If we are passed a null dictionary, then we are expected to
     create the dictionary from the entry and return it */
  if (dict == NULL)
    dict = PLMakeDictionaryFromEntries (prop_string1, prop_string2, NULL);
  else
    PLInsertDictionaryEntry (dict, prop_string1, prop_string2);

  PLRelease (prop_string1);
  PLRelease (prop_string2);

  return dict;
}				/* pl_dict_add_str_str */

/* A helper routine to get the corresponding value for the string-type
   key 'str' in 'dict'.  Returns the string-value of the corresponding
   value on success and NULL on failure. */
static gchar *
pl_dict_get_str (proplist_t dict, gchar * str)
{
  proplist_t temp_str, elem;

  temp_str = PLMakeString (str);
  elem = PLGetDictionaryEntry (dict, temp_str);
  PLRelease (temp_str);

  if (elem == NULL)
    return NULL;

  if (!PLIsString (elem))
    return NULL;

  return PLGetString (elem);
}				/* pl_dict_get_str */

/* Grab the mailbox whose 'Name' field's string value matches 'name'.
   Returns a handle to the proplist_t for the mailbox if it exists and
   NULL otherwise. */
static proplist_t
config_mailbox_get_by_name (gchar * name)
{
  proplist_t temp_str, accounts, mbox, name_prop, mbox_key;
  int num_elements, i;

  g_assert (balsa_app.proplist != NULL);

  /* Grab the list of accounts */
  temp_str = PLMakeString ("Accounts");
  accounts = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease (temp_str);

  if (accounts == NULL)
    return NULL;

  /* Walk the list of all mailboxes until one with a matching "Name" field
     is found. */
  num_elements = PLGetNumberOfElements (accounts);
  temp_str = PLMakeString ("Name");
  for (i = 0; i < num_elements; i++)
    {
      mbox_key = PLGetArrayElement (accounts, i);
      if (mbox_key == NULL)
	continue;

      mbox = PLGetDictionaryEntry (accounts, mbox_key);
      if (mbox == NULL)
	continue;

      name_prop = PLGetDictionaryEntry (mbox, temp_str);
      if (name_prop == NULL)
	continue;

      if (!strcmp (PLGetString (name_prop), name))
	{
	  PLRelease (temp_str);
	  return mbox;
	}
    }

  PLRelease (temp_str);

  return NULL;
}				/* config_mailbox_get_by_name */

static proplist_t
config_mailbox_get_key (proplist_t mailbox)
{
  proplist_t temp_str, accounts, mbox, mbox_key;
  int num_elements, i;

  g_assert (balsa_app.proplist != NULL);

  /* Grab the list of accounts */
  temp_str = PLMakeString ("Accounts");
  accounts = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease (temp_str);

  if (accounts == NULL)
    return NULL;

  /* Walk the list of all mailboxes until the matching mailbox is found */
  num_elements = PLGetNumberOfElements (accounts);
  for (i = 0; i < num_elements; i++)
    {
      mbox_key = PLGetArrayElement (accounts, i);
      if (mbox_key == NULL)
	continue;

      mbox = PLGetDictionaryEntry (accounts, mbox_key);

      if (mbox == mailbox)
	return mbox_key;

    }

  return NULL;
}				/* config_mailbox_get_key */

static gint
config_mailbox_get_highest_number (proplist_t accounts)
{
  int num_elements, i, max = 0, curr;
  proplist_t mbox_name;
  char *name;

  num_elements = PLGetNumberOfElements (accounts);
  for (i = 0; i < num_elements; i++)
    {
      mbox_name = PLGetArrayElement (accounts, i);

      if (mbox_name == NULL)
	{
	  fprintf (stderr, "config_mailbox_get_highest_number: "
		   "error getting mailbox key!\n");
	  abort ();
	}

      name = PLGetString (mbox_name);

      curr = 0;

      if (strlen (name) > 1)
	curr = atoi (name + 1);

      if (curr > max)
	max = curr;
    }

  return max;
}				/* config_mailbox_get_highest_number */

