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

#include <gnome.h>
#include <proplist.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "balsa-app.h"
#include "misc.h"
#include "save-restore.h"
#include "mailbox-conf.h"
#include "mutt.h"

static proplist_t pl_dict_add_str_str (proplist_t dict_arg, gchar * string1,
				       gchar * string2);
static gchar *pl_dict_get_str (proplist_t dict, gchar * str);
static gchar* config_get_pkey(proplist_t mbox);
static proplist_t config_mailbox_get_by_pkey (const gchar * pkey);
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
  g_assert( pass != NULL );

  len = strlen (pass);
  buff = g_strdup (pass);

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
/* config_mailbox_set_as_special:
   allows to set given mailboxe as one of the special mailboxes
   PS: I am not sure if I should add outbox to the list.
   specialNames must be in sync with the specialType definition.
*/

static gchar * specialNames[] = { "Inbox", "Sentbox", "Trash", "Draftbox" };
void
config_mailbox_set_as_special(Mailbox * mailbox, specialType which)
{
    Mailbox ** special;
    GNode * node;

    g_return_if_fail(mailbox != NULL);

    switch(which) {
    case SPECIAL_INBOX: special = &balsa_app.inbox;    break;
    case SPECIAL_SENT : special = &balsa_app.sentbox;  break;
    case SPECIAL_TRASH: special = &balsa_app.trash;    break;
    case SPECIAL_DRAFT: special = &balsa_app.draftbox; break;
    default : return;
    }
    if(*special) {
	config_mailbox_add(*special, NULL);
	node = g_node_new (mailbox_node_new (
	    (*special)->name, *special, 
	    (*special)->type == MAILBOX_MH));
	g_node_append (balsa_app.mailbox_nodes, node);
    }
    config_mailbox_delete (mailbox);
    config_mailbox_add (mailbox, specialNames[which]);
    
    node = find_gnode_in_mbox_list (balsa_app.mailbox_nodes, mailbox);
    g_node_unlink(node);

    *special = mailbox;
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


static void
add_prop_mailbox(proplist_t mbox_dict, char *key_arg)
{
  proplist_t accounts, temp_str;
  char key[MAX_PROPLIST_KEY_LEN];

  /* Initialize the key in case it is accidentally used uninitialized */
  strcpy (key, "AnErrorOccurred");

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
}

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
  proplist_t mbox_dict;

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

	snprintf (tmp, sizeof (tmp), "%d", MAILBOX_POP3 (mailbox)->server->port);
	pl_dict_add_str_str (mbox_dict, "Port", tmp);

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

  add_prop_mailbox(mbox_dict, key_arg);

  return config_save (BALSA_CONFIG_FILE);
}				/* config_mailbox_add */

/* Remove the specified mailbox from the list of accounts: check
   mailbox_get_pkey and config_get_pkey functions to understand how
   mailboxes are distinguished. Returns TRUE if the mailbox was
   succesfully deleted and FALSE otherwise. */
gint
config_mailbox_delete (const Mailbox * mailbox)
{
  proplist_t accounts, mbox, mbox_key, temp_str;
  gchar * pkey;

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
  pkey = mailbox_get_pkey(mailbox);
  mbox = config_mailbox_get_by_pkey (pkey);
  g_free( pkey );

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
config_mailbox_update (Mailbox * mailbox, const gchar * old_pkey)
{
  proplist_t mbox, mbox_key;

  mbox = config_mailbox_get_by_pkey (old_pkey);

  mbox_key = mbox ? PLGetString (config_mailbox_get_key (mbox)) : NULL;

  config_mailbox_delete (mailbox);
  config_mailbox_add (mailbox, mbox_key);

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

gint 
config_imapdir_add(ImapDir * dir)
{
  proplist_t mbox_dict;
  char tmp[MAX_PROPLIST_KEY_LEN];

  mbox_dict = pl_dict_add_str_str (NULL, "Type", "IMAPDir");
  pl_dict_add_str_str (mbox_dict, "Name",   dir->name);
  pl_dict_add_str_str (mbox_dict, "Server", dir->host);

  /* Add the Port entry */
  snprintf (tmp, sizeof (tmp), "%d", dir->port);
  pl_dict_add_str_str (mbox_dict, "Port",   tmp);

  pl_dict_add_str_str (mbox_dict, "Path",     dir->path);
  pl_dict_add_str_str (mbox_dict, "Username", dir->user);

  if (dir->passwd != NULL)
  {
      gchar *buff;
      buff = rot (dir->passwd);
      pl_dict_add_str_str (mbox_dict, "Password", buff);
      g_free (buff);
  }

  add_prop_mailbox(mbox_dict, NULL);
  return TRUE;
}

static gboolean
get_raw_imap_data(proplist_t mbox, gchar ** username, gchar **passwd, 
		  gchar ** server,  gint * port, gchar **path)
{
    gchar *field;
    if ((field = pl_dict_get_str (mbox, "Username")) == NULL)
	return FALSE;
    else *username = g_strdup(field);

    if( (field = pl_dict_get_str (mbox, "Password")) != NULL)
	*passwd = rot (field);
    else *passwd = NULL;
    if ((field = pl_dict_get_str (mbox, "Server")) == NULL)
	return FALSE;
    else *server = g_strdup(field);
    
    *port = ((field = pl_dict_get_str (mbox, "Port")) == NULL)
	     ? 143 : atol(field);

    if( (field = pl_dict_get_str (mbox, "Path")) == NULL)
	return FALSE;
    else *path = g_strdup(field);

    return TRUE;
}


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
	  mailbox_add_for_checking(mailbox);
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

      if ((field = pl_dict_get_str (mbox, "Port")) == NULL)
	MAILBOX_POP3 (mailbox)->server->port = 110;
      else
	MAILBOX_POP3 (mailbox)->server->port = atol (field);

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
      MailboxIMAP * m;
      mailbox = BALSA_MAILBOX(mailbox_new (MAILBOX_IMAP));
      mailbox->name = mailbox_name;
      m = MAILBOX_IMAP(mailbox);
      if( !get_raw_imap_data(mbox, &m->server->user, 
			     &m->server->passwd, &m->server->host,
			     &m->server->port, &m->path) ){
	  gtk_object_destroy( GTK_OBJECT(mailbox) );
	  return FALSE;
      }
      mailbox_add_for_checking(mailbox);
    }
  else if (!strcasecmp (type, "IMAPDir")) {
      ImapDir * id = imapdir_new();

      if( !get_raw_imap_data(mbox, &id->user, &id->passwd, &id->host,&id->port,
			     &id->path) ){
	  imapdir_destroy(id);
	  return FALSE;
      }
      /* list'em and add'em. The rest is irrelevant. */
      imapdir_scan(id);

      g_node_append (balsa_app.mailbox_nodes, id->file_tree);
      id->file_tree = NULL;
      imapdir_destroy(id);
      return TRUE;
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

  if ((field = pl_dict_get_str (globals, "SigSeparator")) == NULL)
    balsa_app.sig_separator = TRUE;
  else
    balsa_app.sig_separator = atoi ( field );

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

  if ((field = pl_dict_get_str (globals, "BrowseWrap")) == NULL)
    balsa_app.browse_wrap = TRUE;
  else
    balsa_app.browse_wrap = atoi (field);

  if ((field = pl_dict_get_str (globals, "ShownHeaders")) == NULL)
    balsa_app.shown_headers = HEADERS_SELECTED;
  else
    balsa_app.shown_headers = atoi (field);

  g_free(balsa_app.selected_headers);
  if ((field = pl_dict_get_str (globals, "SelectedHeaders")) == NULL)
    balsa_app.selected_headers = g_strdup (DEFAULT_SELECTED_HDRS);
  else
    balsa_app.selected_headers = g_strdup (field);
  g_strdown(balsa_app.selected_headers);

  if ((field = pl_dict_get_str (globals, "ShowMBList")) == NULL)
    balsa_app.show_mblist = TRUE;
  else
    balsa_app.show_mblist = atoi (field);

  if ((field = pl_dict_get_str (globals, "ShowTabs")) == NULL)
    balsa_app.show_notebook_tabs = FALSE;
  else
    balsa_app.show_notebook_tabs = atoi (field);

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

  
  /* column width settings */
  if ((field = pl_dict_get_str (globals, "MBListNameWidth")) == NULL)
    balsa_app.mblist_name_width = MBNAME_DEFAULT_WIDTH;
  else
    balsa_app.mblist_name_width = atoi(field);
  
#ifdef BALSA_SHOW_INFO
  if ((field = pl_dict_get_str (globals, "MBListNewMsgWidth")) == NULL)
    balsa_app.mblist_newmsg_width = NEWMSGCOUNT_DEFAULT_WIDTH;
  else
    balsa_app.mblist_newmsg_width = atoi(field);

  if ((field = pl_dict_get_str (globals, "MBListTotalMsgWidth")) == NULL)
    balsa_app.mblist_totalmsg_width = TOTALMSGCOUNT_DEFAULT_WIDTH;
  else
    balsa_app.mblist_totalmsg_width = atoi(field);

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

  if ((field = pl_dict_get_str (globals, "IndexStatusWidth")) == NULL)
    balsa_app.index_status_width = STATUS_DEFAULT_WIDTH;
  else
    balsa_app.index_status_width = atoi (field);

  if ((field = pl_dict_get_str (globals, "IndexAttachmentWidth")) == NULL)
    balsa_app.index_attachment_width = ATTACHMENT_DEFAULT_WIDTH;
  else
    balsa_app.index_attachment_width = atoi (field);

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

  /* shown headers in the compose window */
  g_free (balsa_app.compose_headers);
  if ((field = pl_dict_get_str (globals, "ComposeHeaders")) == NULL)
     balsa_app.compose_headers = g_strdup("to subject cc");
  else
    balsa_app.compose_headers = g_strdup (field);

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

  if (( field = pl_dict_get_str (globals, "RememberOpenMailboxes")) == NULL )
	  balsa_app.remember_open_mboxes = FALSE;
  else
	  balsa_app.remember_open_mboxes = atoi(field);

  if ( balsa_app.remember_open_mboxes &&
       ( field = pl_dict_get_str (globals, "OpenMailboxes")) != NULL &&
      strlen(field)>0 ) {
      if(balsa_app.open_mailbox) {
	  gchar * str = g_strconcat(balsa_app.open_mailbox, ";", field,NULL);
	  g_free(balsa_app.open_mailbox);
	  balsa_app.open_mailbox = str;
      } else balsa_app.open_mailbox = g_strdup(field);
  }

  if (( field = pl_dict_get_str (globals, "EmptyTrash")) == NULL )
	  balsa_app.empty_trash_on_exit = FALSE;
  else
	  balsa_app.empty_trash_on_exit = atoi(field);

  /* Here we load the unread mailbox colour for the mailbox list */
  if ((field = pl_dict_get_str (globals, "MBListUnreadColorRed")) == NULL)
    balsa_app.mblist_unread_color.red = MBLIST_UNREAD_COLOR_RED;
  else
    balsa_app.mblist_unread_color.red = atoi (field);

  if ((field = pl_dict_get_str (globals, "MBListUnreadColorGreen")) == NULL)
    balsa_app.mblist_unread_color.green = MBLIST_UNREAD_COLOR_GREEN;
  else
    balsa_app.mblist_unread_color.green = atoi (field);

  if ((field = pl_dict_get_str (globals, "MBListUnreadColorBlue")) == NULL)
    balsa_app.mblist_unread_color.blue = MBLIST_UNREAD_COLOR_BLUE;
  else
    balsa_app.mblist_unread_color.blue = atoi (field);


  /* address book location */
  if ((field = pl_dict_get_str (globals, "AddressBookDistMode")) != NULL)
    balsa_app.ab_dist_list_mode = atoi (field);

  if ((field = pl_dict_get_str (globals, "AddressBookLocation")) != NULL) {
    g_free (balsa_app.ab_location);
    balsa_app.ab_location = g_strdup (field);
  }

  /* How we format dates */
  if ((field = pl_dict_get_str (globals, "DateFormat")) != NULL) {
    g_free (balsa_app.date_string);
    balsa_app.date_string = g_strdup (field);
  }

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

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.sig_separator);
    pl_dict_add_str_str (globals, "SigSeparator", tmp);


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

    snprintf (tmp, sizeof(tmp), "%d", balsa_app.mblist_name_width);
    pl_dict_add_str_str (globals, "MBListNameWidth", tmp);

#ifdef BALSA_SHOW_INFO
    snprintf (tmp, sizeof(tmp), "%d", balsa_app.mblist_newmsg_width);
    pl_dict_add_str_str (globals, "MBListNewMsgWidth", tmp);

    snprintf (tmp, sizeof(tmp), "%d", balsa_app.mblist_totalmsg_width);
    pl_dict_add_str_str (globals, "MBListTotalMsgWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.mblist_show_mb_content_info);
    pl_dict_add_str_str (globals, "ShowMailboxContentInfo", tmp);
#endif
    snprintf (tmp, sizeof (tmp), "%d", balsa_app.mw_width);
    pl_dict_add_str_str (globals, "MainWindowWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.mw_height);
    pl_dict_add_str_str (globals, "MainWindowHeight", tmp);

/* We need to add 18 to the mailbox list width to prevent it from changing
   after the exit (not sure why but possibly because of difference between
   paned width and the ctree width. */
    snprintf (tmp, sizeof (tmp), "%d", balsa_app.mblist_width);
    pl_dict_add_str_str (globals, "MailboxListWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.notebook_height);
    pl_dict_add_str_str (globals, "NotebookHeight", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.index_num_width);
    pl_dict_add_str_str (globals, "IndexNumWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.index_status_width);
    pl_dict_add_str_str (globals, "IndexStatusWidth", tmp);

    snprintf (tmp, sizeof (tmp), "%d", balsa_app.index_attachment_width);
    pl_dict_add_str_str (globals, "IndexAttachmentWidth", tmp);

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

  snprintf (tmp, sizeof (tmp), "%d", balsa_app.browse_wrap );
  pl_dict_add_str_str (globals, "BrowseWrap", tmp);

  snprintf (tmp, sizeof (tmp), "%d", balsa_app.shown_headers );
  pl_dict_add_str_str (globals, "ShownHeaders", tmp);

  if(balsa_app.selected_headers)
     pl_dict_add_str_str (globals, "SelectedHeaders", 
			  balsa_app.selected_headers);
  else pl_dict_add_str_str (globals, "SelectedHeaders", DEFAULT_SELECTED_HDRS);

  snprintf (tmp, sizeof (tmp), "%d", balsa_app.show_mblist );
  pl_dict_add_str_str (globals, "ShowMBList", tmp);

  snprintf (tmp, sizeof (tmp), "%d", balsa_app.show_notebook_tabs );
  pl_dict_add_str_str (globals, "ShowTabs", tmp);

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

  if (balsa_app.compose_headers != NULL)
     pl_dict_add_str_str (globals, "ComposeHeaders",balsa_app.compose_headers);

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

  if( balsa_app.open_mailbox != NULL )
	  pl_dict_add_str_str (globals, "OpenMailboxes", balsa_app.open_mailbox);

  snprintf (tmp, sizeof (tmp), "%d", balsa_app.remember_open_mboxes);
  pl_dict_add_str_str (globals, "RememberOpenMailboxes", tmp);

  snprintf ( tmp, sizeof(tmp), "%d", balsa_app.empty_trash_on_exit);
  pl_dict_add_str_str (globals, "EmptyTrash", tmp);

  snprintf (tmp, sizeof (tmp), "%hd", balsa_app.mblist_unread_color.red);
  pl_dict_add_str_str (globals, "MBListUnreadColorRed", tmp);
  
  snprintf (tmp, sizeof (tmp), "%hd", balsa_app.mblist_unread_color.green);
  pl_dict_add_str_str (globals, "MBListUnreadColorGreen", tmp);

  snprintf (tmp, sizeof (tmp), "%hd", balsa_app.mblist_unread_color.blue);
  pl_dict_add_str_str (globals, "MBListUnreadColorBlue", tmp);

  /* address book */
  snprintf (tmp, sizeof (tmp), "%d", balsa_app.ab_dist_list_mode);
  pl_dict_add_str_str (globals, "AddressBookDistMode", tmp);

  if (balsa_app.signature_path != NULL)
    pl_dict_add_str_str (globals, "AddressBookLocation", 
			 balsa_app.ab_location);

  if( balsa_app.date_string )
	  pl_dict_add_str_str (globals, "DateFormat", balsa_app.date_string );

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

/* mailbox_get_pkey:
   returns a string - primary key (pkey) identifing the mailbox.
   note that the mailbox name does not fulfill pkey conditions.
   FIXME: the present implementation leaves room for improvement.
*/
gchar*
mailbox_get_pkey(const Mailbox * mailbox)
{
    gchar *pkey;

    g_assert(mailbox != NULL);

    switch(mailbox->type) {
    case MAILBOX_POP3:
	    pkey = g_strdup_printf( "%s %d", MAILBOX_POP3 (mailbox)->server->host, MAILBOX_POP3(mailbox)->server->port ); 
	    break;
    case MAILBOX_MBOX:
    case MAILBOX_MAILDIR:
    case MAILBOX_MH:
	    pkey = g_strdup( MAILBOX_LOCAL (mailbox)->path );
	    break;
    case MAILBOX_IMAP:
	    pkey = g_strdup_printf( "%s %d %s", MAILBOX_IMAP (mailbox)->server->host, MAILBOX_IMAP (mailbox)->server->port, MAILBOX_IMAP (mailbox)->path );
	    break;
    default: /* MAILBOX_UNKNOWN */
	    pkey = g_strdup( mailbox->name );
    }

    return pkey;
}

static gchar*
config_get_pkey(proplist_t mbox)
{
	gchar *type;
	gchar *result;

	type = pl_dict_get_str (mbox, "Type");
	
	if (strcasecmp (type, "local") == 0)
		result = g_strdup( pl_dict_get_str(mbox, "Path") );
	else if(strcasecmp(type,"POP3") == 0)
		result = g_strdup_printf( "%s %s", pl_dict_get_str(mbox, "Server"), pl_dict_get_str( mbox, "Port" ) );
	else if(g_strcasecmp(type,"IMAP") == 0)
		result = g_strdup_printf( "%s %s %s", pl_dict_get_str(mbox, "Server"), pl_dict_get_str( mbox, "Port" ), pl_dict_get_str(mbox, "Path") );
	else 
		result = NULL;

	return result;
}

static proplist_t
config_mailbox_get_by_pkey (const gchar * pkey)
{
  proplist_t temp_str, accounts, mbox, mbox_key;
  int num_elements, i;
  gchar * key;

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
  for (i = 0; i < num_elements; i++)
    {
      mbox_key = PLGetArrayElement (accounts, i);
      if (mbox_key == NULL)
	continue;

      mbox = PLGetDictionaryEntry (accounts, mbox_key);
      if (mbox == NULL)
	continue;

      key = config_get_pkey(mbox);

      if (key && strcmp (key, pkey) == 0) {
	      g_free( key );
	  return mbox;
      }

      g_free( key );

    }

  return NULL;
}				/* config_mailbox_get_by_pkey */

#if 0
/* section disabled on 2000.05.05 */
/* Grab the mailbox whose 'Name' field's string value matches 'name'.
   Returns a handle to the proplist_t for the mailbox if it exists and
   NULL otherwise. */
static proplist_t
config_mailbox_get_by_name (const gchar * name)
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
#endif

static proplist_t
config_mailbox_get_key (proplist_t mailbox)
{
  proplist_t temp_str, accounts, mbox, mbox_key;
  int num_elements, i;

  g_return_val_if_fail (mailbox != NULL, NULL);
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

      if ( PLIsEqual( mbox, mailbox ) )
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

