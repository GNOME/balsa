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
#include <gnome.h>
#include "balsa-app.h"
#include "new-mailbox.h"
#include "main-window.h"
#include "misc.h"
#include "save-restore.h"

/* we'll create the notebook pages in the
 * order of these enumerated types so they 
 * can be refered to easily
 */
typedef enum
{
  NM_PAGE_NEW,
  NM_PAGE_LOCAL,
  NM_PAGE_POP3,
  NM_PAGE_IMAP,
} NewMailboxPageType;



typedef struct _NewMailboxWindow NewMailboxWindow;
struct _NewMailboxWindow
{
  Mailbox *mailbox;

  GtkWidget *window;
  GtkWidget *notebook;
  GtkWidget *ok;
  GtkWidget *cancel;


  /* new page */
  GtkWidget *mailbox_type_menu;
  NewMailboxPageType next_page;


  /* for local mailboxes */
  GtkWidget *local_mailbox_name;
  GtkWidget *local_type_menu;
  GtkWidget *local_mailbox_path;


};

static GList *open_mailbox_list = NULL;




/* notebook pages */
static GtkWidget * create_new_page (NewMailboxWindow * nmw);
static GtkWidget * create_local_mailbox_page ();


/* callbacks */
static void destroy_new_mailbox (GtkWidget * widget);
static void close_new_mailbox (GtkWidget * widget);
static void ok_new_mailbox (GtkWidget * widget);
static void refresh_new_mailbox (NewMailboxWindow * nmw);
static void refresh_button_state (NewMailboxWindow * nmw);

/* callbacks for new page */
static void local_mailbox_type_cb (GtkWidget * widget);
static void next_cb (GtkWidget * widget);

/* callbacks for local page */
static void local_mailbox_name_changed_cb (GtkWidget * widget);
static void local_mailbox_standard_path_cb (GtkWidget * widget);
static void local_mailbox_fixed_path_cb (GtkWidget * widget);


/* misc */
static void set_new_mailbox_data (GtkObject * object, NewMailboxWindow *nmw);
static NewMailboxWindow * get_new_mailbox_data (GtkObject * object);




void
open_new_mailbox (Mailbox * mailbox)
{
  NewMailboxWindow *nmw;
  GList *list;
  GtkWidget *bbox;


  /* DISABLE EDITING FOR NOW */
  if (mailbox)
    return;

  /* keep a list of mailboxes which are being edited so
   * we don't do a double-edit thing, kus, like, that would
   * suck bevis */
  if (mailbox)
    {
      list = open_mailbox_list;

      while (list)
	{
	  nmw = (NewMailboxWindow *) list->data;
	  list = list->next;

	  if (mailbox == nmw->mailbox)
	    {
	      gdk_window_raise (nmw->window->window);
	      return;
	    }
	}
    }


  nmw = g_malloc (sizeof (NewMailboxWindow));
  nmw->mailbox = mailbox;
  open_mailbox_list = g_list_append (open_mailbox_list, nmw);


  nmw->window = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (nmw->window), "New Mailbox");
  gtk_container_border_width (GTK_CONTAINER (nmw->window), 0);

  set_new_mailbox_data (GTK_OBJECT (nmw->window), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->window),
                      "destroy",
                      (GtkSignalFunc) destroy_new_mailbox,
                      NULL);

  gtk_signal_connect (GTK_OBJECT (nmw->window),
                      "delete_event",
                      (GtkSignalFunc) gtk_false,
                      NULL);



  /* notbook for action area of dialog */
  nmw->notebook = gtk_notebook_new ();
  gtk_container_border_width (GTK_CONTAINER (nmw->notebook), 5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (nmw->window)->vbox), nmw->notebook, TRUE, TRUE, 0);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (nmw->notebook), FALSE);
  gtk_notebook_set_show_border ( GTK_NOTEBOOK(nmw->notebook), FALSE);
  gtk_widget_show (nmw->notebook);


  /* notebook pages */
  gtk_notebook_append_page (GTK_NOTEBOOK (nmw->notebook),
			    create_new_page (nmw),
			    NULL);

  gtk_notebook_append_page (GTK_NOTEBOOK (nmw->notebook),
			    create_local_mailbox_page (nmw),
			    NULL);



  /* close button (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (nmw->window)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox), BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  /* ok button */
  nmw->ok = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
  gtk_container_add (GTK_CONTAINER (bbox), nmw->ok);

  set_new_mailbox_data (GTK_OBJECT (nmw->ok), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->ok),
		      "clicked",
		      (GtkSignalFunc) ok_new_mailbox,
		      NULL);

  gtk_widget_show (nmw->ok);


  /* cancel button */
  nmw->cancel = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL); 
  gtk_container_add (GTK_CONTAINER (bbox), nmw->cancel);
  
  set_new_mailbox_data (GTK_OBJECT (nmw->cancel), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->cancel),
		      "clicked",
		      (GtkSignalFunc) close_new_mailbox,
		      NULL);

  gtk_widget_show (nmw->cancel);




  refresh_new_mailbox (nmw);
  refresh_button_state (nmw);
  gtk_widget_show (nmw->window);
}


static void 
destroy_new_mailbox (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));

  /* remove the mailbox from the open mailbox list */
  open_mailbox_list = g_list_remove (open_mailbox_list, nmw);

  g_free (nmw);
}


static void
close_new_mailbox (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  gtk_widget_destroy (nmw->window);
}


static void 
ok_new_mailbox (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  GtkWidget *menu;
  GtkWidget *menuitem;
  MailboxType mailbox_type;
  Mailbox *mailbox;


  switch (nmw->next_page)
    {
    case NM_PAGE_LOCAL:
      menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (nmw->local_type_menu));
      menuitem = gtk_menu_get_active (GTK_MENU (menu));
      mailbox_type = (MailboxType) gtk_object_get_user_data (GTK_OBJECT (menuitem));
      
      mailbox =  mailbox_new (mailbox_type);
      mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->local_mailbox_name)));
      MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->local_mailbox_path)));
      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mailbox);

      add_mailbox_config (mailbox->name, MAILBOX_LOCAL (mailbox)->path, 0);
      break;
      
    case NM_PAGE_POP3:
      break;
      
    case NM_PAGE_IMAP:
      break;
    }


  /* close the new mailbox window */
  refresh_mailbox_manager ();
  refresh_main_window ();
  gtk_widget_destroy (nmw->window);
}


static void
refresh_new_mailbox (NewMailboxWindow * nmw)
{
  GString *str = g_string_new (NULL);

  if (nmw->mailbox == NULL)
    {
      nmw->next_page = NM_PAGE_LOCAL;
      gtk_window_set_title (GTK_WINDOW (nmw->window), "Mailbox: New");
    }
  else
    {
      g_string_assign (str, "Mailbox: ");
      g_string_append (str, nmw->mailbox->name);
      gtk_window_set_title (GTK_WINDOW (nmw->window), str->str);

      gtk_notebook_set_page (GTK_NOTEBOOK (nmw->notebook), 1);
    }


  /* cleanup */
  g_string_free (str, TRUE);
}



static void
refresh_button_state (NewMailboxWindow * nmw)
{
  switch (gtk_notebook_current_page (GTK_NOTEBOOK (nmw->notebook)))
    {
    case NM_PAGE_NEW:
      gtk_widget_set_sensitive (nmw->ok, FALSE);
      break;
      
    default:
      gtk_widget_set_sensitive (nmw->ok, TRUE);
      break;
    }
}


/*
 * create notebook pages
 */
static GtkWidget *
create_new_page (NewMailboxWindow * nmw)
{
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *button;
  GtkWidget *radio_button;

  
  hbox = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (hbox);


  frame = gtk_frame_new ("Mailbox Type");
  gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);


  /* the 'Continue' button */
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 5);
  gtk_widget_show (vbox);


  button = gtk_button_new_with_label ("Continue...");
  gtk_widget_set_usize (button, BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 5);
  set_new_mailbox_data (GTK_OBJECT (button), nmw);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) next_cb,
		      NULL);

  gtk_widget_show (button);


  /* inside the frame */
  table = gtk_table_new (2, 4, FALSE);
  gtk_container_border_width (GTK_CONTAINER (table), 5);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_widget_show (table);


  /* radio buttons */
  radio_button = gtk_radio_button_new_with_label (NULL, "Local");
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL | GTK_EXPAND, 
		    0, 0);

  set_new_mailbox_data (GTK_OBJECT (radio_button), nmw);
  gtk_object_set_user_data (GTK_OBJECT (radio_button), (gpointer) NM_PAGE_LOCAL);

  gtk_signal_connect (GTK_OBJECT (radio_button),
		      "clicked",
		      (GtkSignalFunc) local_mailbox_type_cb,
		      NULL);

  gtk_widget_show (radio_button);
  


  radio_button = gtk_radio_button_new_with_label 
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "POP3");
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL | GTK_EXPAND, 
		    0, 0);

  set_new_mailbox_data (GTK_OBJECT (radio_button), nmw);
  gtk_object_set_user_data (GTK_OBJECT (radio_button), (gpointer) NM_PAGE_POP3);

  gtk_signal_connect (GTK_OBJECT (radio_button),
		      "clicked",
		      (GtkSignalFunc) local_mailbox_type_cb,
		      NULL);

  gtk_widget_show (radio_button);



  radio_button = gtk_radio_button_new_with_label 
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "IMAP");
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL | GTK_EXPAND, 
		    0, 0);

  set_new_mailbox_data (GTK_OBJECT (radio_button), nmw);
  gtk_object_set_user_data (GTK_OBJECT (radio_button), (gpointer) NM_PAGE_IMAP);

  gtk_signal_connect (GTK_OBJECT (radio_button),
		      "clicked",
		      (GtkSignalFunc) local_mailbox_type_cb,
		      NULL);

  gtk_widget_show (radio_button);

  return hbox;
}



static GtkWidget *
create_local_mailbox_page (NewMailboxWindow * nmw)
{
  GtkWidget *return_widget;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *menu;
  GtkWidget *menuitem;
  GtkWidget *frame;
  GtkWidget *radio_button;


  return_widget = table = gtk_table_new (3, 2, FALSE);
  gtk_widget_show (table);


  /* mailbox name */
  label = gtk_label_new ("Mailbox Name:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  nmw->local_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->local_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  set_new_mailbox_data (GTK_OBJECT (nmw->local_mailbox_name), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->local_mailbox_name),
		      "changed",
		      (GtkSignalFunc) local_mailbox_name_changed_cb,
		      NULL);

  gtk_widget_show (nmw->local_mailbox_name);


  /* mailbox type */
  label = gtk_label_new ("Mailbox Type:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);

  menu = gtk_menu_new ();

  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_MBOX),
			     NULL,
			     NULL,
			     (gpointer) MAILBOX_MBOX);
  set_new_mailbox_data (GTK_OBJECT (menuitem), nmw);

  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_MH),
			     NULL,
			     NULL,
			     (gpointer) MAILBOX_MH);
  set_new_mailbox_data (GTK_OBJECT (menuitem), nmw);

  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_MH),
			     NULL,
			     NULL,
			     (gpointer) MAILBOX_MH);

  nmw->local_type_menu = gtk_option_menu_new ();
  gtk_widget_set_usize (nmw->local_type_menu, 0, BALSA_BUTTON_HEIGHT);
  gtk_option_menu_set_menu (GTK_OPTION_MENU (nmw->local_type_menu), menu);
  gtk_table_attach (GTK_TABLE (table), nmw->local_type_menu, 1, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL,
		    0, 10);
  gtk_widget_show (nmw->local_type_menu);



  /* mailbox path */
  frame = gtk_frame_new ("Mailbox Path");
  gtk_table_attach (GTK_TABLE (table), frame, 0, 2, 2, 3,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		    0, 10);
  gtk_widget_show (frame);

  table = gtk_table_new (2, 2, FALSE);
  gtk_container_border_width (GTK_CONTAINER (table), 5);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_widget_show (table);


  radio_button = gtk_radio_button_new_with_label (NULL, "Standard Mailbox Path");
  gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (radio_button), TRUE);
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 0, 1,
                    GTK_FILL, GTK_FILL | GTK_EXPAND,
                    0, 0);

  set_new_mailbox_data (GTK_OBJECT (radio_button), nmw);

  gtk_signal_connect (GTK_OBJECT (radio_button),
                      "clicked",
                      (GtkSignalFunc) local_mailbox_standard_path_cb,
                      NULL);

  gtk_widget_show (radio_button);


  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "Fixed Path");
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 1, 2,
                    GTK_FILL, GTK_FILL | GTK_EXPAND,
                    0, 0);

  set_new_mailbox_data (GTK_OBJECT (radio_button), nmw);

  gtk_signal_connect (GTK_OBJECT (radio_button),
                      "clicked",
                      (GtkSignalFunc) local_mailbox_fixed_path_cb,
                      NULL);

  gtk_widget_show (radio_button);

  nmw->local_mailbox_path = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->local_mailbox_path , 1, 2, 1, 2,
                    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
                    0, 0);
  set_new_mailbox_data (GTK_OBJECT (nmw->local_mailbox_path), nmw);
  gtk_widget_show (nmw->local_mailbox_path);


  return return_widget;
}


/*
 * callbacks
 */
static void
local_mailbox_type_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  NewMailboxPageType type = (NewMailboxPageType) gtk_object_get_user_data (GTK_OBJECT (widget));
  nmw->next_page = type;
}


static void
next_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));

  switch (nmw->next_page)
    {
    case NM_PAGE_LOCAL:
      gtk_notebook_set_page (GTK_NOTEBOOK (nmw->notebook), NM_PAGE_LOCAL);
      break;

    case NM_PAGE_POP3:
      break;

    case NM_PAGE_IMAP:
      break;
    }

  refresh_button_state (nmw);
}


/*
 * local page callbacks
 */
static void
local_mailbox_name_changed_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  GString *str;

  str = g_string_new (balsa_app.local_mail_directory);
  g_string_append_c (str, '/');
  g_string_append (str, gtk_entry_get_text (GTK_ENTRY (widget)));

  gtk_entry_set_text (GTK_ENTRY (nmw->local_mailbox_path), str->str);

  g_string_free (str, TRUE);
}


static void
local_mailbox_standard_path_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  GString *str;

  str = g_string_new (balsa_app.local_mail_directory);
  g_string_append_c (str, '/');
  g_string_append (str, gtk_entry_get_text (GTK_ENTRY (nmw->local_mailbox_name)));

  gtk_widget_set_sensitive (nmw->local_mailbox_path, FALSE);
  gtk_entry_set_text (GTK_ENTRY (nmw->local_mailbox_path), str->str);

  g_string_free (str, TRUE);

}

static void
local_mailbox_fixed_path_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  gtk_widget_set_sensitive (nmw->local_mailbox_path, TRUE);
  gtk_entry_set_text (GTK_ENTRY (nmw->local_mailbox_path), "");
}


/*
 * pop3 page callbacks
 */




/*
 * set/get data convience functions used for attaching the
 * NewMailboxWindow structure to GTK objects so it can be retrieved
 * in callbacks
 */
static void
set_new_mailbox_data (GtkObject * object, NewMailboxWindow *nmw)
{
  gtk_object_set_data (object, "new_mailbox_data", (gpointer) nmw);
}


static NewMailboxWindow * 
get_new_mailbox_data (GtkObject * object)
{
  return gtk_object_get_data (object, "new_mailbox_data");
}
