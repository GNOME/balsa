/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
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
#include "quote-color.h"

static void pl_dict_add_remote (proplist_t dict_arg, 
				const LibBalsaServer * server);
static proplist_t d_add_gint(proplist_t dict_arg, gchar * string1, 
				       gint iarg);
static proplist_t pl_dict_add_str_str (proplist_t dict_arg, gchar * string1,
				       gchar * string2);
static gchar *pl_dict_get_str (proplist_t dict, gchar * str);
static gint d_get_gint (proplist_t dict, gchar * key, gint def_val);
static gchar* config_get_pkey(proplist_t mbox);
static proplist_t config_mailbox_get_key_by_pkey (const gchar * pkey);
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
config_mailbox_set_as_special(LibBalsaMailbox * mailbox, specialType which)
{
    LibBalsaMailbox **special;
    GNode * node;

    g_return_if_fail(mailbox != NULL);

    switch(which) {
    case SPECIAL_INBOX: special = &balsa_app.inbox;    break;
    case SPECIAL_SENT : special = &balsa_app.sentbox;  break;
    case SPECIAL_TRASH: special = &balsa_app.trash;    break;
    case SPECIAL_DRAFT: special = &balsa_app.draftbox; break;
    default : return;
    }
    if(*special) 
    {
      config_mailbox_add(*special, NULL);
      node = g_node_new (mailbox_node_new (
	(*special)->name, *special, 
	(*special)->is_directory));
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
add_prop_mailbox(proplist_t mbox_dict, const char *key_arg)
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
 * mailbox.  If key is NULL, the mailbox's key is determined
 * automatically.  The function returns TRUE on success and FALSE on
 * failure.
 */
gint
config_mailbox_add (LibBalsaMailbox * mailbox, const char *key_arg)
{
  proplist_t mbox_dict;
  
  /* Each mailbox is stored as a Proplist dictionary of mailbox settings.
     First create the dictionary, then add it to the "accounts" section
     of the global configuration. */
  if ( LIBBALSA_IS_MAILBOX_LOCAL (mailbox) )
  {
    /* Create a new mailbox configuration with the following entries:
       Type = local;
       Name = ... ;
       Path = ... ;
    */
    mbox_dict = pl_dict_add_str_str (NULL, "Type", "local");
    pl_dict_add_str_str (mbox_dict, "Name", mailbox->name);
    pl_dict_add_str_str (mbox_dict, "Path", LIBBALSA_MAILBOX_LOCAL (mailbox)->path);
  }
  else if ( LIBBALSA_IS_MAILBOX_POP3 (mailbox) )
  {
    /* Create a new mailbox configuration with the following entries:
       Type = POP3;
       Name = ...;
       Username = ...;
       Password = ...;
       Server = ...;
       Check = 0 | 1;
       Delete =  0 | 1;
       Apop = 0 | 1;
    */
    mbox_dict = pl_dict_add_str_str (NULL, "Type", "POP3");
    pl_dict_add_str_str (mbox_dict, "Name", mailbox->name);
    pl_dict_add_remote  (mbox_dict, LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));
    d_add_gint (mbox_dict, "Check", LIBBALSA_MAILBOX_POP3(mailbox)->check); 
    d_add_gint (mbox_dict, "Delete",
		LIBBALSA_MAILBOX_POP3(mailbox)->delete_from_server);
    d_add_gint (mbox_dict, "Apop", LIBBALSA_MAILBOX_POP3(mailbox)->use_apop);
    
    if ((LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid) != NULL)
      pl_dict_add_str_str (mbox_dict, "LastUID", LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid);
  }
  else if ( LIBBALSA_IS_MAILBOX_IMAP(mailbox) )
  {
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
    pl_dict_add_remote  (mbox_dict, LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));
    pl_dict_add_str_str (mbox_dict, "Path", 
			 LIBBALSA_MAILBOX_IMAP (mailbox)->path);
  }
  else
  {
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
config_mailbox_delete (const LibBalsaMailbox * mailbox)
{
  proplist_t accounts, mbox_key, temp_str;
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

  /* Grab the specified mailbox's key */
  pkey = mailbox_get_pkey(mailbox);
  mbox_key = config_mailbox_get_key_by_pkey (pkey);
  g_free( pkey );

  if (mbox_key == NULL)
    return FALSE;

  accounts = PLRemoveDictionaryEntry (accounts, mbox_key);

  config_save (BALSA_CONFIG_FILE);
  return TRUE;
}				/* config_mailbox_delete */

/* Update the configuration information for the specified mailbox. */
gint
config_mailbox_update (LibBalsaMailbox * mailbox, const gchar * old_pkey)
{
  proplist_t prop_key;
  gchar * mbox_key;

  prop_key = config_mailbox_get_key_by_pkey (old_pkey);

  g_assert(PLIsString(prop_key));
  mbox_key = prop_key ? g_strdup(PLGetString(prop_key)) : NULL;
  config_mailbox_delete (mailbox);
  config_mailbox_add (mailbox, mbox_key);
  g_free(mbox_key);
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
    *port = d_get_gint (mbox, "Port", 143);

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
  LibBalsaMailboxType mailbox_type;
  LibBalsaMailbox *mailbox;
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

      mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_local_new(path, FALSE));

      if ( mailbox == NULL ) {
	fprintf (stderr, "config_mailbox_init: Cannot create "
		   "local mailbox %s\n", mailbox_name);
	return FALSE;
      }
      mailbox->name = mailbox_name;

    }
  else if (!strcasecmp (type, "POP3"))	/* POP3 mailbox */
    {
      LibBalsaServer *server;
      mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_pop3_new ());
      mailbox->name = mailbox_name;
      server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
      gtk_signal_connect(GTK_OBJECT(server), "get-password", 
			 GTK_SIGNAL_FUNC(ask_password), mailbox);

      if ((field = pl_dict_get_str (mbox, "Username")) == NULL)
	return FALSE;
      libbalsa_server_set_username(server, field);

      field = pl_dict_get_str (mbox, "Password");
      if ( field != NULL )
      {
	gchar *buff ;
	buff = rot (field);
	libbalsa_server_set_password (server, buff);
	g_free (buff);
      }
      else
	libbalsa_server_set_password (server, NULL);

      field = pl_dict_get_str (mbox, "Server");
      if (field == NULL) 
	return FALSE;
      else
	libbalsa_server_set_host (server, field, 
				  d_get_gint (mbox, "Port", 110));

      LIBBALSA_MAILBOX_POP3 (mailbox)->check = d_get_gint (mbox,"Check",FALSE);
      LIBBALSA_MAILBOX_POP3 (mailbox)->delete_from_server =
	d_get_gint (mbox, "Delete", FALSE);

      if ((field = pl_dict_get_str (mbox, "LastUID")) == NULL)
	LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid = NULL;
      else
	LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid = g_strdup (field);

      LIBBALSA_MAILBOX_POP3 (mailbox)->use_apop =
	d_get_gint (mbox, "Apop", FALSE);

      balsa_app.inbox_input =
	g_list_append (balsa_app.inbox_input, mailbox);

      return TRUE; /* Don't put POP mailbox in mailbox nodes */
    }
  else if (!strcasecmp (type, "IMAP"))	/* IMAP Mailbox */
    {
      LibBalsaMailboxImap * m;
      LibBalsaServer *s;
      gchar *user, *passwd, *host, *path;
      gint port;

      mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_imap_new());
      mailbox->name = mailbox_name;


      m = LIBBALSA_MAILBOX_IMAP(mailbox);
      if( !get_raw_imap_data(mbox, &user, 
			     &passwd, &host,
			     &port, &path) ){
	gtk_object_destroy( GTK_OBJECT(mailbox) );
	return FALSE;
      }

      s = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
      gtk_signal_connect(GTK_OBJECT(s), "get-password", 
			 GTK_SIGNAL_FUNC(ask_password), m);
      libbalsa_server_set_username (s, user);
      libbalsa_server_set_password (s, passwd);
      libbalsa_server_set_host (s, host, port);
      libbalsa_mailbox_imap_set_path(m, path);
      
      g_free(user);
      g_free(passwd);
      g_free(host);
      g_free(path);
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
      balsa_app.inbox = mailbox;
  else if (strcmp ("Outbox",   key) == 0)
      balsa_app.outbox = mailbox;
  else if (strcmp ("Sentbox",  key) == 0)
      balsa_app.sentbox = mailbox;
  else if (strcmp ("Draftbox", key) == 0)
      balsa_app.draftbox = mailbox;
  else if (strcmp ("Trash",    key) == 0)
      balsa_app.trash = mailbox;
  else
    {
      node = g_node_new (mailbox_node_new (g_strdup (mailbox->name),
					   mailbox, 
					   mailbox_type == MAILBOX_MH));
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

  /* users's domain */
  g_free (balsa_app.domain);
  if ((field = pl_dict_get_str (globals, "Domain")) == NULL)
    balsa_app.domain = g_strdup ("");
  else
    balsa_app.domain = g_strdup (field);

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

  balsa_app.sig_sending     = d_get_gint (globals, "SigSending",   TRUE);
  balsa_app.sig_whenreply   = d_get_gint (globals, "SigReply",     TRUE);
  balsa_app.sig_whenforward = d_get_gint (globals, "SigForward",   TRUE);
  balsa_app.sig_separator   = d_get_gint (globals, "SigSeparator", TRUE);

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
  balsa_app.check_mail_auto =  d_get_gint (globals, "CheckMailAuto", FALSE);
  balsa_app.check_mail_timer = d_get_gint (globals, "CheckMailMinutes", 10);

  if (balsa_app.check_mail_timer < 1 )
    balsa_app.check_mail_timer = 10;

  if( balsa_app.check_mail_auto )
    update_timer(TRUE, balsa_app.check_mail_timer );

  /* Word Wrap */
  balsa_app.wordwrap   = d_get_gint (globals, "WordWrap",TRUE);
  balsa_app.wraplength = d_get_gint (globals, "WrapLength",75);

  if (balsa_app.wraplength < 40 )
    balsa_app.wraplength = 40;

  balsa_app.browse_wrap = d_get_gint (globals, "BrowseWrap",TRUE);

  balsa_app.shown_headers = d_get_gint (globals, "ShownHeaders",
					HEADERS_SELECTED);

  g_free(balsa_app.selected_headers);
  if ((field = pl_dict_get_str (globals, "SelectedHeaders")) == NULL)
    balsa_app.selected_headers = g_strdup (DEFAULT_SELECTED_HDRS);
  else
    balsa_app.selected_headers = g_strdup (field);
  g_strdown(balsa_app.selected_headers);

  balsa_app.show_mblist        = d_get_gint (globals, "ShowMBList",TRUE);
  balsa_app.show_notebook_tabs = d_get_gint (globals, "ShowTabs",FALSE);

  /* toolbar style */
  balsa_app.toolbar_style =d_get_gint(globals,"ToolbarStyle",GTK_TOOLBAR_BOTH);

  /* Progress Window Dialog */
  balsa_app.pwindow_option = d_get_gint (globals, "PWindowOption",WHILERETR);

  /* use the preview pane */
  balsa_app.previewpane = d_get_gint (globals, "UsePreviewPane",TRUE);
  
  /* column width settings */
  balsa_app.mblist_name_width = 
    d_get_gint (globals, "MBListNameWidth",MBNAME_DEFAULT_WIDTH);
  
  balsa_app.mblist_newmsg_width = d_get_gint (globals, "MBListNewMsgWidth",NEWMSGCOUNT_DEFAULT_WIDTH);
  balsa_app.mblist_totalmsg_width = d_get_gint (globals, "MBListTotalMsgWidth",TOTALMSGCOUNT_DEFAULT_WIDTH);

  /* show mailbox content info */
  balsa_app.mblist_show_mb_content_info = d_get_gint (globals, "ShowMailboxContentInfo",TRUE);

  /* debugging enabled */
  balsa_app.debug = d_get_gint (globals, "Debug",FALSE);

  /* window sizes */
  balsa_app.mw_width     = d_get_gint (globals, "MainWindowWidth",640);
  balsa_app.mw_height    = d_get_gint (globals, "MainWindowHeight",480);
  balsa_app.mblist_width = d_get_gint (globals, "MailboxListWidth",100);

  /* restore column sizes from previous session */
  balsa_app.index_num_width = d_get_gint (globals, "IndexNumWidth",NUM_DEFAULT_WIDTH);
  balsa_app.index_status_width = d_get_gint (globals, "IndexStatusWidth",STATUS_DEFAULT_WIDTH);
  balsa_app.index_attachment_width = d_get_gint (globals, "IndexAttachmentWidth",ATTACHMENT_DEFAULT_WIDTH);
  balsa_app.index_from_width = d_get_gint (globals, "IndexFromWidth",FROM_DEFAULT_WIDTH);
  balsa_app.index_subject_width = d_get_gint (globals, "IndexSubjectWidth",SUBJECT_DEFAULT_WIDTH);
  balsa_app.index_date_width = d_get_gint (globals, "IndexDateWidth",DATE_DEFAULT_WIDTH);

  /* PKGW: why comment this out? Breaks my Transfer context menu. */
  if (balsa_app.mblist_width < 100)
      balsa_app.mblist_width = 170;

  balsa_app.notebook_height = d_get_gint (globals, "NotebookHeight",170);
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
  libbalsa_set_charset (balsa_app.charset);

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

  balsa_app.PrintCommand.linesize = 
    d_get_gint (globals, "PrintLinesize",DEFAULT_LINESIZE);
  balsa_app.PrintCommand.breakline = 
    d_get_gint (globals, "PrintBreakline",FALSE);
  balsa_app.check_mail_upon_startup = 
    d_get_gint (globals, "CheckMailUponStartup",FALSE);
  balsa_app.remember_open_mboxes = 
    d_get_gint (globals, "RememberOpenMailboxes",FALSE);

  if ( balsa_app.remember_open_mboxes &&
       ( field = pl_dict_get_str (globals, "OpenMailboxes")) != NULL &&
      strlen(field)>0 ) {
      if(balsa_app.open_mailbox) {
	  gchar * str = g_strconcat(balsa_app.open_mailbox, ";", field,NULL);
	  g_free(balsa_app.open_mailbox);
	  balsa_app.open_mailbox = str;
      } else balsa_app.open_mailbox = g_strdup(field);
  }

  balsa_app.empty_trash_on_exit = d_get_gint (globals, "EmptyTrash",FALSE);

  /* Here we load the unread mailbox colour for the mailbox list */
  balsa_app.mblist_unread_color.red = d_get_gint (globals, "MBListUnreadColorRed",MBLIST_UNREAD_COLOR_RED);

  balsa_app.mblist_unread_color.green = d_get_gint (globals, "MBListUnreadColorGreen",MBLIST_UNREAD_COLOR_GREEN);

  balsa_app.mblist_unread_color.blue = d_get_gint (globals, "MBListUnreadColorBlue",MBLIST_UNREAD_COLOR_BLUE);


  /*
   * Here we load the quoted text colour for the mailbox list.
   * We load two colours, and recalculate the gradient.
   */
  balsa_app.quoted_color[0].red = d_get_gint (globals, "QuotedColorStartRed",QUOTED_COLOR_RED);
  balsa_app.quoted_color[0].green = d_get_gint (globals, "QuotedColorStartGreen",QUOTED_COLOR_GREEN);
  balsa_app.quoted_color[0].blue = d_get_gint (globals, "QuotedColorStartBlue",QUOTED_COLOR_BLUE);

  balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].red = d_get_gint (globals, "QuotedColorEndRed",QUOTED_COLOR_RED);
  balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].green = d_get_gint (globals, "QuotedColorEndGreen",QUOTED_COLOR_GREEN);
  balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].blue = d_get_gint (globals, "QuotedColorEndBlue",QUOTED_COLOR_BLUE);

  make_gradient (balsa_app.quoted_color, 0, MAX_QUOTED_COLOR - 1);


  /* address book location */
  if ((field = pl_dict_get_str (globals, "AddressBookDistMode")) != NULL)
    balsa_app.ab_dist_list_mode = atoi (field);

  if ((field = pl_dict_get_str (globals, "AddressBookLocation")) != NULL) {
    g_free (balsa_app.ab_location);
    balsa_app.ab_location = g_strdup (field);
  }

  balsa_app.alias_find_flag = d_get_gint (globals, "AliasFlag",FALSE);
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
  if (balsa_app.domain != NULL)
    pl_dict_add_str_str (globals, "Domain", balsa_app.domain);
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

  d_add_gint (globals, "SigSending", balsa_app.sig_sending);
  d_add_gint (globals, "SigForward", balsa_app.sig_whenforward);
  d_add_gint (globals, "SigReply",   balsa_app.sig_whenreply);
  d_add_gint (globals, "SigSeparator", balsa_app.sig_separator);

  d_add_gint (globals, "ToolbarStyle", balsa_app.toolbar_style);
  d_add_gint (globals, "PWindowOption", balsa_app.pwindow_option);
  d_add_gint (globals, "Debug", balsa_app.debug);
  d_add_gint (globals, "UsePreviewPane", balsa_app.previewpane);

  if (balsa_app.smtp) 
    d_add_gint (globals, "SMTP", balsa_app.smtp);
  
  d_add_gint (globals, "MBListNameWidth",   balsa_app.mblist_name_width);
  d_add_gint (globals, "MBListNewMsgWidth", balsa_app.mblist_newmsg_width);
  d_add_gint (globals,"MBListTotalMsgWidth",balsa_app.mblist_totalmsg_width);
  d_add_gint (globals, "ShowMailboxContentInfo", 
	      balsa_app.mblist_show_mb_content_info);
  
  d_add_gint (globals, "MainWindowWidth",  balsa_app.mw_width);
  d_add_gint (globals, "MainWindowHeight", balsa_app.mw_height);
  
  d_add_gint (globals, "MailboxListWidth", balsa_app.mblist_width);
  d_add_gint (globals, "NotebookHeight",   balsa_app.notebook_height);
  d_add_gint (globals, "IndexNumWidth",    balsa_app.index_num_width);
  d_add_gint (globals, "IndexStatusWidth", balsa_app.index_status_width);
  d_add_gint (globals, "IndexAttachmentWidth", 
	      balsa_app.index_attachment_width);
  
  d_add_gint (globals, "IndexFromWidth",    balsa_app.index_from_width);
  d_add_gint (globals, "IndexSubjectWidth", balsa_app.index_subject_width);
  d_add_gint (globals, "IndexDateWidth",    balsa_app.index_date_width);
  d_add_gint (globals, "EncodingStyle",     balsa_app.encoding_style);
  d_add_gint (globals, "CheckMailAuto",     balsa_app.check_mail_auto);
  d_add_gint (globals, "CheckMailMinutes",  balsa_app.check_mail_timer);
  d_add_gint (globals, "WordWrap",          balsa_app.wordwrap );
  d_add_gint (globals, "WrapLength",        balsa_app.wraplength);
  d_add_gint (globals, "BrowseWrap",        balsa_app.browse_wrap );
  d_add_gint (globals, "ShownHeaders",      balsa_app.shown_headers );

  if(balsa_app.selected_headers)
     pl_dict_add_str_str (globals, "SelectedHeaders", 
			  balsa_app.selected_headers);
  else pl_dict_add_str_str (globals, "SelectedHeaders", DEFAULT_SELECTED_HDRS);

  d_add_gint (globals, "ShowMBList", balsa_app.show_mblist );
  d_add_gint (globals, "ShowTabs", balsa_app.show_notebook_tabs );

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

  d_add_gint (globals, "PrintLinesize",  balsa_app.PrintCommand.linesize);
  d_add_gint (globals, "PrintBreakline", balsa_app.PrintCommand.breakline);
  d_add_gint (globals, "CheckMailUponStartup",
	      balsa_app.check_mail_upon_startup);

  if( balsa_app.open_mailbox != NULL )
	  pl_dict_add_str_str (globals, "OpenMailboxes", balsa_app.open_mailbox);

  d_add_gint (globals, "RememberOpenMailboxes",
			balsa_app.remember_open_mboxes); 
  d_add_gint (globals, "EmptyTrash", balsa_app.empty_trash_on_exit);
  
  d_add_gint (globals, "MBListUnreadColorR", 
	      balsa_app.mblist_unread_color.red);
  d_add_gint (globals, "MBListUnreadColorG", 
	      balsa_app.mblist_unread_color.green);
  d_add_gint (globals, "MBListUnreadColorB", 
	      balsa_app.mblist_unread_color.blue);

  /*
   * Quoted color - we only save the first and last, and recalculate
   * the gradient when Balsa starts.
   */
  d_add_gint(globals, "QuotedColorStartRed",  balsa_app.quoted_color[0].red);
  d_add_gint(globals, "QuotedColorStartGreen",balsa_app.quoted_color[0].green);
  d_add_gint(globals, "QuotedColorStartBlue", balsa_app.quoted_color[0].blue);
  d_add_gint (globals, "QuotedColorEndRed", 
	     balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].red);
  d_add_gint (globals, "QuotedColorEndGreen", 
	     balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].green);
  d_add_gint (globals, "QuotedColorEndBlue", 
	      balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].blue);
  make_gradient (balsa_app.quoted_color, 0, MAX_QUOTED_COLOR - 1);

  /* address book */
  d_add_gint (globals, "AddressBookDistMode", balsa_app.ab_dist_list_mode);

  if (balsa_app.signature_path != NULL)
    pl_dict_add_str_str (globals, "AddressBookLocation", 
			 balsa_app.ab_location);
  d_add_gint (globals, "AliasFlag", balsa_app.alias_find_flag);

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

static void
pl_dict_add_remote (proplist_t dict_arg, const LibBalsaServer * server) 
{
  pl_dict_add_str_str (dict_arg, "Server",   server->host);
  d_add_gint(dict_arg, "Port",     server->port);
  pl_dict_add_str_str (dict_arg, "Username", server->user);
  
  if (server->passwd != NULL) {
    gchar *buff;
    buff = rot (server->passwd);
    pl_dict_add_str_str (dict_arg, "Password", buff);
    g_free (buff);
  }
}

static proplist_t
d_add_gint (proplist_t dict_arg, gchar * string1, gint iarg) 
{
  char tmp[MAX_PROPLIST_KEY_LEN];
  snprintf (tmp, sizeof (tmp), "%d", iarg);
  return pl_dict_add_str_str (dict_arg, string1, tmp);
}

static gint 
d_get_gint (proplist_t dict, gchar * key, gint def_val)
{
  gchar * field = pl_dict_get_str (dict, key);
  return field ? atoi(field) : def_val;
}

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
mailbox_get_pkey(const LibBalsaMailbox * mailbox)
{
    gchar *pkey;

    g_assert(mailbox != NULL);

    if ( LIBBALSA_IS_MAILBOX_POP3 (mailbox) )
    {
	    pkey = g_strdup_printf( "%s %d", LIBBALSA_MAILBOX_REMOTE_SERVER (mailbox)->host, 
				    LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox)->port ); 
    }
    else if ( LIBBALSA_IS_MAILBOX_LOCAL(mailbox) )
    {
      
      pkey = g_strdup( LIBBALSA_MAILBOX_LOCAL (mailbox)->path );
    }
    else if ( LIBBALSA_IS_MAILBOX_IMAP (mailbox) )
    {
      pkey = g_strdup_printf( "%s %d %s", LIBBALSA_MAILBOX_REMOTE_SERVER (mailbox)->host, 
			      LIBBALSA_MAILBOX_REMOTE_SERVER (mailbox)->port, LIBBALSA_MAILBOX_IMAP (mailbox)->path );
    } 
    else
    {
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
config_mailbox_get_key_by_pkey (const gchar * pkey)
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
	  return mbox_key;
      }

      g_free( key );

    }

  return NULL;
}				/* config_mailbox_get_key_by_pkey */

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

