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
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include "balsa-app.h"
#include "save-restore.h"
#include "quote-color.h"

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))
static gchar * MboxSectionPrefix = "mailbox-";

static gint config_mailboxes_init (void);
static gint config_global_load (void);
static gint config_mailbox_init (const gchar * key);
static gchar* config_mailbox_get_free_pkey (void);

gint
config_load(void) {
   config_mailboxes_init ();
   config_global_load ();
   return TRUE;
}

static gint d_get_gint (const gchar * key, gint def_val)
{
  gint def;
  gint res = gnome_config_private_get_int_with_default(key, &def);
  return def ? def_val : res;
}

static gchar* d_get_string (const gchar * key, const gchar * def_val)
{
  gint def;
  gchar* res = gnome_config_private_get_string_with_default(key, &def);
  return def ? g_strdup(def_val) : res;
}

#define d_set_gint(key, val) gnome_config_private_set_int((key), (val))
#define mailbox_section_path(mbox)\
    g_strconcat("balsa/", MboxSectionPrefix, (mbox)->pkey, "/",NULL);

/* config_mailbox_set_as_special:
   allows to set given mailboxe as one of the special mailboxes
   PS: I am not sure if I should add outbox to the list.
   specialNames must be in sync with the specialType definition.
*/
static gchar * specialNames[] = { 
  "Inbox", "Sentbox", "Trash", "Draftbox", "Outbox" 
};
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
    if(*special) {
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

/* config_mailbox_add:
   adds the specifed mailbox to the configuration. If the mailbox does
   not have the unique pkey assigned, find one.
*/
gint
config_mailbox_add (LibBalsaMailbox * mailbox, const char *key_arg)
{
  gchar * tmp;
  g_return_val_if_fail(mailbox, FALSE);

  g_free(mailbox->pkey); /* be paranoid memory leak enemy */
  mailbox->pkey = key_arg ? g_strdup(key_arg) : config_mailbox_get_free_pkey();
  tmp = mailbox_section_path(mailbox);
  gnome_config_push_prefix(tmp); 
  libbalsa_mailbox_save_config(mailbox);
  g_free(tmp);
  gnome_config_pop_prefix();
  gnome_config_sync();
  return TRUE;
}				/* config_mailbox_add */

/* removes from the ocnfiguration only */
gint
config_mailbox_delete (const LibBalsaMailbox * mailbox)
{
  gchar * tmp; /* the key in the mailbox section name */
  gint res;
  tmp = mailbox_section_path(mailbox);
  res = gnome_config_private_has_section(tmp);
  gnome_config_private_clean_section(tmp);
  gnome_config_sync();
  g_free(tmp);
  return  res;
}				/* config_mailbox_delete */

/* Update the configuration information for the specified mailbox. */
gint
config_mailbox_update (LibBalsaMailbox * mailbox)
{
  gchar * key; /* the key in the mailbox section name */
  gint res;

  key = mailbox_section_path(mailbox);
  res = gnome_config_private_has_section(key);
  gnome_config_private_clean_section(key);
  gnome_config_push_prefix(key);
  libbalsa_mailbox_save_config(mailbox);
  gnome_config_pop_prefix();
  gnome_config_sync();
  return res;
}				/* config_mailbox_update */

/* This function initializes all the mailboxes internally, going through
   the list of all the mailboxes in the configuration file one by one. */
static gint
config_mailboxes_init (void)
{
  void *iterator;
  gchar *key, *val, *tmp;
  int pref_len =   strlen(MboxSectionPrefix);

  iterator = gnome_config_private_init_iterator_sections("balsa");
  g_print("config_mailboxes_init\n");
  while( (iterator = gnome_config_iterator_next(iterator, &key, &val)) ) {
    if(strncmp(key, MboxSectionPrefix, pref_len) == 0) {
      tmp = g_strconcat("balsa/", key, "/",NULL);
      gnome_config_push_prefix(tmp);
      config_mailbox_init (key+pref_len);
      gnome_config_pop_prefix();
      g_free(tmp);
    }
  }
  return TRUE; /* hm... check_basic_mailboxes? */
}				/* config_mailboxes_init */

#if 0
static gboolean
get_raw_imap_data(gchar ** username, gchar **passwd, 
                  gchar ** server,  gint * port, gchar **path)
{
    gchar *field;
    if ((field =  gnome_config_private_get_string("Username")) == NULL)
        return FALSE;
    else *username = g_strdup(field);

    if( (field = gnome_config_private_get_string ("Password")) != NULL)
      *passwd = field; /* rot (field); */
    else *passwd = NULL;
    if ((field = gnome_config_private_get_string("Server")) == NULL)
        return FALSE;
    else *server = g_strdup(field);
    *port = d_get_gint ("Port", 143);

    if( (field = gnome_config_private_get_string ("Path")) == NULL)
        return FALSE;
    else *path = g_strdup(field);

    return TRUE;
}
#endif

/* Initialize the specified mailbox, creating the internal data
   structures which represent the mailbox. */
static gint
config_mailbox_init (const gchar * key)
{
  LibBalsaMailboxType mailbox_type;
  LibBalsaMailbox *mailbox;
  gchar *mailbox_name, *type, *field;
  GNode *node;

  g_return_val_if_fail (key != NULL, FALSE);

  mailbox_type = MAILBOX_UNKNOWN;
  mailbox = NULL;

  /* All mailboxes have a type and a name.  Grab those. */
  type = gnome_config_private_get_string ("Type");
  if (type == NULL)  {
    fprintf (stderr, "config_mailbox_init: mailbox type not set\n");
    return FALSE;
  }
  mailbox_name =gnome_config_private_get_string ("Name");
  if (mailbox_name == NULL)
    mailbox_name = g_strdup ("Friendly Mailbox Name");

  /* Now grab the mailbox-type-specific fields */
  if (!strcasecmp (type, "local"))	/* Local mailbox */
    {
      gchar *path;

      if( !(path =  gnome_config_private_get_string("Path")) )
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

      if(!libbalsa_server_load_conf(server, 110))
	return FALSE;

      LIBBALSA_MAILBOX_POP3 (mailbox)->check = d_get_gint ("Check",FALSE);
      LIBBALSA_MAILBOX_POP3 (mailbox)->delete_from_server =
	d_get_gint ("Delete", FALSE);

      if ((field =  gnome_config_private_get_string ("LastUID")) == NULL)
	LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid = NULL;
      else
	LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid = g_strdup (field);

      LIBBALSA_MAILBOX_POP3 (mailbox)->use_apop = d_get_gint ("Apop", FALSE);

      balsa_app.inbox_input =
	g_list_append (balsa_app.inbox_input, mailbox);

      mailbox->pkey = g_strdup(key);
      return TRUE; /* Don't put POP mailbox in mailbox nodes */
    }
  else if (!strcasecmp (type, "IMAP"))	/* IMAP Mailbox */
    {
      LibBalsaMailboxImap * m;
      LibBalsaServer *s;
      gchar *path;

      mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_imap_new());
      mailbox->name = mailbox_name;


      m = LIBBALSA_MAILBOX_IMAP(mailbox);
      s = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
      if( !libbalsa_server_load_conf(s, 143) ){
	gtk_object_destroy( GTK_OBJECT(mailbox) );
	return FALSE;
      }

      path = d_get_string("Path", "INBOX");
      libbalsa_mailbox_imap_set_path(m, path);
      gtk_signal_connect(GTK_OBJECT(s), "get-password", 
			 GTK_SIGNAL_FUNC(ask_password), m);
      g_free(path);
    }
#if 0
  else if (!strcasecmp (type, "IMAPDir")) {
      ImapDir * id = imapdir_new();

      if( !get_raw_imap_data(&id->user, &id->passwd, &id->host,&id->port,
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
#endif
  else
    {
      fprintf (stderr, "config_mailbox_init: Unknown mailbox type \"%s\" "
	       "on mailbox %s\n", type, mailbox_name);
    }

  mailbox->pkey = g_strdup(key);
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
static gint
config_global_load (void)
{
  gchar *field;

  gnome_config_push_prefix("balsa/globals/");

  /* user's real name */
  g_free (balsa_app.address->personal);
  balsa_app.address->personal = gnome_config_private_get_string("RealName");

  /* user's email address */
  g_free (balsa_app.address->mailbox);
  balsa_app.address->mailbox = gnome_config_private_get_string("Email");

  /* users's replyto address */
  g_free (balsa_app.replyto);
  balsa_app.replyto =  gnome_config_private_get_string("ReplyTo");

  /* users's domain */
  g_free (balsa_app.domain);
  balsa_app.domain = gnome_config_private_get_string("Domain");

  /* bcc field for outgoing mails; optional */
  g_free (balsa_app.bcc);
  balsa_app.bcc = gnome_config_private_get_string("Bcc");

  /* directory */
  g_free (balsa_app.local_mail_directory);
  balsa_app.local_mail_directory = gnome_config_private_get_string("MailDir");

  /* signature file path */
  g_free (balsa_app.signature_path);
  if ((balsa_app.signature_path = 
       gnome_config_private_get_string("SignaturePath")) == NULL)  {
      balsa_app.signature_path = g_malloc (strlen (g_get_home_dir ()) + 12);
      sprintf (balsa_app.signature_path, "%s/.signature", g_get_home_dir ());
  }

  balsa_app.sig_sending     = d_get_gint ("SigSending",   TRUE);
  balsa_app.sig_whenreply   = d_get_gint ("SigReply",     TRUE);
  balsa_app.sig_whenforward = d_get_gint ("SigForward",   TRUE);
  balsa_app.sig_separator   = d_get_gint ("SigSeparator", TRUE);

  balsa_app.information_message = d_get_gint ("ShowInformationMsgs", 
					      BALSA_INFORMATION_SHOW_NONE);
  balsa_app.warning_message = d_get_gint ("ShowWarningMsgs", 
					  BALSA_INFORMATION_SHOW_LIST);
  balsa_app.error_message = d_get_gint ("ShowErrorMsgs", 
					BALSA_INFORMATION_SHOW_DIALOG);
  balsa_app.debug_message = d_get_gint ("ShowDebugMsgs", 
					BALSA_INFORMATION_SHOW_NONE);

  /* smtp server */
  balsa_app.smtp_server = gnome_config_private_get_string("SMTPServer");
  balsa_app.smtp        = d_get_gint("SMTP", FALSE);
 
  /* Check mail timer */
  balsa_app.check_mail_auto =  d_get_gint ("CheckMailAuto", FALSE);
  balsa_app.check_mail_timer = d_get_gint ("CheckMailMinutes", 10);

  if (balsa_app.check_mail_timer < 1 )
    balsa_app.check_mail_timer = 10;

  if( balsa_app.check_mail_auto )
    update_timer(TRUE, balsa_app.check_mail_timer );

  /* Word Wrap */
  balsa_app.wordwrap   = d_get_gint ("WordWrap",TRUE);
  balsa_app.wraplength = d_get_gint ("WrapLength",75);

  if (balsa_app.wraplength < 40)
    balsa_app.wraplength = 40;

  balsa_app.browse_wrap   = d_get_gint ("BrowseWrap",   TRUE);
  balsa_app.shown_headers = d_get_gint ("ShownHeaders", HEADERS_SELECTED);

  balsa_app.selected_headers = d_get_string("SelectedHeaders",
					    DEFAULT_SELECTED_HDRS);
  g_strdown(balsa_app.selected_headers);

  balsa_app.show_mblist        = d_get_gint ("ShowMbList",TRUE);
  balsa_app.show_notebook_tabs = d_get_gint ("ShowTabs",  FALSE);

  /* toolbar style */
  balsa_app.toolbar_style =d_get_gint("ToolbarStyle",GTK_TOOLBAR_BOTH);

  /* Progress Window Dialog */
  balsa_app.pwindow_option = d_get_gint ("ProgressWnd", WHILERETR);

  /* use the preview pane */
  balsa_app.previewpane = d_get_gint ("UsePreviewPane",TRUE);
  
  /* column width settings */
  balsa_app.mblist_name_width = d_get_gint ("MBListNameWidth",
					    MBNAME_DEFAULT_WIDTH);
  balsa_app.mblist_newmsg_width = d_get_gint ("MBListNewMsgWidth",
					      NEWMSGCOUNT_DEFAULT_WIDTH);
  balsa_app.mblist_totalmsg_width = d_get_gint ("MBListTotalMsgWidth",
						TOTALMSGCOUNT_DEFAULT_WIDTH);

  /* show mailbox content info */
  balsa_app.mblist_show_mb_content_info = d_get_gint ("ShowMailboxContentInfo",
						      TRUE);

  /* debugging enabled */
  balsa_app.debug = d_get_gint ("Debug",FALSE);

  /* window sizes */
  balsa_app.mw_width     = d_get_gint ("MainWindowWidth",640);
  balsa_app.mw_height    = d_get_gint ("MainWindowHeight",480);
  balsa_app.mblist_width = d_get_gint ("MailboxListWidth",100);

  /* restore column sizes from previous session */
  balsa_app.index_num_width = d_get_gint ("IndexNumWidth",NUM_DEFAULT_WIDTH);
  balsa_app.index_status_width = d_get_gint ("IndexStatusWidth",
					     STATUS_DEFAULT_WIDTH);
  balsa_app.index_attachment_width = d_get_gint ("IndexAttachmentWidth",
						 ATTACHMENT_DEFAULT_WIDTH);
  balsa_app.index_from_width = d_get_gint("IndexFromWidth",FROM_DEFAULT_WIDTH);
  balsa_app.index_subject_width = d_get_gint ("IndexSubjectWidth",
					      SUBJECT_DEFAULT_WIDTH);
  balsa_app.index_date_width = d_get_gint("IndexDateWidth",DATE_DEFAULT_WIDTH);

  /* PKGW: why comment this out? Breaks my Transfer context menu. */
  if (balsa_app.mblist_width < 100)
      balsa_app.mblist_width = 170;

  balsa_app.notebook_height = d_get_gint ("NotebookHeight",170);
  /* FIXME this can be removed later */
  /* PKGW see above */
  if (balsa_app.notebook_height < 100)
      balsa_app.notebook_height = 200;

  /* arp --- LeadinStr for "reply to" leadin. */
  g_free (balsa_app.quote_str);
  balsa_app.quote_str = d_get_string("LeadinStr", "> ");

  /* regular expression used in determining quoted text */
  g_free (balsa_app.quote_regex);
  balsa_app.quote_regex = d_get_string("QuoteRegex", DEFAULT_QUOTE_REGEX);

  /* font used to display messages */
  g_free(balsa_app.message_font);
  balsa_app.message_font = d_get_string("MessageFont", DEFAULT_MESSAGE_FONT);

  /* more here */
  g_free(balsa_app.charset);
  balsa_app.charset = d_get_string("Charset", DEFAULT_CHARSET);
  libbalsa_set_charset (balsa_app.charset);
  balsa_app.encoding_style = d_get_gint("EncodingStyle", 2);

  /* shown headers in the compose window */
  g_free (balsa_app.compose_headers);
  balsa_app.compose_headers = d_get_string ("ComposeHeaders", "to subject cc");

  g_free(balsa_app.PrintCommand.PrintCommand);
  balsa_app.PrintCommand.PrintCommand = d_get_string("PrintCommand", 
						     "a2ps -d -q %s");
  balsa_app.PrintCommand.linesize = d_get_gint ("PrintLinesize",
						DEFAULT_LINESIZE);
  balsa_app.PrintCommand.breakline = d_get_gint ("PrintBreakline", FALSE);
  balsa_app.check_mail_upon_startup = d_get_gint("CheckMailUponStartup",FALSE);
  balsa_app.remember_open_mboxes =  d_get_gint ("RememberOpenMailboxes",FALSE);

  if ( balsa_app.remember_open_mboxes &&
       ( field = gnome_config_private_get_string ("OpenMailboxes")) != NULL &&
       strlen(field)>0 ) {
    if(balsa_app.open_mailbox) {
      gchar * str = g_strconcat(balsa_app.open_mailbox, ";", field,NULL);
      g_free(balsa_app.open_mailbox);
      balsa_app.open_mailbox = str;
    } else balsa_app.open_mailbox = g_strdup(field);
  }

  balsa_app.empty_trash_on_exit = d_get_gint ("EmptyTrash", FALSE);

  /* Here we load the unread mailbox colour for the mailbox list */
  balsa_app.mblist_unread_color.red = d_get_gint ("MBListUnreadColorRed",
						  MBLIST_UNREAD_COLOR_RED);
  balsa_app.mblist_unread_color.green = d_get_gint ("MBListUnreadColorGreen",
						    MBLIST_UNREAD_COLOR_GREEN);
  balsa_app.mblist_unread_color.blue = d_get_gint ("MBListUnreadColorBlue",
						   MBLIST_UNREAD_COLOR_BLUE);

  /*
   * Here we load the quoted text colour for the mailbox list.
   * We load two colours, and recalculate the gradient.
   */
  balsa_app.quoted_color[0].red = d_get_gint ("QuotedColorStartRed",
					      QUOTED_COLOR_RED);
  balsa_app.quoted_color[0].green = d_get_gint ("QuotedColorStartGreen",
						QUOTED_COLOR_GREEN);
  balsa_app.quoted_color[0].blue = d_get_gint ("QuotedColorStartBlue",
					       QUOTED_COLOR_BLUE);

  balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].red = 
    d_get_gint ("QuotedColorEndRed",QUOTED_COLOR_RED);
  balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].green = 
    d_get_gint ("QuotedColorEndGreen",QUOTED_COLOR_GREEN);
  balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].blue = 
    d_get_gint ("QuotedColorEndBlue",QUOTED_COLOR_BLUE);
  make_gradient (balsa_app.quoted_color, 0, MAX_QUOTED_COLOR - 1);


  /* address book location */
  balsa_app.ab_dist_list_mode =  d_get_gint ("AddressBookDistMode", FALSE);

  g_free (balsa_app.ab_location);
  balsa_app.ab_location = d_get_string("AddressBookFile", 
				       DEFAULT_ADDRESS_BOOK_PATH);
  balsa_app.alias_find_flag = d_get_gint ("AliasFlag",FALSE);

  /* spell checking */
  balsa_app.module = d_get_gint ("PspellModule", DEFAULT_PSPELL_MODULE);
  balsa_app.suggestion_mode = d_get_gint ("PspellSuggestMode", 
                                          DEFAULT_PSPELL_SUGGEST_MODE);
  balsa_app.ignore_size = d_get_gint ("PspellIgnoreSize", 
                                      DEFAULT_PSPELL_IGNORE_SIZE);


  /* How we format dates */
  g_free(balsa_app.date_string);
  balsa_app.date_string = d_get_string("DateFormat", DEFAULT_DATE_FORMAT);

#ifdef ENABLE_LDAP
  /*
   * LDAP can set a host, and a base Domain name.
   */
  g_free (balsa_app.ldap_host);
  balsa_app.ldap_host = gnome_config_private_get_string("LDAPHost");

  g_free (balsa_app.ldap_base_dn);
  balsa_app.ldap_base_dn =gnome_config_private_get_string("BaseDN");
#endif /* ENABLE_LDAP */
  
  gnome_config_pop_prefix();
  return TRUE;
}				/* config_global_load */

gint
config_save (void)
{

  /* Out with the old */
  gnome_config_private_clean_section("balsa/globals");
  gnome_config_push_prefix("balsa/globals/");

  /* In with the new */
  gnome_config_private_set_string ("RealName", balsa_app.address->personal);
  if (balsa_app.address->mailbox != NULL)
    gnome_config_private_set_string("Email", balsa_app.address->mailbox);
  if (balsa_app.replyto != NULL)
    gnome_config_private_set_string("ReplyTo", balsa_app.replyto);
  if (balsa_app.domain != NULL)
    gnome_config_private_set_string("Domain", balsa_app.domain);
  if (balsa_app.bcc != NULL)
    gnome_config_private_set_string("Bcc", balsa_app.bcc);

  if (balsa_app.local_mail_directory != NULL)
    gnome_config_private_set_string("MailDir", balsa_app.local_mail_directory);

  if (balsa_app.smtp) d_set_gint ("SMTP", balsa_app.smtp);
  if (balsa_app.smtp_server != NULL)
    gnome_config_private_set_string("SMTPServer", balsa_app.smtp_server);

  if (balsa_app.signature_path != NULL)
    gnome_config_private_set_string("SignaturePath", balsa_app.signature_path);

  d_set_gint ("SigSending", balsa_app.sig_sending);
  d_set_gint ("SigForward", balsa_app.sig_whenforward);
  d_set_gint ("SigReply",   balsa_app.sig_whenreply);
  d_set_gint ("SigSeparator", balsa_app.sig_separator);

  d_set_gint ("ShowInformationMsgs", balsa_app.information_message );
  d_set_gint ("ShowWarningMsgs", balsa_app.warning_message );
  d_set_gint ("ShowErrorMsgs", balsa_app.error_message );
  d_set_gint ("ShowDebugMsgs", balsa_app.debug_message );

  d_set_gint ("ToolbarStyle", balsa_app.toolbar_style);
  d_set_gint ("ProgressWnd", balsa_app.pwindow_option);
  d_set_gint ("Debug", balsa_app.debug);
  d_set_gint ("UsePreviewPane", balsa_app.previewpane);

  d_set_gint ("MBListNameWidth",   balsa_app.mblist_name_width);
  d_set_gint ("MBListNewMsgWidth", balsa_app.mblist_newmsg_width);
  d_set_gint ("MBListTotalMsgWidth",balsa_app.mblist_totalmsg_width);
  d_set_gint ("ShowMailboxContentInfo", balsa_app.mblist_show_mb_content_info);
  
  d_set_gint ("MainWindowWidth",  balsa_app.mw_width);
  d_set_gint ("MainWindowHeight", balsa_app.mw_height);
  
  d_set_gint ("MailboxListWidth", balsa_app.mblist_width);
  d_set_gint ("NotebookHeight",   balsa_app.notebook_height);
  d_set_gint ("IndexNumWidth",    balsa_app.index_num_width);
  d_set_gint ("IndexStatusWidth", balsa_app.index_status_width);
  d_set_gint ("IndexAttachmentWidth", balsa_app.index_attachment_width);
  
  d_set_gint ("IndexFromWidth",    balsa_app.index_from_width);
  d_set_gint ("IndexSubjectWidth", balsa_app.index_subject_width);
  d_set_gint ("IndexDateWidth",    balsa_app.index_date_width);
  d_set_gint ("EncodingStyle",     balsa_app.encoding_style);
  d_set_gint ("CheckMailAuto",     balsa_app.check_mail_auto);
  d_set_gint ("CheckMailMinutes",  balsa_app.check_mail_timer);
  d_set_gint ("WordWrap",          balsa_app.wordwrap );
  d_set_gint ("WrapLength",        balsa_app.wraplength);
  d_set_gint ("BrowseWrap",        balsa_app.browse_wrap );
  d_set_gint ("ShownHeaders",      balsa_app.shown_headers );

  if(balsa_app.selected_headers)
     gnome_config_private_set_string("SelectedHeaders", 
				     balsa_app.selected_headers);

  d_set_gint ("ShowMBList", balsa_app.show_mblist);
  d_set_gint ("ShowTabs", balsa_app.show_notebook_tabs);

  /* arp --- "LeadinStr" into cfg. */
  if (balsa_app.quote_str)
    gnome_config_private_set_string("LeadinStr", balsa_app.quote_str);

  /* quoted text regular expression */
  if (balsa_app.quote_regex)
    gnome_config_private_set_string ("QuoteRegex", balsa_app.quote_regex);

  /* message font */
  if (balsa_app.message_font)
    gnome_config_private_set_string("MessageFont", balsa_app.message_font);

  /* encoding */
  if (balsa_app.charset)
      gnome_config_private_set_string("Charset", balsa_app.charset);

  if (balsa_app.compose_headers)
     gnome_config_private_set_string("ComposeHeaders",
				     balsa_app.compose_headers);

  if (balsa_app.PrintCommand.PrintCommand)
      gnome_config_private_set_string("PrintCommand", 
				      balsa_app.PrintCommand.PrintCommand);

  d_set_gint ("PrintLinesize",        balsa_app.PrintCommand.linesize);
  d_set_gint ("PrintBreakline",       balsa_app.PrintCommand.breakline);
  d_set_gint ("CheckMailUponStartup", balsa_app.check_mail_upon_startup);

  if( balsa_app.open_mailbox)
	  gnome_config_private_set_string("OpenMailboxes", 
					  balsa_app.open_mailbox);

  d_set_gint ("RememberOpenMailboxes", balsa_app.remember_open_mboxes); 
  d_set_gint ("EmptyTrash",            balsa_app.empty_trash_on_exit);
  
  d_set_gint ("MBListUnreadColorR",    balsa_app.mblist_unread_color.red);
  d_set_gint ("MBListUnreadColorG",    balsa_app.mblist_unread_color.green);
  d_set_gint ("MBListUnreadColorB",    balsa_app.mblist_unread_color.blue);

  /*
   * Quoted color - we only save the first and last, and recalculate
   * the gradient when Balsa starts.
   */
  d_set_gint("QuotedColorStartRed",  balsa_app.quoted_color[0].red);
  d_set_gint("QuotedColorStartGreen",balsa_app.quoted_color[0].green);
  d_set_gint("QuotedColorStartBlue", balsa_app.quoted_color[0].blue);
  d_set_gint("QuotedColorEndRed",    
	     balsa_app.quoted_color[MAX_QUOTED_COLOR-1].red);
  d_set_gint("QuotedColorEndGreen", 
	     balsa_app.quoted_color[MAX_QUOTED_COLOR-1].green);
  d_set_gint("QuotedColorEndBlue", 
	     balsa_app.quoted_color[MAX_QUOTED_COLOR-1].blue);

  /* address book */
  d_set_gint ("AddressBookDistMode", balsa_app.ab_dist_list_mode);

  if (balsa_app.ab_location)
    gnome_config_private_set_string("AddressBookFile", balsa_app.ab_location);
  d_set_gint ("AliasFlag", balsa_app.alias_find_flag);
  
  if(balsa_app.date_string)
    gnome_config_private_set_string ("DateFormat", balsa_app.date_string);

  /* spell checking */
  d_set_gint ("PspellModule", balsa_app.module);
  d_set_gint ("PspellSuggestMode", balsa_app.suggestion_mode);
  d_set_gint ("PspellIgnoreSize", balsa_app.ignore_size);

#ifdef ENABLE_LDAP
  if (balsa_app.ldap_host)
    gnome_config_private_set_string ("LDAPHost", balsa_app.ldap_host);
  if (balsa_app.ldap_base_dn)
    gnome_config_private_set_string ("BaseDN", balsa_app.ldap_base_dn);
#endif /* ENABLE_LDAP */

  gnome_config_sync();
  return TRUE;
}				/* config_global_save */


static gchar*
config_mailbox_get_free_pkey ()
{
  int max = 0, curr;
  void *iterator;
  gchar *name, *key, *val;
  int pref_len =   strlen(MboxSectionPrefix);

  g_print("config_mailbox_get_free_pkey\n");
  iterator = gnome_config_private_init_iterator_sections("balsa");

  max = 0;
  while( (iterator = gnome_config_iterator_next(iterator, &key, &val)) ) {
    printf("Trying %s\n", key);
    if(strncmp(key, MboxSectionPrefix, pref_len) == 0) {
      if(strlen(key+pref_len)>1 && (curr = atoi(key+pref_len+1)) && curr>max)
	max = curr;
    }
  }
  name =  g_strdup_printf("m%d", max+1);
  if(balsa_app.debug)
    g_print("config_mailbox_get_highest_number: name='%s'\n", name);
  return name;
}

