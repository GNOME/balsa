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




typedef struct _NewMailboxWindow NewMailboxWindow;
struct _NewMailboxWindow
{
  Mailbox *mailbox;

  GtkWidget *window;
  GtkWidget *notebook;
};

static GList *open_mailbox_list = NULL;


/* notebook pages */



/* callbacks */
static void destroy_new_mailbox (GtkWidget * widget);
static void close_new_mailbox (GtkWidget * widget);
static void refresh_new_mailbox (NewMailboxWindow * nmw);



/* misc */
static void set_new_mailbox_data (GtkObject * object, NewMailboxWindow *nmw);
static NewMailboxWindow * get_new_mailbox_data (GtkObject * object);




void
open_new_mailbox (Mailbox * mailbox)
{
  NewMailboxWindow *nmw;
  GtkWidget *button;
  GtkWidget *bbox;


  /* keep a list of mailboxes which are being edited so
   * we don't do a double-edit thing, kus, like, that would
   * suck bevis */
  if (mailbox)
    {
      if (g_list_find (open_mailbox_list, mailbox))
	return;
      else
	open_mailbox_list = g_list_append (open_mailbox_list, mailbox);
    }


  nmw = g_malloc (sizeof (NewMailboxWindow));
  nmw->mailbox = mailbox;


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
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (nmw->window)->vbox), nmw->notebook, TRUE, TRUE, 0);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (nmw->notebook), FALSE);
  gtk_notebook_set_show_border ( GTK_NOTEBOOK(nmw->notebook), FALSE);
  gtk_widget_show (nmw->notebook);



  /* close button (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (nmw->window)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
				 BALSA_BUTTON_WIDTH,
				 BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  button = gnome_stock_button (GNOME_STOCK_BUTTON_CLOSE);
  gtk_container_add (GTK_CONTAINER (bbox), button);

  set_new_mailbox_data (GTK_OBJECT (button), nmw);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) close_new_mailbox,
		      NULL);

  gtk_widget_show (button);




  refresh_new_mailbox (nmw);
  gtk_widget_show (nmw->window);
}


static void 
destroy_new_mailbox (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));

  /* remove the mailbox from the open mailbox list */
  if (nmw->mailbox && g_list_find (open_mailbox_list, nmw->mailbox))
    open_mailbox_list = g_list_remove (open_mailbox_list, nmw->mailbox);

  g_free (nmw);
}


static void
close_new_mailbox (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  gtk_widget_destroy (nmw->window);
}


static void
refresh_new_mailbox (NewMailboxWindow * nmw)
{
  GString *str = g_string_new (NULL);
  

  /* set the window title */
  if (nmw->mailbox == NULL)
    gtk_window_set_title (GTK_WINDOW (nmw->window), "Mailbox: New");
  else
    {
      g_string_assign (str, "Mailbox: ");
      g_string_append (str, nmw->mailbox->name);

      gtk_window_set_title (GTK_WINDOW (nmw->window), str->str);
    }



  /* cleanup */
  g_string_free (str, TRUE);
}



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
