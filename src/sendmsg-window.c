/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-1999 Jay Painter and Stuart Parmenter
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

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <ctype.h>

#include <sys/stat.h>	/* for check_if_regular_file() */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include "libbalsa.h"

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"

#include "sendmsg-window.h"
#include "address-book.h"
#include "expand-alias.h"
#include "main.h"

static gchar *read_signature (void);
static gint include_file_cb (GtkWidget *, BalsaSendmsg *);
static gint send_message_cb (GtkWidget *, BalsaSendmsg *);
static gint postpone_message_cb (GtkWidget *, BalsaSendmsg *);
static gint print_message_cb (GtkWidget *, BalsaSendmsg *);
static gint attach_clicked (GtkWidget *, gpointer);
static gint close_window (GtkWidget *, gpointer);
static gint check_if_regular_file (const gchar *);
static void balsa_sendmsg_destroy (BalsaSendmsg * bsm);

static void wrap_body_cb(GtkWidget* widget, BalsaSendmsg *bsmsg);
static void reflow_par_cb(GtkWidget* widget, BalsaSendmsg *bsmsg);
static void reflow_body_cb(GtkWidget* widget, BalsaSendmsg *bsmsg);

static void check_readiness(GtkEditable *w, BalsaSendmsg *bsmsg);
static void set_menus(BalsaSendmsg*);
static gint toggle_from_cb (GtkWidget *, BalsaSendmsg *);
static gint toggle_to_cb (GtkWidget *, BalsaSendmsg *);
static gint toggle_subject_cb (GtkWidget *, BalsaSendmsg *);
static gint toggle_cc_cb (GtkWidget *, BalsaSendmsg *);
static gint toggle_bcc_cb (GtkWidget *, BalsaSendmsg *);
static gint toggle_fcc_cb (GtkWidget *, BalsaSendmsg *);
static gint toggle_reply_cb (GtkWidget *, BalsaSendmsg *);
static gint toggle_attachments_cb (GtkWidget *, BalsaSendmsg *);
static gint toggle_comments_cb (GtkWidget *, BalsaSendmsg *);
static gint toggle_keywords_cb (GtkWidget *, BalsaSendmsg *);

static gint set_iso_charset(BalsaSendmsg*, gint , gint );
static gint iso_1_cb(GtkWidget* , BalsaSendmsg *);
static gint iso_2_cb(GtkWidget* , BalsaSendmsg *);
static gint iso_3_cb(GtkWidget* , BalsaSendmsg *);
static gint iso_5_cb(GtkWidget* , BalsaSendmsg *);
static gint iso_8_cb(GtkWidget* , BalsaSendmsg *);
static gint iso_9_cb(GtkWidget* , BalsaSendmsg *);
static gint iso_13_cb(GtkWidget* , BalsaSendmsg *);
static gint iso_14_cb(GtkWidget* , BalsaSendmsg *);
static gint iso_15_cb(GtkWidget* , BalsaSendmsg *);
static gint koi8_r_cb(GtkWidget* , BalsaSendmsg *);
static gint koi8_u_cb(GtkWidget* , BalsaSendmsg *);

/* Standard DnD types */
enum
  {
    TARGET_URI_LIST,
    TARGET_EMAIL,
  };

static GtkTargetEntry drop_types[] =
{
  {"text/uri-list", 0, TARGET_URI_LIST}
};

static GtkTargetEntry email_field_drop_types[] = 
{
  {"x-application/x-email", 0, TARGET_EMAIL }
};

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

static void wrap_body_cb(GtkWidget* widget, BalsaSendmsg *bsmsg);
static void reflow_par_cb(GtkWidget* widget, BalsaSendmsg *bsmsg);
static void reflow_body_cb(GtkWidget* widget, BalsaSendmsg *bsmsg);

static GnomeUIInfo main_toolbar[] =
{
#define TOOL_SEND_POS 0
  GNOMEUIINFO_ITEM_STOCK (N_ ("Send"), N_ ("Send this mail"), send_message_cb,
			  GNOME_STOCK_PIXMAP_MAIL_SND),
  GNOMEUIINFO_SEPARATOR,
#define TOOL_ATTACH_POS 2
  GNOMEUIINFO_ITEM_STOCK (N_ ("Attach"),N_ ("Add attachments to this message"),
			  attach_clicked, GNOME_STOCK_PIXMAP_ATTACH),
  GNOMEUIINFO_SEPARATOR,
#define TOOL_POSTPONE_POS 4
  GNOMEUIINFO_ITEM_STOCK (N_ ("Postpone"), N_ ("Continue this message later"),
			  postpone_message_cb, GNOME_STOCK_PIXMAP_SAVE),
  GNOMEUIINFO_SEPARATOR,
#ifdef BALSA_SHOW_INFO
  GNOMEUIINFO_ITEM_STOCK (N_ ("Spelling"), N_ ("Check Spelling"), 
			  NULL, GNOME_STOCK_PIXMAP_SPELLCHECK),
  GNOMEUIINFO_SEPARATOR,
#endif
  GNOMEUIINFO_ITEM_STOCK (N_ ("Print"), N_ ("Print"), 
			  print_message_cb, GNOME_STOCK_PIXMAP_PRINT),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Cancel"), N_ ("Cancel"), 
			  close_window, GNOME_STOCK_PIXMAP_CLOSE),
  GNOMEUIINFO_END
};

static GnomeUIInfo file_menu[] =
{
#define MENU_FILE_INCLUDE_POS 0
  GNOMEUIINFO_ITEM_STOCK(N_ ("_Include File..."), NULL,
			 include_file_cb, GNOME_STOCK_MENU_OPEN),

#define MENU_FILE_ATTACH_POS 1
  GNOMEUIINFO_ITEM_STOCK(N_ ("_Attach file..."), NULL, 
			 attach_clicked, GNOME_STOCK_MENU_ATTACH),
  GNOMEUIINFO_SEPARATOR,

#define MENU_FILE_SEND_POS 3
  GNOMEUIINFO_ITEM_STOCK(N_ ("_Send"),N_ ("Send the currently edited message"),
			 send_message_cb, GNOME_STOCK_MENU_MAIL_SND),

#define MENU_FILE_POSTPONE_POS 4
  GNOMEUIINFO_ITEM_STOCK(N_ ("_Postpone"), NULL, 
			 postpone_message_cb, GNOME_STOCK_MENU_SAVE),

#define MENU_FILE_PRINT_POS 5
  GNOMEUIINFO_ITEM_STOCK(N_ ("Print"), N_ ("Print the edited message"), 
			  print_message_cb, GNOME_STOCK_PIXMAP_PRINT),
  GNOMEUIINFO_SEPARATOR,

#define MENU_FILE_CLOSE_POS 7
  GNOMEUIINFO_MENU_CLOSE_ITEM(close_window, NULL),

  GNOMEUIINFO_END
};

/* Cut, Copy&Paste are in our case just a placeholders because they work 
   anyway */
static GnomeUIInfo edit_menu[] = 
{
   GNOMEUIINFO_MENU_CUT_ITEM(NULL, NULL),
   GNOMEUIINFO_MENU_COPY_ITEM(NULL, NULL),
   GNOMEUIINFO_MENU_PASTE_ITEM(NULL, NULL),
   GNOMEUIINFO_SEPARATOR,
   { GNOME_APP_UI_ITEM, N_ ("_Wrap body") ,N_ ("Wrap message lines"),
     (gpointer)wrap_body_cb, NULL, NULL,  GNOME_APP_PIXMAP_NONE, NULL, 
     GDK_z, GDK_CONTROL_MASK, NULL },
   GNOMEUIINFO_SEPARATOR,
   { GNOME_APP_UI_ITEM, N_ ("_Reflow paragraph") , NULL,
     (gpointer)reflow_par_cb, NULL, NULL,  GNOME_APP_PIXMAP_NONE, NULL, 
     GDK_r, GDK_CONTROL_MASK, NULL },
   { GNOME_APP_UI_ITEM, N_ ("R_eflow message") , NULL,
     (gpointer)reflow_body_cb, NULL, NULL,  GNOME_APP_PIXMAP_NONE, NULL, 
     GDK_r, GDK_CONTROL_MASK | GDK_SHIFT_MASK, NULL },
   GNOMEUIINFO_END
};

static GnomeUIInfo view_menu[] =
{
#define MENU_TOGGLE_FROM_POS 0
  GNOMEUIINFO_TOGGLEITEM( N_ ("Fr_om"), NULL, toggle_from_cb, NULL),
#define MENU_TOGGLE_TO_POS 1
  GNOMEUIINFO_TOGGLEITEM( N_ ("_To"), NULL, toggle_to_cb, NULL),
#define MENU_TOGGLE_SUBJECT_POS 2
  GNOMEUIINFO_TOGGLEITEM( N_ ("_Subject"), NULL, toggle_subject_cb, NULL),
#define MENU_TOGGLE_CC_POS 3
  GNOMEUIINFO_TOGGLEITEM( N_ ("_Cc"), NULL, toggle_cc_cb, NULL),
#define MENU_TOGGLE_BCC_POS 4
  GNOMEUIINFO_TOGGLEITEM( N_ ("_Bcc"), NULL, toggle_bcc_cb, NULL),
#define MENU_TOGGLE_FCC_POS 5
  GNOMEUIINFO_TOGGLEITEM( N_ ("_Fcc"), NULL, toggle_fcc_cb, NULL),
#define MENU_TOGGLE_REPLY_POS 6
  GNOMEUIINFO_TOGGLEITEM( N_ ("_Reply To"), NULL, toggle_reply_cb, NULL),
#define MENU_TOGGLE_ATTACHMENTS_POS 7
  GNOMEUIINFO_TOGGLEITEM( N_ ("_Attachments"),NULL,toggle_attachments_cb,NULL),
#define MENU_TOGGLE_COMMENTS_POS 8
  GNOMEUIINFO_TOGGLEITEM( N_ ("_Comments"), NULL, toggle_comments_cb, NULL),
#define MENU_TOGGLE_KEYWORDS_POS 9
  GNOMEUIINFO_TOGGLEITEM( N_ ("_Keywords"), NULL, toggle_keywords_cb, NULL),
  GNOMEUIINFO_END
};

#if MENU_TOGGLE_KEYWORDS_POS+1 != VIEW_MENU_LENGTH
#error Inconsistency in defined lengths.
#endif

/* ISO-8859-1 MUST BE at the first position - see set_menus */
static GnomeUIInfo iso_charset_menu[] = {
#define ISO_CHARSET_1_POS 0
  GNOMEUIINFO_ITEM_NONE( N_ ("_Western (ISO-8859-1)"), NULL, iso_1_cb),
#define ISO_CHARSET_15_POS 1
  GNOMEUIINFO_ITEM_NONE( N_ ("W_estern (ISO-8859-15)"), NULL, iso_15_cb),
#define ISO_CHARSET_2_POS 2
  GNOMEUIINFO_ITEM_NONE( N_ ("_Central European (ISO-8859-2)"), NULL,iso_2_cb),
#define ISO_CHARSET_3_POS 3
  GNOMEUIINFO_ITEM_NONE( N_ ("_South European (ISO-8859-3)"), NULL, iso_3_cb),
#define ISO_CHARSET_13_POS 4
  GNOMEUIINFO_ITEM_NONE( N_ ("_Baltic (ISO-8859-13)"), NULL, iso_13_cb),
#define ISO_CHARSET_5_POS 5
  GNOMEUIINFO_ITEM_NONE( N_ ("Cy_rillic (ISO-8859-5)"), NULL, iso_5_cb),
#define ISO_CHARSET_8_POS 6
  GNOMEUIINFO_ITEM_NONE( N_ ("_Hebrew (ISO-8859-8)"), NULL, iso_8_cb),
#define ISO_CHARSET_9_POS 7
  GNOMEUIINFO_ITEM_NONE( N_ ("_Turkish (ISO-8859-9)"), NULL, iso_9_cb),
#define ISO_CHARSET_14_POS 8
  GNOMEUIINFO_ITEM_NONE( N_ ("Ce_ltic (ISO-8859-14)"), NULL, iso_14_cb),
#define KOI8_R_POS 9
  GNOMEUIINFO_ITEM_NONE( N_ ("Ru_ssian (KOI8-R)"), NULL, koi8_r_cb),
#define KOI8_U_POS 10
  GNOMEUIINFO_ITEM_NONE( N_ ("_Ukrainian (KOI8-U)"), NULL, koi8_u_cb),
  GNOMEUIINFO_END
};

/* the same sequence as in iso_charset_menu; the array stores charset name
   included in the MIME type information.
 */
static gchar* iso_charset_names[] = {
   "ISO-8859-1",
   "ISO-8859-15",
   "ISO-8859-2",
   "ISO-8859-3",
   "ISO-8859-13",
   "ISO-8859-5",
   "ISO-8859-8",
   "ISO-8859-9",
   "ISO-8859-14",
   "KOI8-R"
};

typedef struct {
      gchar * name;
      guint length; 
} headerMenuDesc;


#define CASE_INSENSITIVE_NAME
#define PRESERVE_CASE TRUE
#define OVERWRITE_CASE FALSE


headerMenuDesc headerDescs[] = { {"from", 3}, {"to", 3}, {"subject",2},
				 {"cc", 3}, {"bcc",  3}, {"fcc",    2},
				 {"replyto", 3}, {"attachments", 4},
				 {"comments", 2}, {"keywords",2}};

static GnomeUIInfo iso_menu[] = {
   GNOMEUIINFO_RADIOLIST(iso_charset_menu),
   GNOMEUIINFO_END
};


static GnomeUIInfo main_menu[] =
{
  GNOMEUIINFO_MENU_FILE_TREE(file_menu),
  GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
  GNOMEUIINFO_SUBTREE(N_("_Show"), view_menu),
  GNOMEUIINFO_SUBTREE(N_("_ISO Charset"), iso_menu),
  GNOMEUIINFO_END
};


/* the callback handlers */
static gint
close_window (GtkWidget * widget, gpointer data)
{
  balsa_sendmsg_destroy ((BalsaSendmsg*)data);
  return TRUE;
}

static gint
delete_event_cb (GtkWidget * widget, GdkEvent *e, gpointer data)
{
  g_message ("delete_event_cb(): Start.\n");
  g_message ("delete_event_cb(): Calling balsa_sendmsg_destroy().\n");
  balsa_sendmsg_destroy ((BalsaSendmsg*)data);
  g_message ("delete_event_cb(): Calling alias_free_addressbook().\n");
  alias_free_addressbook ();
  g_message ("delete_event_cb(): End.\n");
  return TRUE;
}

/* the balsa_sendmsg destructor; copies first the shown headers setting
   to the balsa_app structure.
*/
static void
balsa_sendmsg_destroy (BalsaSendmsg * bsm)
{
   int i;
   gchar newStr[ELEMENTS(headerDescs)*20]; /* assumes that longest header ID
					      has no more than 19 characters */

   g_assert(bsm != NULL);
   g_assert(ELEMENTS(headerDescs) == ELEMENTS(bsm->view_checkitems));
   
   newStr[0] = '\0';

   for(i=0; i<ELEMENTS(headerDescs); i++)
      if(GTK_CHECK_MENU_ITEM(bsm->view_checkitems[i])->active) {
	 strcat(newStr, headerDescs[i].name);
	 strcat(newStr, " ");
      }
   if(balsa_app.compose_headers) /* should never fail... */
      g_free(balsa_app.compose_headers);
   
   balsa_app.compose_headers = g_strdup(newStr);

   gtk_widget_destroy (bsm->window);
   if(balsa_app.debug) printf("balsa_sendmsg_destroy: Freeing bsm\n");
   g_free (bsm);

   if(bsm->orig_message) {
       if(bsm->orig_message->mailbox) 
	 libbalsa_mailbox_close(bsm->orig_message->mailbox);
       gtk_object_unref( GTK_OBJECT(bsm->orig_message) );
   }

   if(balsa_app.compose_email)
       balsa_exit();
}

/* remove_attachment - right mouse button callback */
static void
remove_attachment (GtkWidget * widget, GnomeIconList * ilist)
{
  gint num = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (ilist), 
						   "selectednumbertoremove"));
  gnome_icon_list_remove (ilist, num);
  gtk_object_remove_data (GTK_OBJECT (ilist), "selectednumbertoremove");
}

/* the menu is created on right-button click on an attachement */
static GtkWidget *
create_popup_menu (GnomeIconList * ilist, gint num)
{
  GtkWidget *menu, *menuitem;
  menu = gtk_menu_new ();
  menuitem = gtk_menu_item_new_with_label (_ ("Remove"));
  gtk_object_set_data (GTK_OBJECT (ilist), "selectednumbertoremove", 
		       GINT_TO_POINTER (num));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (remove_attachment), ilist);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);
  
  return menu;
}

/* select_icon --------------------------------------------------------------
   This signal is emitted when an icon becomes selected. If the event
   argument is NULL, then it means the icon became selected due to a
   range or rubberband selection. If it is non-NULL, it means the icon
   became selected due to an user-initiated event such as a mouse button
   press. The event can be examined to get this information.
*/

static void
select_attachment (GnomeIconList * ilist, gint num, GdkEventButton * event,
		   gpointer data)
{
   if(event==NULL) return;
   if (event->type == GDK_BUTTON_PRESS && event->button == 3)
      gtk_menu_popup (GTK_MENU (create_popup_menu (ilist, num)),
		      NULL, NULL, NULL, NULL,
		      event->button, event->time);
}

static void
add_attachment (GnomeIconList * iconlist, char *filename)
{
   /* FIXME: the path to the file must not be hardcoded */ 
	/* gchar *pix = gnome_pixmap_file ("balsa/attachment.png"); */
    gchar *pix = balsa_pixmap_finder( "balsa/attachment.png" );
	
   if( !check_if_regular_file( filename ) ) {
      /*c_i_r_f() will pop up an error dialog for us, so we need do nothing.*/
      return;
   }
   
   if( pix && check_if_regular_file( pix ) ) {
      gint pos;
      pos = gnome_icon_list_append (
	 iconlist, pix,
	 g_basename (filename));
      gnome_icon_list_set_icon_data (iconlist, pos, filename);
   } else {
      /*PKGW*/
      GtkWidget *box = gnome_message_box_new( 
	 _("The attachment pixmap (balsa/attachment.png) cannot be found.\n"
	  "This means you cannot attach any files.\n"), 
	GNOME_MESSAGE_BOX_ERROR, _("OK"), NULL );
     gtk_window_set_modal( GTK_WINDOW( box ), TRUE );
     gnome_dialog_run( GNOME_DIALOG( box ) );
     gtk_widget_destroy( GTK_WIDGET( box ) );
   }
}

static gint
check_if_regular_file (const gchar *filename)
{
  struct stat s;
  GtkWidget *msgbox;
  gchar *ptr = NULL;
  gint result = TRUE;

  if (stat (filename, &s)) {
     ptr = g_strdup_printf (_ ("Cannot get info on file '%s': %s\n"), 
			    filename, strerror (errno));
    result = FALSE;
  } else if (!S_ISREG (s.st_mode)) {
     ptr = g_strdup_printf (_ ("Attachment is not a regular file: '%s'\n"), 
			   filename);
    result = FALSE;
  }
  if (ptr) {
    msgbox = gnome_message_box_new (ptr, GNOME_MESSAGE_BOX_ERROR, 
				    _ ("Cancel"), NULL);
    g_free (ptr);
    gtk_window_set_modal (GTK_WINDOW (msgbox), TRUE);
    gnome_dialog_run (GNOME_DIALOG (msgbox));
  }
  return result;
}

static void
attach_dialog_ok (GtkWidget * widget, gpointer data)
{
  GtkFileSelection *fs;
  GnomeIconList *iconlist;
  gchar *filename, *dir, *p, *sel_file;
  GList * node;

  fs = GTK_FILE_SELECTION (data);
  iconlist = GNOME_ICON_LIST (gtk_object_get_user_data (GTK_OBJECT (fs)));

  sel_file = g_strdup(gtk_file_selection_get_filename(fs));
  dir = g_strdup(sel_file);
  p = strrchr(dir, '/');
  if (p)
      *(p + 1) = '\0';

  add_attachment (iconlist, sel_file);
  for(node = GTK_CLIST(fs->file_list)->selection; node; 
      node = g_list_next(node) ) {
      gtk_clist_get_text(GTK_CLIST(fs->file_list), 
			 GPOINTER_TO_INT(node->data), 0, &p);
      filename = g_strconcat(dir, p, NULL);
      if(strcmp(filename, sel_file) != 0)
	  add_attachment (iconlist, filename);
      /* do not g_free(filename) - the add_attachment arg is not const */
      /* g_free(filename); */
  }

  gtk_widget_destroy (GTK_WIDGET (fs));
  g_free(dir);
}

/* attach_clicked - menu and toolbar callback */
static gint
attach_clicked (GtkWidget * widget, gpointer data)
{
  GtkWidget *fsw;
  GnomeIconList *iconlist;
  GtkFileSelection *fs;
  BalsaSendmsg *bsm;

  bsm = data;

  iconlist = GNOME_ICON_LIST (bsm->attachments[1]);

  fsw = gtk_file_selection_new (_ ("Attach file"));
  gtk_object_set_user_data (GTK_OBJECT (fsw), iconlist);

  fs = GTK_FILE_SELECTION (fsw);
  gtk_clist_set_selection_mode(GTK_CLIST(fs->file_list), 
			       GTK_SELECTION_EXTENDED);

  gtk_signal_connect (GTK_OBJECT (fs->ok_button), "clicked",
		      (GtkSignalFunc) attach_dialog_ok,
		      fs);
  gtk_signal_connect_object (GTK_OBJECT (fs->cancel_button), "clicked",
		      (GtkSignalFunc) GTK_SIGNAL_FUNC(gtk_widget_destroy),
		      GTK_OBJECT(fsw) );

  gtk_window_set_wmclass (GTK_WINDOW (fsw), "file", "Balsa");
  gtk_widget_show (fsw);

  return TRUE;
}

/* attachments_add - attachments field D&D callback */
static void
attachments_add (GtkWidget * widget,
		 GdkDragContext * context,
		 gint x,
		 gint y,
		 GtkSelectionData * selection_data,
		 guint info,
		 guint32 time,
		 BalsaSendmsg * bsmsg)
{
  GList *names, *l;

  names = gnome_uri_list_extract_uris (selection_data->data);
  for (l = names; l; l = l->next)
    {
      char *name = l->data;
      if(g_strncasecmp(name , "file:",5) == 0)
	 add_attachment (GNOME_ICON_LIST (bsmsg->attachments[1]), name+5);
    }
  gnome_uri_list_free_strings (names);

  /* show attachment list */
   gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
     bsmsg->view_checkitems[MENU_TOGGLE_ATTACHMENTS_POS]), TRUE);
}

/* to_add - e-mail (To, From, Cc, Bcc) field D&D callback */
static void
to_add (GtkWidget * widget,
		 GdkDragContext * context,
		 gint x,
		 gint y,
		 GtkSelectionData * selection_data,
		 guint info,
		 guint32 time,
		 GnomeIconList * iconlist)
{

   if (strlen (gtk_entry_get_text (GTK_ENTRY(widget))) == 0)
   {
      gtk_entry_set_text (GTK_ENTRY(widget), selection_data->data);
      return;
   } else {
      gtk_entry_append_text (GTK_ENTRY(widget),",");
      gtk_entry_append_text (GTK_ENTRY(widget),selection_data->data);
   }
}


/*
 * static void create_string_entry()
 * 
 * Creates a gtk_label()/gtk_entry() pair.
 *
 * Input: GtkWidget* table       - Table to attach to.
 *        const gchar* label     - Label string.
 *        int y_pos              - position in the table.
 *      
 * Output: GtkWidget* arr[] - arr[0] will be the label widget.
 *                          - arr[1] will be the entry widget.
 */
static void
create_string_entry(GtkWidget* table, const gchar * label, int y_pos, 
		    GtkWidget* arr[])
{
   arr[0] = gtk_label_new (label);
   gtk_misc_set_alignment (GTK_MISC (arr[0]), 0.0, 0.5);
   gtk_misc_set_padding (GTK_MISC (arr[0]), GNOME_PAD_SMALL, GNOME_PAD_SMALL);
   gtk_table_attach (GTK_TABLE (table), arr[0], 0, 1, y_pos, y_pos+1,
		     GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);
   
   arr[1] = gtk_entry_new_with_max_length (2048);
   gtk_table_attach (GTK_TABLE (table), arr[1], 1, 2, y_pos, y_pos+1,
		     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 0, 0);
   gtk_signal_connect (GTK_OBJECT (arr[1]), "activate",
		     GTK_SIGNAL_FUNC (next_entrybox), arr[1]);
}

/*
 * static void create_email_entry()
 *
 * Creates a gtk_label()/gtk_entry() and button in a table for
 * e-mail entries, eg. To:.  It also sets up some callbacks in gtk.
 *
 * Input:  GtkWidget *table   - table to insert the widgets into.
 *         const gchar *label - label to use.
 *         int y_pos          - How far down in the table to put label.
 *         const gchar *icon  - icon for the button.
 * 
 * Output: GtkWidget *arr[]   - An array of GtkWidgets, as follows:
 *            arr[0]          - the label.
 *            arr[1]          - the entrybox.
 *            arr[2]          - the button.
 */
static void
create_email_entry(GtkWidget* table, const gchar * label, int y_pos, 
		   const gchar* icon, GtkWidget* arr[]) {

   gint *focus_counter;
   
   create_string_entry(table, label, y_pos, arr);

   arr[2] = gtk_button_new ();
   gtk_button_set_relief (GTK_BUTTON (arr[2]), GTK_RELIEF_NONE);
   GTK_WIDGET_UNSET_FLAGS (arr[2], GTK_CAN_FOCUS);
   gtk_container_add (GTK_CONTAINER (arr[2]),
		      gnome_stock_pixmap_widget(NULL, icon));
   gtk_table_attach (GTK_TABLE (table), arr[2], 2, 3, y_pos, y_pos+1,
		    0, 0, 0, 0);
   gtk_signal_connect(GTK_OBJECT(arr[2]), "clicked", 
		     GTK_SIGNAL_FUNC(address_book_cb),
		     (gpointer) arr[1]);
   gtk_signal_connect (GTK_OBJECT (arr[1]), "drag_data_received",
		      GTK_SIGNAL_FUNC (to_add), NULL);
   gtk_drag_dest_set (GTK_WIDGET (arr[1]), GTK_DEST_DEFAULT_ALL,
		     email_field_drop_types, ELEMENTS (email_field_drop_types),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
   /*
    * These will make sure that we know about every key pressed.
    */
#if 0
   gtk_signal_connect (GTK_OBJECT (arr[1]), "activate",
		     GTK_SIGNAL_FUNC (to_check), arr[1]);
#endif
   if (balsa_app.alias_find_flag)
   {
      gtk_signal_connect (GTK_OBJECT (arr[1]), "key-press-event",
                          GTK_SIGNAL_FUNC (key_pressed_cb), arr[1]);
      gtk_signal_connect (GTK_OBJECT (arr[1]), "button-press-event",
                          GTK_SIGNAL_FUNC (button_pressed_cb), arr[1]);
   }
   /*
    * And these make sure we rescan the input if the user plays
    * around.
    */
   gtk_signal_connect (GTK_OBJECT (arr[1]), "focus-out-event",
		       GTK_SIGNAL_FUNC (lost_focus_cb), arr[1]);
   gtk_signal_connect (GTK_OBJECT (arr[1]), "destroy",
		       GTK_SIGNAL_FUNC (destroy_cb), arr[1]);
   focus_counter = g_malloc(sizeof(gint));
   *focus_counter = 1;
   gtk_object_set_data(GTK_OBJECT(arr[1]), "focus_c", focus_counter);
}

/* create_info_pane 
   creates upper panel with the message headers: From, To, ... and 
   returns it.
*/
static GtkWidget *
create_info_pane (BalsaSendmsg * msg, SendType type)
{
  GtkWidget *sw;
  GtkWidget *table;
  GtkWidget *frame;

  table = gtk_table_new (10, 3, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 2);

  /* From: */
  create_email_entry(table, _("From:"),0,GNOME_STOCK_MENU_BOOK_BLUE,msg->from);
  /* To: */
  create_email_entry(table, _("To:"), 1, GNOME_STOCK_MENU_BOOK_RED, msg->to );
  gtk_signal_connect (GTK_OBJECT (msg->to[1]), "changed",
		      GTK_SIGNAL_FUNC (check_readiness), msg);

  /* Subject: */
  create_string_entry(table, _("Subject:"), 2, msg->subject);
  gtk_object_set_data(GTK_OBJECT (msg->to[1]), "next_in_line",msg->subject[1]); 

  /* cc: */
  create_email_entry(table, _("cc:"),3, GNOME_STOCK_MENU_BOOK_YELLOW, msg->cc);
  gtk_object_set_data(GTK_OBJECT (msg->subject[1]), "next_in_line",
	msg->cc[1]); 

  /* bcc: */
  create_email_entry(table,_("bcc:"), 4,GNOME_STOCK_MENU_BOOK_GREEN, msg->bcc);
  gtk_object_set_data(GTK_OBJECT (msg->cc[1]), "next_in_line", msg->bcc[1]); 

  /* fcc: */
  msg->fcc[0] = gtk_label_new (_("fcc:"));
  gtk_misc_set_alignment (GTK_MISC (msg->fcc[0]), 0.0, 0.5);
  gtk_misc_set_padding (GTK_MISC (msg->fcc[0]), GNOME_PAD_SMALL, GNOME_PAD_SMALL);
  gtk_table_attach (GTK_TABLE (table), msg->fcc[0], 0, 1, 5, 6,
		    GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

  msg->fcc[1] = gtk_combo_new ();
  gtk_combo_set_use_arrows (GTK_COMBO (msg->fcc[1]), 0);
  gtk_combo_set_use_arrows_always (GTK_COMBO (msg->fcc[1]), 0);
  gtk_object_set_data(GTK_OBJECT (msg->bcc[1]), "next_in_line", msg->fcc[1]); 
  
  if (balsa_app.mailbox_nodes) {
    GNode *walk;
    GList *glist = NULL;
    
    glist = g_list_append (glist, balsa_app.sentbox->name);
    glist = g_list_append (glist, balsa_app.draftbox->name);
    glist = g_list_append (glist, balsa_app.outbox->name);
    glist = g_list_append (glist, balsa_app.trash->name);
    walk = g_node_last_child (balsa_app.mailbox_nodes);
    while (walk) {
      glist = g_list_append (glist, ((MailboxNode *)((walk)->data))->name);
      walk = walk->prev;
    }
    gtk_combo_set_popdown_strings (GTK_COMBO (msg->fcc[1]), glist);
  }
  gtk_table_attach (GTK_TABLE (table), msg->fcc[1], 1, 3, 5, 6, 
      GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 0, 0);

  /* Reply To: */
  create_email_entry(table, _("Reply To:"), 6, GNOME_STOCK_MENU_BOOK_RED, 
		     msg->reply_to);
  gtk_object_set_data(GTK_OBJECT (msg->fcc[1]), "next_in_line",
		      msg->reply_to[1]); 

  /* Attachment list */
  msg->attachments[0] = gtk_label_new (_("Attachments:"));
  gtk_misc_set_alignment (GTK_MISC (msg->attachments[0]), 0.0, 0.5);
  gtk_misc_set_padding (GTK_MISC (msg->attachments[0]), GNOME_PAD_SMALL, GNOME_PAD_SMALL);
  gtk_table_attach (GTK_TABLE (table), msg->attachments[0], 0, 1, 7, 8,
		    GTK_FILL, GTK_FILL | GTK_SHRINK, 0, 0);

  /* create icon list */
  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);

  msg->attachments[1] = gnome_icon_list_new (100, NULL, FALSE);
  gtk_signal_connect (GTK_OBJECT (msg->window), "drag_data_received",
		      GTK_SIGNAL_FUNC (attachments_add), msg);
  gtk_drag_dest_set (GTK_WIDGET (msg->window), GTK_DEST_DEFAULT_ALL,
		     drop_types, ELEMENTS (drop_types),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

  gtk_widget_set_usize (msg->attachments[1], -1, 50);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (sw), msg->attachments[1]);
  gtk_container_add (GTK_CONTAINER (frame), sw);

  gtk_table_attach (GTK_TABLE (table), frame, 1, 3, 7, 8,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND | GTK_SHRINK,
		    0, 0);

  gtk_signal_connect (GTK_OBJECT (msg->attachments[1]), "select_icon",
		      GTK_SIGNAL_FUNC (select_attachment),
		      NULL);

  gnome_icon_list_set_selection_mode (GNOME_ICON_LIST (msg->attachments[1]), 
				      GTK_SELECTION_MULTIPLE);
  GTK_WIDGET_SET_FLAGS (GNOME_ICON_LIST (msg->attachments[1]), GTK_CAN_FOCUS);

  msg->attachments[2] = sw;
  msg->attachments[3] = frame;


  /* Comments: */
  create_string_entry(table, _("Comments:"), 8, msg->comments);

  /* Keywords: */
  create_string_entry(table, _("Keywords:"), 9, msg->keywords);

  gtk_widget_show_all( GTK_WIDGET(table) );

  return table;
}

/* create_text_area 
   Creates the text entry part of the compose window.
*/
static GtkWidget *
create_text_area (BalsaSendmsg * msg)
{
  GtkWidget *table;
  GtkWidget *hscrollbar;
  GtkWidget *vscrollbar;

  table = gtk_table_new (2, 2, FALSE);

  msg->text = gtk_text_new (NULL, NULL);
  gtk_text_set_editable (GTK_TEXT (msg->text), TRUE);
  gtk_text_set_word_wrap (GTK_TEXT (msg->text), TRUE);

  /*gtk_widget_set_usize (msg->text, 
			(82 * 7) + (2 * msg->text->style->klass->xthickness), 
			-1); */
  gtk_widget_show (msg->text);
  gtk_table_attach_defaults (GTK_TABLE (table), msg->text, 0, 1, 0, 1);
  hscrollbar = gtk_hscrollbar_new (GTK_TEXT (msg->text)->hadj);
  GTK_WIDGET_UNSET_FLAGS (hscrollbar, GTK_CAN_FOCUS);
  gtk_table_attach (GTK_TABLE (table), hscrollbar, 0, 1, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

  vscrollbar = gtk_vscrollbar_new (GTK_TEXT (msg->text)->vadj);
  GTK_WIDGET_UNSET_FLAGS (vscrollbar, GTK_CAN_FOCUS);
  gtk_table_attach (GTK_TABLE (table), vscrollbar, 1, 2, 0, 1,
		    GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  gtk_widget_show_all( GTK_WIDGET(table) );

  gtk_object_set_data(GTK_OBJECT (msg->reply_to[1]), "next_in_line",
		      msg->text); 

  return table;
}

/* continueBody ---------------------------------------------------------
   a short-circuit procedure for the 'Continue action'
   basically copies the text over to the entry field.
   NOTE that rbdy == NULL if message has no text parts.
*/
static void
continueBody(BalsaSendmsg *msg, LibBalsaMessage * message)
{
   GString *rbdy;

   libbalsa_message_body_ref (message);
   rbdy = content2reply (message, NULL); 
   if(rbdy) {
      gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, rbdy->str, 
		       strlen (rbdy->str));
      g_string_free (rbdy, TRUE);
   }

   if(!msg->charset) msg->charset = libbalsa_message_charset(message);
   libbalsa_message_body_unref (message);
}

/* quoteBody ------------------------------------------------------------
   quotes properly the body of the message
*/
static void 
quoteBody(BalsaSendmsg *msg, LibBalsaMessage * message, SendType type)
{
   GString *rbdy;
   gchar *str, *personStr;
   gchar *date;

   libbalsa_message_body_ref (message);
   
   personStr = (message->from && message->from->personal) ?
      message->from->personal : _("you");
   
      /* Should use instead something like:
       * 	strftime( buf, sizeof(buf), _("On %A %B %d %Y etc"),
       * 	                somedateparser(message-date)));
       * 	tmp = g_strdup_printf (buf);
       * so the date attribution can fully (and properly) translated.
       */
   if(message->date) {
     date = libbalsa_message_date_to_gchar (message, balsa_app.date_string);
     str = g_strdup_printf (_("On %s %s wrote:\n"), date, personStr);
     g_free(date);
   } else
      str = g_strdup_printf (_("%s wrote:\n"), personStr);


   gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, 
		    str, strlen (str));
   g_free (str);

   rbdy = content2reply (message, 
			 (type == SEND_REPLY || type == SEND_REPLY_ALL) ?
			 balsa_app.quote_str : NULL); 
   if(rbdy) {
      gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, rbdy->str, 
		       strlen (rbdy->str));
      g_string_free (rbdy, TRUE);
   }

   if(!msg->charset) msg->charset = libbalsa_message_charset(message);
   libbalsa_message_body_unref (message);
}

/* fillBody --------------------------------------------------------------
   fills the body of the message to be composed based on the given message.
   First quotes the original one and then adds the signature
*/
static void
fillBody(BalsaSendmsg *msg, LibBalsaMessage * message, SendType type)
{
   gchar *signature;
   gint pos = 0;

   gtk_editable_insert_text (GTK_EDITABLE(msg->text), "\n", 1, &pos);
   if (type != SEND_NORMAL && message) 
      quoteBody(msg, message, type);
   
  if ((signature = read_signature()) != NULL) {
     if ( ((type == SEND_REPLY || type == SEND_REPLY_ALL) && 
	   balsa_app.sig_whenreply) ||
	  ( (type == SEND_FORWARD) && balsa_app.sig_whenforward) ||
	  ( (type == SEND_NORMAL) && balsa_app.sig_sending) ) {
	gtk_text_insert   (GTK_TEXT(msg->text), NULL, NULL, NULL, "\n", 1);

	if( balsa_app.sig_separator && g_strncasecmp(signature, "--\n" ,3) && 
	   g_strncasecmp(signature, "-- \n" ,4) )
	   gtk_text_insert   (GTK_TEXT(msg->text), NULL, NULL, NULL, 
			      "-- \n", 4);
	gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, 
			 signature, strlen (signature));
     }
     g_free (signature);
  }
  gtk_editable_set_position( GTK_EDITABLE(msg->text), 0);
}


BalsaSendmsg *
sendmsg_window_new (GtkWidget * widget, LibBalsaMessage * message, SendType type)
{
  GtkWidget *window;
  GtkWidget *paned = gtk_vpaned_new ();
  gchar *newsubject = NULL, *tmp;
  BalsaSendmsg *msg = NULL;

  msg = g_malloc (sizeof (BalsaSendmsg));
  msg->font = NULL;
  msg->charset = NULL;


   if (balsa_app.alias_find_flag)
      alias_load_addressbook();
  
  switch (type)
    {
    case SEND_REPLY:
    case SEND_REPLY_ALL:
      window = gnome_app_new ("balsa", _ ("Reply to "));
      msg->orig_message = message;
      break;

    case SEND_FORWARD:
      window = gnome_app_new ("balsa", _ ("Forward message"));
      msg->orig_message = message;
      break;
      
    case SEND_CONTINUE:
      window = gnome_app_new ("balsa", _ ("Continue message"));
      msg->orig_message = message;
      break;

    default:
      window = gnome_app_new ("balsa", _ ("New message"));
      msg->orig_message = NULL;
      break;
    }
  if(message) { /* ref message so we don't loose it ieven if it is deleted */
      gtk_object_ref( GTK_OBJECT(message) );
                /* reference the original mailbox so we don't loose the
		   mail even if the mailbox is closed */
      if(message->mailbox)
	libbalsa_mailbox_open(message->mailbox, FALSE);
  }
  msg->window  = window;
  msg->type    = type;

  gtk_signal_connect (GTK_OBJECT(msg->window), "delete_event",
		      GTK_SIGNAL_FUNC (delete_event_cb), msg);
  gtk_signal_connect (GTK_OBJECT (msg->window), "destroy_event",
  		      GTK_SIGNAL_FUNC (delete_event_cb), msg);

  gnome_app_create_menus_with_data (GNOME_APP (window), main_menu, msg);
  gnome_app_create_toolbar_with_data (GNOME_APP (window), main_toolbar, msg);

  msg->ready_widgets[0] = file_menu[MENU_FILE_SEND_POS].widget;
  msg->ready_widgets[1] = main_toolbar[TOOL_SEND_POS  ].widget;
/* create the top portion with the to, from, etc in it */
  gtk_paned_add1 (GTK_PANED(paned), create_info_pane (msg, type));

  /* fill in that info: */

  /* To: */
  if (type == SEND_REPLY || type == SEND_REPLY_ALL)
    {
      LibBalsaAddress *addr = NULL;

      if (message->reply_to)
	addr = message->reply_to;
      else
	addr = message->from;

      tmp = libbalsa_address_to_gchar (addr);
      gtk_entry_set_text (GTK_ENTRY (msg->to[1]), tmp);
      g_free (tmp);
    }
    
  /* From: */
  {
    gchar *from;
    from = g_strdup_printf ("%s <%s>", balsa_app.address->personal, 
			    balsa_app.address->mailbox);
    gtk_entry_set_text (GTK_ENTRY (msg->from[1]), from);
    g_free (from);
  }

  /* Reply To */
  if(balsa_app.replyto) 
    gtk_entry_set_text (GTK_ENTRY (msg->reply_to[1]), balsa_app.replyto);

  /* Bcc: */
  {
    if (balsa_app.bcc)
      gtk_entry_set_text (GTK_ENTRY (msg->bcc[1]), balsa_app.bcc);
  }

  /* Fcc: */
  {
    if (type == SEND_CONTINUE && message->fcc_mailbox != NULL)
      gtk_entry_set_text (GTK_ENTRY(GTK_COMBO(msg->fcc[1])->entry),
                          message->fcc_mailbox);
  }

  /* Subject: */
  switch (type)
  {
     case SEND_REPLY:
     case SEND_REPLY_ALL:
	if (!message->subject)
	{
	   newsubject = g_strdup ("Re: ");
	   break;
	}
	
	tmp = message->subject;
	if (strlen (tmp) > 2 &&
	    toupper (tmp[0]) == 'R' &&
	    toupper (tmp[1]) == 'E' &&
	    tmp[2] == ':')
	{
	   newsubject = g_strdup(message->subject);
	   break;
	}
	   newsubject = g_strdup_printf ("Re: %s", message->subject);
	   break;
	
     case SEND_FORWARD:
	if (!message->subject) {
	   if (message->from && message->from->mailbox)
	      newsubject = g_strdup_printf ("Forwarded message from %s", 
					    message->from->mailbox);
	   else
	      newsubject = g_strdup ("Forwarded message");	  
	} else {
	   if (message->from && message->from->mailbox)
	      newsubject = g_strdup_printf ("[%s: %s]", 
					    message->from->mailbox, 
					    message->subject);
	   else
	      newsubject = g_strdup_printf ("Fwd: %s", message->subject);
	}
	break;
     default:
	break;
  }

  if (type == SEND_REPLY ||
      type == SEND_REPLY_ALL ||
      type == SEND_FORWARD)
    {
      gtk_entry_set_text (GTK_ENTRY (msg->subject[1]), newsubject);
      g_free (newsubject);
      newsubject = NULL;
    }

  if (type == SEND_CONTINUE) {
    if (message->to_list) {
      tmp = make_string_from_list (message->to_list);
      gtk_entry_set_text (GTK_ENTRY (msg->to[1]), tmp);
      g_free (tmp);
    }
    if (message->cc_list) {
      tmp = make_string_from_list (message->cc_list);
      gtk_entry_set_text (GTK_ENTRY (msg->cc[1]), tmp);
      g_free (tmp);
    }
    if (message->bcc_list) {
      tmp = make_string_from_list (message->bcc_list);
      gtk_entry_set_text (GTK_ENTRY (msg->bcc[1]), tmp);
      g_free (tmp);
    }
    if (message->subject)
      gtk_entry_set_text (GTK_ENTRY (msg->subject[1]), message->subject);
  }

  if (type == SEND_REPLY_ALL)
    {
      tmp = make_string_from_list (message->to_list);
      gtk_entry_set_text (GTK_ENTRY (msg->cc[1]), tmp);
      g_free (tmp);

      if (message->cc_list)
	{
	  gtk_entry_append_text (GTK_ENTRY (msg->cc[1]), ", ");

	  tmp = make_string_from_list (message->cc_list);
	  gtk_entry_append_text (GTK_ENTRY (msg->cc[1]), tmp);
	  g_free (tmp);
	}
    }

  gtk_paned_add2 (GTK_PANED (paned),create_text_area (msg));

  gnome_app_set_contents (GNOME_APP (window), paned);

  if(type==SEND_CONTINUE) 
     continueBody(msg, message);
  else
     fillBody(msg, message, type);

  /* set the toolbar so we are consistant with the rest of balsa */
  {
    GnomeDockItem *item;
    GtkWidget *toolbar;

    item = gnome_app_get_dock_item_by_name (GNOME_APP (window),
					    GNOME_APP_TOOLBAR_NAME);
    toolbar = gnome_dock_item_get_child (item);

    gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), balsa_app.toolbar_style);
  }

  /* set the menus  - and charset index - and display the window */
  /* FIXME: this will also reset the font, copying the text back and 
     forth which is sub-optimal.
  */
  set_menus(msg); 
  gtk_window_set_default_size ( 
     GTK_WINDOW(window), 
     (82 * 7) + (2 * msg->text->style->klass->xthickness), 35*12);
  gtk_window_set_wmclass (GTK_WINDOW (window), "compose", "Balsa");
  gtk_widget_show (window);


  if( type==SEND_NORMAL || type==SEND_FORWARD)  
    gtk_widget_grab_focus(msg->to[1]);
  else
    gtk_widget_grab_focus(msg->text);

   return msg;
}

static gchar *
read_signature (void)
{
  FILE *fp;
  size_t len;
  gchar *ret;

  if (balsa_app.signature_path == NULL
      || !(fp = fopen (balsa_app.signature_path, "r")))
    return NULL;
  len = readfile (fp, &ret);
  fclose (fp);
  if (len > 0 && ret[len - 1] == '\n')
     ret[len - 1] = '\0';

  return ret;
}

/* opens the load file dialog box, allows selection of the file and includes
   it at current point */
static void do_insert_file(GtkWidget *selector, GtkFileSelection* fs) {
   gchar* fname;
   guint cnt;
   gchar buf[4096];
   FILE* fl;
   BalsaSendmsg * bsmsg;

   bsmsg = (BalsaSendmsg*) gtk_object_get_user_data (GTK_OBJECT (fs));
   fname = gtk_file_selection_get_filename( GTK_FILE_SELECTION(fs) );

   cnt = gtk_editable_get_position( GTK_EDITABLE(bsmsg->text) );

   if(! (fl = fopen(fname,"rt")) ) {
      GtkWidget *box = gnome_message_box_new( 
	 _("Could not open the file.\n"), 
	 GNOME_MESSAGE_BOX_ERROR, _("Cancel"), NULL );
      gtk_window_set_modal( GTK_WINDOW( box ), TRUE );
      gnome_dialog_run( GNOME_DIALOG( box ) );
      gtk_widget_destroy( GTK_WIDGET( box ) );
   } else {
      gnome_appbar_push(balsa_app.appbar, _("Loading..."));

      gtk_text_freeze( GTK_TEXT(bsmsg->text) );
      gtk_text_set_point( GTK_TEXT(bsmsg->text), cnt); 
      while( (cnt=fread(buf, 1,sizeof(buf), fl)) > 0) {
	 if(balsa_app.debug)
	    printf("%s cnt: %d (max: %d)\n",fname, cnt, sizeof(buf));
	 gtk_text_insert(GTK_TEXT(bsmsg->text), bsmsg->font, 
			 NULL, NULL, buf, cnt);
      }
      if(balsa_app.debug)
	 printf("%s cnt: %d (max: %d)\n",fname, cnt, sizeof(buf));

      gtk_text_thaw( GTK_TEXT(bsmsg->text) );
      fclose(fl);
      gnome_appbar_pop(balsa_app.appbar);
   }
   /* g_free(fname); */
   gtk_widget_destroy(GTK_WIDGET(fs) );
   
}

static gint include_file_cb (GtkWidget *widget, BalsaSendmsg *bsmsg) {
   GtkWidget *file_selector;

   file_selector =  gtk_file_selection_new(_("Include file"));
   gtk_object_set_user_data (GTK_OBJECT (file_selector), bsmsg);

   gtk_file_selection_hide_fileop_buttons (GTK_FILE_SELECTION(file_selector));

   gtk_signal_connect (GTK_OBJECT(
      GTK_FILE_SELECTION(file_selector)->ok_button),
		       "clicked", GTK_SIGNAL_FUNC (do_insert_file), 
		       file_selector);
                             
   /* Ensure that the dialog box is destroyed when the user clicks a button. */
     
   gtk_signal_connect_object (GTK_OBJECT(
      GTK_FILE_SELECTION(file_selector)->cancel_button),  
			      "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
			      (gpointer) file_selector);
   
   /* Display that dialog */
   gtk_window_set_wmclass (GTK_WINDOW (file_selector), "file", "Balsa");
   gtk_widget_show (file_selector);

   return TRUE;
}


/* is_ready_to_send returns TRUE if the message is ready to send or 
   postpone. It tests currently only the "To" field
*/
static gboolean
is_ready_to_send(BalsaSendmsg * bsmsg) {
   gchar *tmp;
   size_t len;
   
   tmp = gtk_entry_get_text (GTK_ENTRY (bsmsg->to[1]));
   len = strlen (tmp);
   
   if (len < 1)		/* empty */
      return FALSE;
   
   if (tmp[len - 1] == '@')	/* this shouldn't happen */
      return FALSE;
   
   if (len < 4)
   {
      if (strchr (tmp, '@'))	/* you won't have an @ in an
				   address less than 4 characters */
	 return FALSE;
      
      /* assume they are mailing it to someone in their local domain */
   }
   return TRUE;
}

static void 
strip_chars(gchar *str, const gchar * char2strip) 
{
    gchar *ins = str;
    while(*str) {
	if(strchr(char2strip, *str) == NULL) 
	    *ins++ = *str;
	str++;
    }
    *ins = '\0';
}

/* bsmsg2message:
   creates Message struct based on given BalsaMessage
   stripping EOL chars is necessary - the GtkEntry fields can in principle 
   contain them. Such characters might screw up message formatting
   (consider moving this code to mutt part).
*/
static LibBalsaMessage *
bsmsg2message(BalsaSendmsg *bsmsg)
{
  LibBalsaMessage * message;
  LibBalsaMessageBody * body;
  gchar * tmp;
  gchar recvtime[50];
  struct tm *footime;

  g_assert(bsmsg != NULL);
  message = libbalsa_message_new ();

  message->from = libbalsa_address_new_from_string(gtk_entry_get_text 
					   (GTK_ENTRY (bsmsg->from[1])));

  message->subject = g_strdup (gtk_entry_get_text 
			       (GTK_ENTRY (bsmsg->subject[1])));
  strip_chars(message->subject,"\r\n");

  message->to_list = libbalsa_address_new_list_from_string (
     gtk_entry_get_text (GTK_ENTRY (bsmsg->to[1])));
  message->cc_list = libbalsa_address_new_list_from_string (
     gtk_entry_get_text (GTK_ENTRY (bsmsg->cc[1])));
  message->bcc_list = libbalsa_address_new_list_from_string (
     gtk_entry_get_text (GTK_ENTRY (bsmsg->bcc[1])));
  
  if( (tmp = gtk_entry_get_text (GTK_ENTRY (bsmsg->reply_to[1]))) != NULL &&
      strlen(tmp)>0) 
     message->reply_to = libbalsa_address_new_from_string(tmp);

  if (bsmsg->orig_message != NULL && 
      !GTK_OBJECT_DESTROYED(bsmsg->orig_message) ) {
    message->references = g_strdup (bsmsg->orig_message->message_id);
    
    footime = localtime (&bsmsg->orig_message->date);
    strftime (recvtime, sizeof (recvtime), "%a, %b %d, %Y at %H:%M:%S %z", footime);
    message->in_reply_to = g_strconcat (bsmsg->orig_message->message_id, 
                                        "; from ", 
                                        bsmsg->orig_message->from->mailbox, 
                                        " on ", 
                                        recvtime, 
                                        NULL);
  }
  
  body = libbalsa_message_body_new (message);

  body->buffer = gtk_editable_get_chars(GTK_EDITABLE (bsmsg->text), 0,
					gtk_text_get_length (
					   GTK_TEXT (bsmsg->text)));
  if(balsa_app.wordwrap)
     libbalsa_wrap_string (body->buffer, balsa_app.wraplength);

  body->charset = g_strdup(bsmsg->charset);

  libbalsa_message_append_part (message, body);

  {				/* handle attachments */
    gint i;
    for (i = 0; i < GNOME_ICON_LIST (bsmsg->attachments[1])->icons; i++) {
      body = libbalsa_message_body_new (message);
	/* PKGW: This used to be g_strdup'ed. However, the original pointer 
	   was strduped and never freed, so we'll take it. */
      body->filename = (gchar *) 
	 gnome_icon_list_get_icon_data(GNOME_ICON_LIST(bsmsg->attachments[1]),
				      i);

      libbalsa_message_append_part (message, body);
    }
  }

  return message;
}

/* "send message" menu and toolbar callback */
static gint
send_message_cb (GtkWidget * widget, BalsaSendmsg * bsmsg)
{
  LibBalsaMessage *message;
  gchar *tmp;
  if(! is_ready_to_send(bsmsg)) return FALSE;

  libbalsa_set_charset(bsmsg->charset);
  if(balsa_app.debug) 
     fprintf(stderr, "sending with charset: %s\n", bsmsg->charset);

  message = bsmsg2message (bsmsg);

  tmp = gtk_entry_get_text (GTK_ENTRY(GTK_COMBO(bsmsg->fcc[1])->entry));
  if ( tmp )
    message->fcc_mailbox = g_strdup(tmp);
  else
    message->fcc_mailbox = NULL;


  if (libbalsa_message_send (message)) {
    if (bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL)
      {
      if (bsmsg->orig_message)
	    libbalsa_message_reply (bsmsg->orig_message);
      }
    else if (bsmsg->type == SEND_CONTINUE)
      {
        if (bsmsg->orig_message)
          {
            libbalsa_message_delete (bsmsg->orig_message);
            libbalsa_mailbox_commit_changes (bsmsg->orig_message->mailbox);
          }
      }
  }

  gtk_object_destroy (GTK_OBJECT(message));
  balsa_sendmsg_destroy (bsmsg);

  return TRUE;
}

/* "postpone message" menu and toolbar callback */
static gint
postpone_message_cb (GtkWidget * widget, BalsaSendmsg * bsmsg)
{
  LibBalsaMessage *message;

  message = bsmsg2message(bsmsg);

  if ((bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL)
      && bsmsg->orig_message)
     libbalsa_message_postpone (message, bsmsg->orig_message, 
				gtk_entry_get_text (
					GTK_ENTRY(GTK_COMBO(bsmsg->fcc[1])->entry)));
  else
     libbalsa_message_postpone (message, NULL, gtk_entry_get_text (
	GTK_ENTRY(GTK_COMBO(bsmsg->fcc[1])->entry)));

 if (bsmsg->type == SEND_CONTINUE && bsmsg->orig_message)
   {
     libbalsa_message_delete (bsmsg->orig_message);
     libbalsa_mailbox_commit_changes (bsmsg->orig_message->mailbox);
   }

  gtk_object_destroy (GTK_OBJECT(message));
  balsa_sendmsg_destroy (bsmsg);

  return TRUE;
}

/* very harsh print handler. Prints headers and the body only, as raw text
*/
static gint 
print_message_cb(GtkWidget * widget, BalsaSendmsg * bsmsg)
{

#ifdef GNOME_PRINT
#error this is not finished yet!
#define FNT_SZ 12
   GnomePrintFont * fnt;
   GnomePrintContext * prn = gnome_print_context_new(
      gnome_print_default_printer());

   fnt = gnome_print_find_font("Times", FNT_SZ);
   gnome_print_set_font(prn,fnt);


   gnome_print_context_close(prn);
   gnome_print_context_free (prn);
#else
   gchar* str;
   gchar* dest;
   FILE * lpr;

   dest = g_strdup_printf(balsa_app.PrintCommand.PrintCommand, "-");
   lpr  = popen(dest,"w");
   g_free(dest);

   if(!lpr) {
      GtkWidget* msgbox = gnome_message_box_new (
	 _("Cannot execute print command."), 
	 GNOME_MESSAGE_BOX_ERROR, 
	 _("Cancel"), NULL);
      gtk_window_set_modal (GTK_WINDOW (msgbox), TRUE);
      gnome_dialog_run (GNOME_DIALOG (msgbox));
   }

   str = gtk_editable_get_chars( GTK_EDITABLE(bsmsg->from[1]),0,-1);
   fprintf(lpr, "From   : %s\n",str);
   g_free(str);
   str = gtk_editable_get_chars( GTK_EDITABLE(bsmsg->to[1]),0,-1);
   fprintf(lpr, "To     : %s\n",str);
   g_free(str);
   str = gtk_editable_get_chars( GTK_EDITABLE(bsmsg->subject[1]),0,-1);
   fprintf(lpr, "Subject: %s\n",str);
   g_free(str);

   str = gtk_editable_get_chars( GTK_EDITABLE(bsmsg->text), 0, -1);
   fputs(str, lpr);
   g_free(str);
   fputs("\n\f",lpr);

   if(pclose(lpr)!=0) {
      GtkWidget* msgbox = gnome_message_box_new (
	 _("Error executing lpr"), GNOME_MESSAGE_BOX_ERROR, 
	 _("Cancel"), NULL);
      gtk_window_set_modal (GTK_WINDOW (msgbox), TRUE);
      gnome_dialog_run (GNOME_DIALOG (msgbox));
   }
#endif
   return TRUE;
}

static void
wrap_body_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{
   gint pos, dummy;
   gchar * the_text;

   pos = gtk_editable_get_position(GTK_EDITABLE(bsmsg->text));

   the_text = gtk_editable_get_chars (GTK_EDITABLE (bsmsg->text),0,-1);
   libbalsa_wrap_string(the_text, balsa_app.wraplength);

   gtk_text_freeze(GTK_TEXT(bsmsg->text));
   gtk_editable_delete_text(GTK_EDITABLE(bsmsg->text),0,-1);
   dummy = 0;
   gtk_editable_insert_text(GTK_EDITABLE(bsmsg->text),the_text,
			    strlen(the_text), &dummy);
   gtk_editable_set_position(GTK_EDITABLE(bsmsg->text), pos);
   gtk_text_thaw(GTK_TEXT(bsmsg->text));
   g_free(the_text);
}

static void
do_reflow (GtkText * txt, gint mode) {
   gint pos, dummy;
   gchar * the_text;

   pos = gtk_editable_get_position( GTK_EDITABLE(txt) );
   the_text = gtk_editable_get_chars ( GTK_EDITABLE(txt),0,-1);
   reflow_string(the_text, mode, &pos, balsa_app.wraplength);

   gtk_text_freeze(txt);
   gtk_editable_delete_text( GTK_EDITABLE(txt), 0,-1);
   dummy = 0;
   gtk_editable_insert_text( GTK_EDITABLE(txt),the_text, 
			     strlen(the_text), &dummy);
   gtk_editable_set_position( GTK_EDITABLE(txt), pos);
   gtk_text_thaw(txt);
   g_free(the_text);
}

static void
reflow_par_cb (GtkWidget * widget, BalsaSendmsg *bsmsg) {
   do_reflow(GTK_TEXT(bsmsg->text), 
	     gtk_editable_get_position(GTK_EDITABLE(bsmsg->text)) );
}

static void
reflow_body_cb (GtkWidget * widget, BalsaSendmsg *bsmsg) {
   do_reflow(GTK_TEXT(bsmsg->text), -1);
}

/* To field "changed" signal callback. */
static void
check_readiness(GtkEditable *w, BalsaSendmsg *msg) 
{
   gint i;
   gboolean state = is_ready_to_send(msg);

   for(i=0; i<ELEMENTS(msg->ready_widgets); i++)
      gtk_widget_set_sensitive(msg->ready_widgets[i], state);
}

/* toggle_entry auxiliary function for "header show/hide" toggle menu entries.
 */
static gint 
toggle_entry (BalsaSendmsg * bmsg, GtkWidget *entry[], int pos, int cnt)
{
   GtkWidget* parent;
   if( GTK_CHECK_MENU_ITEM(bmsg->view_checkitems[pos])->active) {
      while(cnt--)
	 gtk_widget_show( GTK_WIDGET(entry[cnt]) );
   } else {
      while(cnt--)
	 gtk_widget_hide( GTK_WIDGET(entry[cnt]) );
      
      /* force size recomputation if embedded in paned */
      parent = GTK_WIDGET(GTK_WIDGET(entry[0])->parent)->parent;
      if(parent)
	 gtk_paned_set_position(GTK_PANED(parent), -1);
   }
   return TRUE;
}

static gint toggle_to_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{return toggle_entry(bsmsg,bsmsg->to, MENU_TOGGLE_TO_POS,3); }

static gint toggle_from_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{return toggle_entry(bsmsg, bsmsg->from, MENU_TOGGLE_FROM_POS,3); }

static gint toggle_subject_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{return toggle_entry(bsmsg, bsmsg->subject, MENU_TOGGLE_SUBJECT_POS,2); }

static gint toggle_cc_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{return toggle_entry(bsmsg, bsmsg->cc, MENU_TOGGLE_CC_POS,3); }

static gint toggle_bcc_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{return toggle_entry(bsmsg, bsmsg->bcc,  MENU_TOGGLE_BCC_POS,3); }
static gint toggle_fcc_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{return toggle_entry(bsmsg, bsmsg->fcc, MENU_TOGGLE_FCC_POS,2); }
static gint toggle_reply_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{return toggle_entry(bsmsg, bsmsg->reply_to, MENU_TOGGLE_REPLY_POS,3); }
static gint toggle_attachments_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{ 
   return toggle_entry(bsmsg, bsmsg->attachments, MENU_TOGGLE_ATTACHMENTS_POS,
		       4);
}

static gint toggle_comments_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{return toggle_entry(bsmsg, bsmsg->comments, MENU_TOGGLE_COMMENTS_POS,2); }
static gint toggle_keywords_cb (GtkWidget * widget, BalsaSendmsg *bsmsg)
{return toggle_entry(bsmsg, bsmsg->keywords, MENU_TOGGLE_KEYWORDS_POS,2); }

/* set_menus:
   performs the initial menu setup: shown headers as well as correct
   message charset. Copies also the the menu pointers for further usage
   at the message close  - they would be overwritten if another compose
   window was opened simultaneously.
*/
static void
set_menus(BalsaSendmsg *msg)
{
   unsigned i;
   const gchar * charset = NULL;

   g_assert(ELEMENTS(headerDescs) == ELEMENTS(msg->view_checkitems));

   for(i=0; i<ELEMENTS(headerDescs); i++) {
      msg->view_checkitems[i] = view_menu[i].widget;
      if(libbalsa_find_word(headerDescs[i].name, balsa_app.compose_headers) ) {
	 /* show... (well, it has already been shown). */
	 gtk_check_menu_item_set_active(
	    GTK_CHECK_MENU_ITEM(view_menu[i].widget), TRUE );
      } else {
	 /* or hide... */
	 GTK_SIGNAL_FUNC(view_menu[i].moreinfo)(view_menu[i].widget,msg);
      }
   }
   /* set the charset:
      read from the preferences set up. If not found, 
      set to the 0th set.
   */
   charset = msg->charset ? msg->charset : balsa_app.charset;
   i = sizeof(iso_charset_names)/sizeof(iso_charset_names[0])-1;
   while( i>0 && g_strcasecmp (iso_charset_names[i], charset) !=0)
      i--;

   if(balsa_app.debug)
      printf("set_menus: Charset: %s idx %d\n", charset, i);
    
   if(i==0) 
      set_iso_charset(msg, 1, 0);
   else
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
	 iso_charset_menu[i].widget), TRUE);

   /* gray 'send' and 'postpone' */
   check_readiness(GTK_EDITABLE(msg->to[1]), msg);
}

/* hardcoded charset set :
   msg is the compose window, code is the iso-8859 character and
   idx - corresponding entry index in iso_charset_names and similar.
*/


static gint 
set_iso_charset(BalsaSendmsg *msg, gint code, gint idx) {
   guint point, txt_len;
   gchar* str, *font_name;
   
   if( ! GTK_CHECK_MENU_ITEM(iso_charset_menu[idx].widget)->active)
      return TRUE;

   msg->charset = iso_charset_names[idx];

   font_name = get_font_name(balsa_app.message_font, code);
   if(msg->font) gdk_font_unref(msg->font);

   if( !( msg->font = gdk_font_load (font_name)) ) {
      printf("Cannot find fond: %s\n", font_name);
      g_free(font_name);
      return TRUE;
   }
   if(balsa_app.debug) 
      fprintf(stderr,"loaded font with mask: %s\n", font_name);
   g_free(font_name);
   

   gtk_text_freeze( GTK_TEXT(msg->text) );
   point   = gtk_editable_get_position( GTK_EDITABLE(msg->text) ); 
   txt_len = gtk_text_get_length( GTK_TEXT(msg->text) );
   str     = gtk_editable_get_chars( GTK_EDITABLE(msg->text), 0, txt_len);
   
   gtk_text_set_point( GTK_TEXT(msg->text), 0);
   gtk_text_forward_delete ( GTK_TEXT(msg->text), txt_len); 
   
   gtk_text_insert(GTK_TEXT(msg->text), msg->font, NULL, NULL, str, txt_len);
   g_free(str);
   gtk_text_thaw( GTK_TEXT(msg->text) );

   gtk_editable_set_position( GTK_EDITABLE(msg->text), point);
   return FALSE;
}

static gint 
set_koi8_charset(BalsaSendmsg *msg, const gchar *code, gint idx) {
   guint point, txt_len;
   gchar* str, *koi_font_name, *iso_font_name, *font_name;
   
   if( ! GTK_CHECK_MENU_ITEM(iso_charset_menu[idx].widget)->active)
      return TRUE;

   msg->charset = iso_charset_names[idx];

   koi_font_name = get_koi_font_name(balsa_app.message_font, code);
   iso_font_name = get_font_name(balsa_app.message_font,1);
   
   font_name = (gchar*)g_malloc(strlen(koi_font_name)+strlen(iso_font_name)+2);
   sprintf(font_name,"%s,%s",koi_font_name,iso_font_name);
   g_free(koi_font_name);
   g_free(iso_font_name);
      
   if(msg->font)
     gdk_font_unref(msg->font);

   if( !( msg->font = gdk_fontset_load (font_name)) ) {
      printf("Cannot find font: %s\n", font_name);
      g_free(font_name);
      return TRUE;
   }

   if(balsa_app.debug) 
      fprintf(stderr,"Loaded font with mask: %s\n", font_name);

   g_free(font_name);
   

   gtk_text_freeze( GTK_TEXT(msg->text) );
   point   = gtk_editable_get_position( GTK_EDITABLE(msg->text) ); 
   txt_len = gtk_text_get_length( GTK_TEXT(msg->text) );
   str     = gtk_editable_get_chars( GTK_EDITABLE(msg->text), 0, txt_len);
   
   gtk_text_set_point( GTK_TEXT(msg->text), 0);
   gtk_text_forward_delete ( GTK_TEXT(msg->text), txt_len); 
   
   gtk_text_insert(GTK_TEXT(msg->text), msg->font , NULL, NULL, str, txt_len);
   g_free(str);
   gtk_text_thaw( GTK_TEXT(msg->text) );

   gtk_editable_set_position( GTK_EDITABLE(msg->text), point);
   return FALSE;

}

static gint iso_1_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_iso_charset(bsmsg,  1, ISO_CHARSET_1_POS); }
static gint iso_2_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_iso_charset(bsmsg,  2, ISO_CHARSET_2_POS); }
static gint iso_3_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_iso_charset(bsmsg,  3, ISO_CHARSET_3_POS); }
static gint iso_5_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_iso_charset(bsmsg,  5, ISO_CHARSET_5_POS); }
static gint iso_8_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_iso_charset(bsmsg,  8, ISO_CHARSET_8_POS); }
static gint iso_9_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_iso_charset(bsmsg,  9, ISO_CHARSET_9_POS); }
static gint iso_13_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_iso_charset(bsmsg, 13, ISO_CHARSET_13_POS); }
static gint iso_14_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_iso_charset(bsmsg, 14, ISO_CHARSET_14_POS); }
static gint iso_15_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_iso_charset(bsmsg, 15, ISO_CHARSET_15_POS); }
static gint koi8_r_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_koi8_charset(bsmsg, "r", KOI8_R_POS); }
static gint koi8_u_cb(GtkWidget* widget, BalsaSendmsg *bsmsg)
{return set_koi8_charset(bsmsg, "u", KOI8_U_POS); }

