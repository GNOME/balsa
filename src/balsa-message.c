/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "balsa-app.h"
#include "mailbackend.h"
#include "balsa-message.h"
#include "mime.h"
#include "misc.h"
#ifdef USE_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

#define BGLINKCOLOR "LightSteelBlue1"

static gchar tmp_file_name[PATH_MAX + 1];

static gint part_idx;
static gint part_nesting_depth;


typedef struct _BalsaSaveFileInfo BalsaSaveFileInfo;
struct _BalsaSaveFileInfo
  {
    GtkWidget *file_entry;
    Message *msg;
    BODY *body;
  };


BalsaSaveFileInfo *balsa_save_file_info_new (GtkWidget * widget, Message * message, BODY * body);
static void item_event (GnomeCanvasItem * item, GdkEvent * event, gpointer data);
static void save_MIME_part (GtkObject * o, BalsaSaveFileInfo *);

/* widget */
static void balsa_message_class_init (BalsaMessageClass * klass);
static void balsa_message_init (BalsaMessage * bmessage);
static void balsa_message_size_request (GtkWidget * widget, GtkRequisition * requisition);
static void balsa_message_size_allocate (GtkWidget * widget, GtkAllocation * allocation);

static void headers2canvas (BalsaMessage * bmessage, Message * message);
static void body2canvas (BalsaMessage * bmessage, Message * message);
static gboolean content2canvas (Message * message, GnomeCanvasGroup * group);

static void part2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group);
static void other2canvas (Message *, BODY * bdy, FILE * fp, GnomeCanvasGroup * group);
static void mimetext2canvas (Message *, BODY * bdy, FILE * fp, GnomeCanvasGroup * group);
static void video2canvas (Message *, BODY * bdy, FILE * fp, GnomeCanvasGroup * group);
static void multipart2canvas (Message *, BODY * bdy, FILE * fp, GnomeCanvasGroup * group);
static void message2canvas (Message *, BODY * bdy, FILE * fp, GnomeCanvasGroup * group);
static void image2canvas (Message *, BODY * bdy, FILE * fp, GnomeCanvasGroup * group);

static void application2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group);
static void audio2canvas (Message *, BODY * bdy, FILE * fp, GnomeCanvasGroup * group);

static gchar *save_mime_part (Message * message, BODY * body);

static gint key_pressed (GtkWidget *widget, GdkEventKey *event, gpointer callback_data);
static void button_pressed (GtkWidget *widget, GdkEventButton *event, gpointer callback_data);
/* static */

static GnomeCanvasClass *parent_class = NULL;

guint
balsa_message_get_type ()
{
  static guint balsa_message_type = 0;

  if (!balsa_message_type)
    {
      GtkTypeInfo balsa_message_info =
      {
	"BalsaMessage",
	sizeof (BalsaMessage),
	sizeof (BalsaMessageClass),
	(GtkClassInitFunc) balsa_message_class_init,
	(GtkObjectInitFunc) balsa_message_init,
	(GtkArgSetFunc) NULL,
	(GtkArgGetFunc) NULL,
	(GtkClassInitFunc) NULL
      };

      balsa_message_type = gtk_type_unique (gnome_canvas_get_type (), &balsa_message_info);
    }

  return balsa_message_type;
}


static void
balsa_message_class_init (BalsaMessageClass * klass)
{
  GtkWidgetClass *widget_class;

  widget_class = (GtkWidgetClass *) klass;

  widget_class->size_request = balsa_message_size_request;
  widget_class->size_allocate = balsa_message_size_allocate;


  parent_class = gtk_type_class (gnome_canvas_get_type ());
}

static void
balsa_message_init (BalsaMessage * bmessage)
{
  bmessage->message = NULL;
  bmessage->headers = NULL;
  bmessage->body = NULL;
  bmessage->message = NULL;
}

BalsaSaveFileInfo *
balsa_save_file_info_new (GtkWidget * widget, Message * message, BODY * body)
{
  BalsaSaveFileInfo *new;

  new = g_malloc (sizeof (BalsaSaveFileInfo));

  new->file_entry = widget;
  new->msg = message;
  new->body = body;

  return new;
}

static void
item_event (GnomeCanvasItem * item, GdkEvent * event, gpointer data)
{
  BalsaSaveFileInfo *info;

  GtkWidget *save_dialog;
  GtkWidget *file_entry;
  GtkWidget *label;

  GdkCursor *cursor = NULL;

  info = data;

  switch (event->type)
    {
    case GDK_BUTTON_PRESS:
      save_dialog = gnome_dialog_new (_ ("Save MIME Part"),
				      _ ("Save"), _ ("Cancel"), NULL);
      gnome_dialog_set_parent (GNOME_DIALOG (save_dialog), GTK_WINDOW (balsa_app.main_window));
      label = gtk_label_new( _("Please choose a filename to save this part of the message as:") );
      file_entry = gnome_file_entry_new ("Balsa_MIME_Saver",
					 _ ("Save MIME Part"));
      info->file_entry = file_entry;

      if (info->body->filename)
	{
	  gtk_entry_set_text (GTK_ENTRY (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (file_entry))), info->body->filename);
	}

      gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (save_dialog)->vbox), label, FALSE, FALSE, 10);
      gtk_widget_show (label);
      gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (save_dialog)->vbox), file_entry, FALSE, FALSE, 10);
      gtk_widget_show (file_entry);
      gnome_dialog_button_connect (GNOME_DIALOG (save_dialog), 0, save_MIME_part, info);
      gtk_window_set_modal (GTK_WINDOW (save_dialog), TRUE);
      gnome_dialog_run (GNOME_DIALOG (save_dialog));
      gtk_widget_destroy (save_dialog);
      save_dialog = NULL;
      break;

    case GDK_ENTER_NOTIFY:
      cursor = gdk_cursor_new (GDK_HAND2);
      gdk_window_set_cursor (GTK_LAYOUT (item->canvas)->bin_window, cursor);
      gdk_cursor_destroy (cursor);

      if (!GNOME_IS_CANVAS_IMAGE (item))
      gnome_canvas_item_set (item, "fill_color", "red", NULL);
      break;

    case GDK_LEAVE_NOTIFY:
      cursor = gdk_cursor_new (GDK_HAND1);
      gdk_window_set_cursor (GTK_LAYOUT (item->canvas)->bin_window, NULL);

      if (!GNOME_IS_CANVAS_IMAGE (item))
      gnome_canvas_item_set (item, "fill_color", "black", NULL);
      break;
    default:
      break;
    }
}

/* PKGW: right-click on a text area to save it. */
static void
text_event( GnomeCanvasItem * item, GdkEvent * event, gpointer data )
{
	BalsaSaveFileInfo *info;
	GtkWidget *save_dialog;
	GtkWidget *file_entry;
	GtkWidget *label;
	GdkCursor *cursor = NULL;

	info = (BalsaSaveFileInfo *) data;

	switch( event->type ) {
	case GDK_BUTTON_PRESS:
	        if( ((GdkEventButton*)event)->button != 3) break;
		save_dialog = gnome_dialog_new( _("Save Text"),
						_("Save"), _("Cancel"), NULL );
                gnome_dialog_set_parent (GNOME_DIALOG (save_dialog), GTK_WINDOW (balsa_app.main_window));
		label = gtk_label_new( _("Please choose a filename to save this part of the message as:") );
		file_entry = gnome_file_entry_new( "Balsa_MIME_Saver",
					 _("Save Text") );
		info->file_entry = file_entry;

		/* Sorry for the unclear code :-( */
		if( info->body->filename ) {
			gtk_entry_set_text( GTK_ENTRY( gnome_file_entry_gtk_entry( 
				GNOME_FILE_ENTRY( file_entry ))), info->body->filename );
		}


		gtk_widget_show( label );
		gtk_box_pack_start( GTK_BOX( GNOME_DIALOG( save_dialog )->vbox ), 
				    label, FALSE, FALSE, 10 );
		gtk_box_pack_start( GTK_BOX( GNOME_DIALOG( save_dialog )->vbox ), 
				    file_entry, FALSE, FALSE, 10 );
		gtk_widget_show( file_entry );
		gnome_dialog_button_connect( GNOME_DIALOG( save_dialog ), 0, save_MIME_part, info );
		gtk_window_set_modal( GTK_WINDOW( save_dialog ), TRUE );
		gnome_dialog_run( GNOME_DIALOG( save_dialog ) );
		gtk_widget_destroy( save_dialog );
		save_dialog = NULL;
		break;

	case GDK_ENTER_NOTIFY:
		cursor = gdk_cursor_new( GDK_HAND2 );
		gdk_window_set_cursor( GTK_LAYOUT( item->canvas )->bin_window, cursor );
		gdk_cursor_destroy( cursor );
		break;

	case GDK_LEAVE_NOTIFY:
		cursor = gdk_cursor_new( GDK_HAND1 );
		gdk_window_set_cursor( GTK_LAYOUT( item->canvas )->bin_window, NULL );
       		break;

	default:
		break;
	}
}
/* End PKGW */

static void
save_MIME_part (GtkObject * o, BalsaSaveFileInfo * info)
{
  gchar *filename;
  GtkWidget *file_entry = info->file_entry;
  char msg_filename[PATH_MAX + 1];
  STATE s;

  switch (info->msg->mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      {
	snprintf (msg_filename, PATH_MAX, "%s/%s", MAILBOX_LOCAL (info->msg->mailbox)->path, message_pathname (info->msg));
	s.fpin = fopen (msg_filename, "r");
	break;
      }
    case MAILBOX_IMAP:
    case MAILBOX_POP3:
      s.fpin = fopen (MAILBOX_IMAP (info->msg->mailbox)->tmp_file_path, "r");
      break;
    default:
      s.fpin = fopen (MAILBOX_LOCAL (info->msg->mailbox)->path, "r");
      break;
    }

  if (!s.fpin || ferror (s.fpin))
    {
      char msg[1024];
      GtkWidget *msgbox;

      snprintf (msg, 1023, _ (" Open of %s failed:%s "), msg_filename, strerror (errno));
      msgbox = gnome_message_box_new (msg, "Error", _ ("Ok"), NULL);
      gtk_window_set_modal (GTK_WINDOW (msgbox), TRUE);
      gnome_dialog_run (GNOME_DIALOG (msgbox));
      return;
    }
  filename = gtk_entry_get_text (GTK_ENTRY (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (file_entry))));
  s.prefix = 0;
  s.fpout = fopen (filename, "w");
  if (!s.fpout)
    {
      char msg[1024];
      GtkWidget *msgbox;

      snprintf (msg, 1023, _ (" Open of %s failed:%s "), filename, 
		strerror (errno));
      msgbox = gnome_message_box_new (msg, "Error", _ ("Ok"), NULL);
      gtk_window_set_modal (GTK_WINDOW (msgbox), TRUE);
      gnome_dialog_run (GNOME_DIALOG (msgbox));
      return;
    }
  fseek (s.fpin, info->body->offset, 0);
  mutt_decode_attachment (info->body, &s);
  fclose (s.fpin);
  fclose (s.fpout);
}

GtkWidget *
balsa_message_create (void)
{
  BalsaMessage *bmessage;
  
  bmessage = gtk_type_new (balsa_message_get_type ());

  gtk_signal_connect_after(GTK_OBJECT (bmessage), "key_press_event", GTK_SIGNAL_FUNC (key_pressed), NULL);
  gtk_signal_connect(GTK_OBJECT (bmessage), "button_press_event", GTK_SIGNAL_FUNC (button_pressed), NULL);
 
  return GTK_WIDGET (bmessage);
}

static void
balsa_message_size_request (GtkWidget * widget, GtkRequisition * requisition)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_MESSAGE (widget));
  g_return_if_fail (requisition != NULL);

  if (GTK_WIDGET_CLASS (parent_class)->size_request)
    (*GTK_WIDGET_CLASS (parent_class)->size_request) (widget, requisition
      );

  requisition->width = 200;
  requisition->height = 150;
}

static void
balsa_message_size_allocate (GtkWidget * widget, GtkAllocation * allocation)
{
  double x1, x2, y1, y2;
  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_MESSAGE (widget));
  g_return_if_fail (allocation != NULL);

  if (GTK_WIDGET_CLASS (parent_class)->size_allocate)
    (*GTK_WIDGET_CLASS (parent_class)->size_allocate) (widget, allocation);

  gnome_canvas_item_get_bounds (
				 GNOME_CANVAS_ITEM (GNOME_CANVAS_GROUP (
					      GNOME_CANVAS (widget)->root)),
				 &x1, &y1, &x2, &y2);

  gnome_canvas_set_scroll_region (GNOME_CANVAS (widget),
				  0,
				  0,
			  (x2 > allocation->width) ? x2 : allocation->width,
		       (y2 > allocation->height) ? y2 : allocation->height);
}

gint key_pressed (GtkWidget *widget, GdkEventKey *event, gpointer callback_data)
{

   int x;
   int y;

   double x2,x3;
   double y2,y3;

   if (event->keyval == GDK_Up) {
      gnome_canvas_get_scroll_offsets (GNOME_CANVAS(widget), &x, &y);
      gnome_canvas_scroll_to (GNOME_CANVAS(widget), x, y-15);
      return (TRUE);
   }
   if (event->keyval == GDK_Down) {
      gnome_canvas_get_scroll_offsets (GNOME_CANVAS(widget), &x, &y);
      gnome_canvas_scroll_to (GNOME_CANVAS(widget), x, y+15);
      return (TRUE);
   }
   if (event->keyval == GDK_Left) {
      gnome_canvas_get_scroll_offsets (GNOME_CANVAS(widget), &x, &y);
      gnome_canvas_scroll_to (GNOME_CANVAS(widget), x-15, y);
      return (TRUE);
   }
   if (event->keyval == GDK_Right) {
      gnome_canvas_get_scroll_offsets (GNOME_CANVAS(widget), &x, &y);
      gnome_canvas_scroll_to (GNOME_CANVAS(widget), x+15, y);
      return (TRUE);
   }
   if (event->keyval == GDK_Page_Up) {
      gnome_canvas_get_scroll_offsets (GNOME_CANVAS(widget), &x, &y);
      gnome_canvas_scroll_to (GNOME_CANVAS(widget), x, y-(GTK_WIDGET (widget))->allocation.height);
      return (TRUE);
   }
   if (event->keyval == GDK_Page_Down) {
      gnome_canvas_get_scroll_offsets (GNOME_CANVAS(widget), &x, &y);
      gnome_canvas_scroll_to (GNOME_CANVAS(widget), x, y+(GTK_WIDGET (widget))->allocation.height);
      return (TRUE);
   }
   if (event->keyval == GDK_Home) {
      gnome_canvas_get_scroll_region (GNOME_CANVAS(widget), &x2, &y2, &x3, &y3);
      gnome_canvas_get_scroll_offsets (GNOME_CANVAS(widget), &x, &y);
      gnome_canvas_scroll_to (GNOME_CANVAS(widget), x, 0);
      return (FALSE);
   }
   if (event->keyval == GDK_End) {
      gnome_canvas_get_scroll_region (GNOME_CANVAS(widget), &x2, &y2, &x3, &y3);
      gnome_canvas_get_scroll_offsets (GNOME_CANVAS(widget), &x, &y);
      gnome_canvas_scroll_to (GNOME_CANVAS(widget), x, y3);
      return (FALSE);
   }

   return (FALSE);
}

void button_pressed (GtkWidget *widget, GdkEventButton *event, gpointer callback_data)
{
  (*(GTK_WIDGET_CLASS (GTK_WIDGET(widget)->object.klass)->grab_focus))(widget);
}

void
balsa_message_clear (BalsaMessage * bmessage)
{
  g_return_if_fail (bmessage != NULL);

  if (bmessage->headers)
    {
      gtk_object_destroy (GTK_OBJECT (bmessage->headers));
      bmessage->headers = NULL;
    }
  if(bmessage->body) {
      gtk_object_destroy (GTK_OBJECT (bmessage->body));
      bmessage->body = NULL;
    }

  gnome_canvas_scroll_to (GNOME_CANVAS (bmessage), 0, 0);
}

void
balsa_message_set (BalsaMessage * bmessage,
		   Message * message)
{
  GtkAllocation *alloc = NULL;
  GnomeCanvasGroup *bm_group;
  double x1, x2, y1, y2;

  g_return_if_fail (bmessage != NULL);
  g_return_if_fail (message != NULL);

  if (bmessage->message == message)
    return;

  balsa_message_clear (bmessage);

  /* mark message as read; no-op if it was read so don't worry.
     and this is the right place to do the marking.
  */
  message_read(message);
  headers2canvas (bmessage, message);
  body2canvas (bmessage, message);
  bm_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (bmessage)->root);

  gnome_canvas_item_get_bounds (GNOME_CANVAS_ITEM (bm_group),
				&x1, &y1, &x2, &y2);

  alloc = &(GTK_WIDGET (bmessage)->allocation);
  gnome_canvas_set_scroll_region (GNOME_CANVAS (bmessage),
				  0,
				  0,
				  (x2 > alloc->width) ? x2 : alloc->width,
				  (y2 > alloc->height) ? y2 : alloc->height);

}

/* balsa_message_text_item:
   inserts text item testing, if requested font exists and falls back to
   standard font otherwise. GnomeCanvas shows nothing if the requested
   font does not exists.
*/

static GnomeCanvasItem *
balsa_message_text_item (const gchar * text, GnomeCanvasGroup * group, 
			 double x, double y, const gchar * font_name)
{
	GnomeCanvasItem *item;
	if(font_name) {
	   GdkFont *fnt = gdk_font_load(font_name);
	   if(fnt) gdk_font_unref(fnt);
	   else {
	      fprintf(stderr,"message/text:: font not found: %s\n", font_name);
	      font_name = balsa_app.message_font;
	   }	      
	} else font_name = balsa_app.message_font;

	item = gnome_canvas_item_new( group,
				      GNOME_TYPE_CANVAS_TEXT,
				      "x", x,
				      "y", y,
				      "anchor", GTK_ANCHOR_NW,
				      "font",  font_name,
				      "text", text, 
				      NULL );
	return item;
}

static GnomeCanvasItem *
balsa_message_text_item_set_bg (GnomeCanvasItem * item, GnomeCanvasGroup * group, gchar * color)
{
  double x1, x2, y1, y2;
  GnomeCanvasItem *new;

  gnome_canvas_item_get_bounds (item, &x1, &y1, &x2, &y2);

  new = gnome_canvas_item_new (group,
			       gnome_canvas_rect_get_type (),
			       "x1", x1,
			       "y1", y1,
			       "x2", x2,
			       "y2", y2,
			       "fill_color", color,
			       NULL);

  gnome_canvas_item_lower (new, 1);
  return new;
}

static double
next_part_height (GnomeCanvasGroup * group)
{
  double x1, x2, y1, y2;
  double r;

  gnome_canvas_item_get_bounds (GNOME_CANVAS_ITEM (group), &x1, &y1, &x2, &y2);

  r = y2 - y1;
  r += 25;

  return r;
}

static double
next_row_height (GnomeCanvasGroup * row[])
{
  double o, t;
  double x1, x2, y1, y2;

  gnome_canvas_item_get_bounds (GNOME_CANVAS_ITEM (row[0]), &x1, &y1, &x2, &y2);
  o = y2 - y1;

  gnome_canvas_item_get_bounds (GNOME_CANVAS_ITEM (row[1]), &x1, &y1, &x2, &y2);
  t = y2 - y1;

  return (o > t) ? o : t;
}

static void
add_header_glist(const char * header, const gchar* label, 
		 GList * list, GnomeCanvasGroup *row[]) 
{
  GnomeCanvasItem *item;
  GnomeCanvasItem *data;
  gchar * value;
  double next_height;

  if (list && (balsa_app.shown_headers == HEADERS_ALL || 
		find_word(header, balsa_app.selected_headers) ) ) {
     next_height = next_row_height (row);
     item  = balsa_message_text_item (label, row[0], 0.0, next_height, NULL);
     value = make_string_from_list (list);
     if(balsa_app.browse_wrap)
	wrap_string(value, balsa_app.wraplength-15);
     data  = balsa_message_text_item (value, row[1], 0.0, next_height, NULL);
     g_free(value);
  }
}

static void
add_header_gchar(const char * header, const gchar * label, 
		 const gchar * value, GnomeCanvasGroup *row[]) 
{
  GnomeCanvasItem *item;
  GnomeCanvasItem *data;
  double next_height;

  g_assert(label != NULL && header != NULL);

  if (value && (balsa_app.shown_headers == HEADERS_ALL || 
		find_word(header, balsa_app.selected_headers) ) ) {
     next_height = next_row_height (row);
     item = balsa_message_text_item (label, row[0], 0.0, next_height, NULL);
     data = balsa_message_text_item (value, row[1], 0.0, next_height, NULL);
  }
}

static void
headers2canvas (BalsaMessage * bmessage, Message * message)
{
  double x1, x2, y1, y2;
  GList *p, * lst;
  gchar **pair, *hdr;
  GnomeCanvasGroup *bm_root;
  GnomeCanvasGroup *row[2];

  if(balsa_app.shown_headers == HEADERS_NONE)
     return;

  bm_root = GNOME_CANVAS_GROUP (GNOME_CANVAS (bmessage)->root);
  
  bmessage->headers =
    GNOME_CANVAS_GROUP (gnome_canvas_item_new (bm_root,
					       GNOME_TYPE_CANVAS_GROUP,
					       "x", (double) 10.0,
					       "y", (double) 10.0,
					       NULL));

  row[0] = GNOME_CANVAS_GROUP (gnome_canvas_item_new (bmessage->headers,
						    GNOME_TYPE_CANVAS_GROUP,
						      "x", (double) 0.0,
						      "y", (double) 0.0,
						      NULL));
  row[1] = GNOME_CANVAS_GROUP (gnome_canvas_item_new (bmessage->headers,
						    GNOME_TYPE_CANVAS_GROUP,
						      "x", (double) 0.0,
						      "y", (double) 0.0,
						      NULL));
  add_header_gchar("date", _("Date:"), message->date, row); 

  if (message->from) {
     gchar *from = address_to_gchar(message->from);
     add_header_gchar("from", _("From:"), from, row);
     g_free (from);
  }

  add_header_glist("to",  _("To:"),  message->to_list,  row);
  add_header_glist("cc",  _("Cc:"),  message->cc_list,  row);
  add_header_glist("bcc", _("Bcc:"), message->bcc_list, row); 

  if(message->fcc_mailbox)
     add_header_gchar("fcc" , _("Fcc:"),     message->fcc_mailbox->name, row);
  add_header_gchar("subject", _("Subject:"), message->subject,           row);

  /* remaining user headers */
  lst = message_user_hdrs(message);
  for(p = g_list_first(lst); p; p = g_list_next(p) ) {
      pair = p->data;
      if(balsa_app.browse_wrap)
	  wrap_string(pair[1], balsa_app.wraplength-strlen(pair[0])-1);
      hdr = g_strconcat(pair[0], ":", NULL);
      add_header_gchar(pair[0], hdr, pair[1],  row);
      g_free(hdr);
      g_strfreev(pair);
  }
  g_list_free(lst);

  gnome_canvas_item_get_bounds (GNOME_CANVAS_ITEM (row[0]), &x1, &y1, &x2, &y2);
  gnome_canvas_item_move (GNOME_CANVAS_ITEM (row[1]), x2 - x1 + 25, 0.0);
}


static void
body2canvas (BalsaMessage * bmessage, Message * message)
{
  GnomeCanvasGroup *bm_root;
  double x1, x2, y1, y2;

  bm_root = GNOME_CANVAS_GROUP (GNOME_CANVAS (bmessage)->root);

  if(bmessage->headers) {
     gnome_canvas_item_get_bounds (
	GNOME_CANVAS_ITEM (bmessage->headers), &x1, &y1, &x2, &y2);
     y1 = (y2 - y1) + 15;
  } else {
     y1 = 0;
  }

  bmessage->body =
    GNOME_CANVAS_GROUP (gnome_canvas_item_new (bm_root,
					       GNOME_TYPE_CANVAS_GROUP,
					       "x", (double) 10.0,
					       "y", (double) y1,
					       NULL));

  message_body_ref (message);
  content2canvas (message, bmessage->body);
  message_body_unref (message);
}

/* error_part:
   called when processing a part failed
*/
static void
error_part(GnomeCanvasGroup * group, gchar * msg)
{
    GnomeCanvasItem *link;
		   
    link = balsa_message_text_item(msg, group, 0.0, next_part_height(group),
				   NULL);
    balsa_message_text_item_set_bg( link, group, BGLINKCOLOR );
    g_free(msg);
}

static void
other2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  STATE s;
  gchar *text;
  size_t alloced;

  fseek (fp, bdy->offset, 0);
  s.fpin = fp;
  mutt_mktemp (tmp_file_name);

  if( (s.fpout = fopen (tmp_file_name, "r+")) == NULL) {
      error_part(group, 
		 g_strdup_printf(
		     _("other part: error writing to temporary file %s: %s"),
		     tmp_file_name, strerror(errno)));
      return;
  }
  
  s.prefix = '\0';
  mutt_decode_attachment (bdy, &s);
  fflush (s.fpout);
  alloced = readfile (s.fpout, &text);

  balsa_message_text_item (text, group, 0.0, next_part_height (group), NULL);

  g_free (text);
  fclose (s.fpout);
  unlink (tmp_file_name);
}

static void
audio2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  GnomeCanvasItem *item;
  BalsaSaveFileInfo *info;

  item = balsa_message_text_item ("--AUDIO--", group, 0.0, 
				  next_part_height (group), NULL);
  balsa_message_text_item_set_bg (item, group, BGLINKCOLOR);
  info = balsa_save_file_info_new (NULL, message, bdy);
  gtk_signal_connect (GTK_OBJECT (item), "event", GTK_SIGNAL_FUNC (item_event), info);

}

static void
application2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  GnomeCanvasItem *item;
  BalsaSaveFileInfo *info;


  /* create text */
  item = balsa_message_text_item ("--APPLICATION--", group, 0.0,
				  next_part_height (group), NULL);
  /* create item's background under text as created above */
  balsa_message_text_item_set_bg (item, group, BGLINKCOLOR);

  info = balsa_save_file_info_new (NULL, message, bdy);
  /* attach a signal to the background we created, so that we can change it
   * when the mouse is moved over it */
  gtk_signal_connect (GTK_OBJECT (item), "event",
		      GTK_SIGNAL_FUNC (item_event), info);

#if 0
  gchar link_bfr[128];
  PARAMETER *bdy_parameter = bdy->parameter;
  obstack_append_string (canvas_bfr,
			 _("<tr><td bgcolor=\"#f0f0f0\"> "
		       "You received an encoded file of type application/"));
  obstack_append_string (canvas_bfr, bdy->subtype);
  obstack_append_string (canvas_bfr, "<BR>");
  obstack_append_string (canvas_bfr, _("<P>The parameters of this message are:<BR>"));
  obstack_append_string (canvas_bfr, _("<table border=0><tr><th>Attribute</th><th>Value</th></tr>\n"));
  while (bdy_parameter)
    {
      obstack_append_string (canvas_bfr, "<tr><td>");
      obstack_append_string (canvas_bfr, bdy_parameter->attribute);
      obstack_append_string (canvas_bfr, "</td><td>");
      obstack_append_string (canvas_bfr, bdy_parameter->value);
      obstack_append_string (canvas_bfr, "</td></tr>");
      bdy_parameter = bdy_parameter->next;
    }
  obstack_append_string (canvas_bfr, "</table>");
  snprintf (link_bfr, 128,
	    "<A HREF=\"memory://%p:%p BODY\"> APPLICATION</A>"
	    "</td></tr>", message, bdy);
  obstack_append_string (canvas_bfr, link_bfr);
#endif
}

static void
image2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  GnomeCanvasItem *item;
  BalsaSaveFileInfo *info;

#ifndef USE_PIXBUF
  GdkImlibImage *im;
#else
  GdkPixbuf *pb;
#endif
  gchar *filename;

  if( (filename = save_mime_part (message, bdy)) == NULL) {
      error_part(group, 
		 g_strdup_printf(
		     _("image: error writing to temporary file: %s"),
		     tmp_file_name, strerror(errno)));
      return;
  }
#ifndef USE_PIXBUF
  im = gdk_imlib_load_image (filename);
  if(im) 
      item = gnome_canvas_item_new (group,
				    gnome_canvas_image_get_type (),
				    "image", im,
				    "x", 0.0,
				    "y", next_part_height (group),
				    "width", (double) im->rgb_width,
				    "height", (double) im->rgb_height,
				    "anchor", GTK_ANCHOR_NW,
				    NULL);
  else {
      item = balsa_message_text_item ("--IMAGE--", group, 0.0,
				      next_part_height (group), NULL);
      balsa_message_text_item_set_bg (item, group, BGLINKCOLOR);
  }
#else
  pb = gdk_pixbuf_new_from_file(filename);
  if(pb)
      item = gnome_canvas_item_new (group,
				    gnome_canvas_pixbuf_get_type(),
				    "pixbuf", pb,
				    "x", 0.0,
				    "y", next_part_height(group),
				    "width", (double) gdk_pixbuf_get_width(pb),
				    "height", (double) gdk_pixbuf_get_height(pb),
				    "anchor", GTK_ANCHOR_NW,
				    NULL);
  else {
      item = balsa_message_text_item ("--IMAGE--", group, 0.0,
				      next_part_height (group), NULL);
      balsa_message_text_item_set_bg (item, group, BGLINKCOLOR);
  }
#endif
  info = balsa_save_file_info_new (NULL, message, bdy);
  gtk_signal_connect (GTK_OBJECT (item), "event", GTK_SIGNAL_FUNC (item_event), info);

  unlink (filename);
}

static void
message2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  GnomeCanvasItem *item;
  BalsaSaveFileInfo *info;

  item = balsa_message_text_item ("--MESSAGE--", group, 0.0, 
				  next_part_height (group), NULL);
  balsa_message_text_item_set_bg (item, group, BGLINKCOLOR);
  info = balsa_save_file_info_new (NULL, message, bdy);
  gtk_signal_connect (GTK_OBJECT (item), "event", GTK_SIGNAL_FUNC (item_event), info);
}

static void
multipart2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  BODY *p;

  for (p = bdy->parts; p; p = p->next)
    {
      part2canvas (message, p, fp, group);
    }
}


static void
video2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  GnomeCanvasItem *item;
  BalsaSaveFileInfo *info;

  item = balsa_message_text_item ("--VIDEO--", group, 0.0, 
				  next_part_height (group), NULL);
  balsa_message_text_item_set_bg (item, group, BGLINKCOLOR);
  info = balsa_save_file_info_new (NULL, message, bdy);
  gtk_signal_connect (GTK_OBJECT (item), "event", GTK_SIGNAL_FUNC (item_event), info);
}

/* get_font_name returns iso8859 font name based on given font 
   wildcard 'base' and given character set encoding.
   Algorithm: copy max first 12 fields, cutting additionally 
   at most two last, if they are constant.
*/
/* the name should really be one_or_two_const_fields_to_end */
static gint 
two_const_fields_to_end(const gchar* ptr) {
   int cnt = 0;
   while(*ptr && cnt<3) {
      if(*ptr   == '*') return 0;
      if(*ptr++ == '-') cnt++;
   }
   return cnt<3;
}

gchar* 
get_font_name(const gchar* base, int code) {
   static gchar type[] ="iso8859";
   gchar *res;
   const gchar* ptr = base;
   int dash_cnt = 0, len;

   g_return_val_if_fail(base != NULL, NULL);
   g_return_val_if_fail(code >= 0,    NULL);

   while(*ptr && dash_cnt<13) {
      if(*ptr == '-') dash_cnt++;
      
      if(two_const_fields_to_end(ptr)) break;
      ptr++;
   }

   /* defense against a patologically short base font wildcard implemented
    * in the chunk below
    * extra space for dwo dashes and '\0' */
   len = ptr-base;
   /* if(dash_cnt>12) len--; */
   if(len<1) len = 1;
   res = (gchar*)g_malloc(len+sizeof(type)+3+(code>9?2:1));
   if(balsa_app.debug)
      fprintf(stderr,"base font name: %s and code: %d\n"
	      "mallocating %d bytes\n", base, code,
	      len+sizeof(type)+2+(code>9?2:1) );

   if(len>1) strncpy(res, base, len);
   else { strncpy(res, "*", 1); len = 1; } 

   sprintf(res+len,"-%s-%d", type, code);
   return res;
}   

gchar* 
get_koi_font_name(const gchar* base, const gchar* code) {
   static gchar type[] ="koi8";
   gchar *res;
   const gchar* ptr = base;
   int dash_cnt = 0, len;

   g_return_val_if_fail(base != NULL, NULL);
   g_return_val_if_fail(code != NULL, NULL);

   while(*ptr && dash_cnt<13) {
      if(*ptr == '-') dash_cnt++;
      
      if(two_const_fields_to_end(ptr)) break;
      ptr++;
   }

   /* defense against a patologically short base font wildcard implemented
    * in the chunk below
    * extra space for dwo dashes and '\0' */
   len = ptr-base;
   /* if(dash_cnt>12) len--; */
   if(len<1) len = 1;
   res = (gchar*)g_malloc(len+sizeof(type)+3+strlen(code));
   if(balsa_app.debug)
      fprintf(stderr,"base font name: %s and code: %s\n"
	      "mallocating %d bytes\n", base, code,
	      len+sizeof(type)+3 );

   if(len>1) strncpy(res, base, len);
   else { strncpy(res, "*", 1); len = 1; } 

   sprintf(res+len,"-%s-%s", type, code);
   return res;
}   


/* HELPER FUNCTIONS ----------------------------------------------- */
static gchar*
find_body_font(BODY * bdy) 
{
   gchar * font_name = NULL, *charset;

   if ((charset=mutt_get_parameter("charset", bdy->parameter)))
   {
      if(g_strncasecmp(charset,"iso-8859-",9) != 0 ) return NULL;
      font_name = get_font_name(balsa_app.message_font, atoi(charset+9));
   } 
   return font_name;
}


/* reflows a paragraph in given string. The paragraph to reflow is
determined by the cursor position. If mode is <0, whole string is
reflowed. Replace tabs with single spaces, squeeze neighboring spaces. 
Single '\n' replaced with spaces, double - retained. 
HQ piece of code, modify only after thorough testing.
*/
/* find_beg_and_end - finds beginning and end of a paragraph;
 *l will store the pointer to the first character of the paragraph,
 *u - to the '\0' or first '\n' character delimiting the paragraph.
 */
static
void find_beg_and_end(gchar *str, gint pos, gchar **l, gchar **u) 
{
   gint ln;

   *l = str + pos;

   while(*l>str && !(**l == '\n' && *(*l-1) == '\n') )
      (*l)--;
   if(*l+1<=str+pos && **l == '\n') (*l)++;

   *u = str + pos;
   ln = 0;
   while(**u && !(ln && **u == '\n') )
      ln = *(*u)++ == '\n';
   if(ln) (*u)--;
}

/* lspace - last was space, iidx - insertion index.  */
void 
reflow_string(gchar* str, gint mode, gint *cur_pos, int width) 
{
   gchar *l, *u, *sppos, *lnbeg, *iidx;
   gint lnl = 0, lspace = 0; // 1 -> skip leading spaces

   if(mode<0) {
      l = str; u = str + strlen(str);
   }
   else find_beg_and_end(str, *cur_pos, &l, &u);

   lnbeg = sppos = iidx = l;

   while(l<u) {
      if(lnl && *l == '\n') {
	 *(iidx-1) = '\n';
	 *iidx++ = '\n';
	 lspace = 1;
	 lnbeg = sppos = iidx;
      } else if(isspace((unsigned char)*l)) {
	 lnl = *l == '\n';
	 if(!lspace) {
	    sppos = iidx; 
	    *iidx++= ' ';
	 } else if(iidx-str<*cur_pos) (*cur_pos)--;
	 lspace = 1;
      } else {
	 lspace = 0; lnl = 0;
	 if(iidx-lnbeg>=width && lnbeg < sppos){
	    *sppos='\n';
	    lnbeg=sppos+1;
	 }
	 *iidx++ = *l;
      }
      l++;
   }
   /* job is done, shrink remainings */
   while( (*iidx++ =*u++) )
      ;
}

/* END OF HELPER FUNCTIONS ----------------------------------------------- */

static void
mimetext2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
	STATE s;
	gchar *ptr = 0;
	size_t alloced;
	BalsaSaveFileInfo *info;

	fseek( fp, bdy->offset, 0 );
	s.fpin = fp;
	s.prefix = '\0';

	s.flags = 0;

	mutt_mktemp( tmp_file_name );

	if( (s.fpout = fopen( tmp_file_name, "w+")) == NULL ) {
	    error_part(
		group, 
		g_strdup_printf(
		    _("text part: error writing to temporary file %s: %s"),
		    tmp_file_name, strerror(errno)));
	    return;
	}

	mutt_decode_attachment( bdy, &s );
	fflush( s.fpout );

	alloced = readfile( s.fpout, &ptr );

	if( ptr ) {
		gchar *linktext = "--TEXT--";
		gboolean showtext, showlink;

		if( g_strcasecmp( bdy->subtype, "html" ) == 0 ) {
			linktext = "--HTML--";
			showtext = FALSE;
			showlink = TRUE;
		} else {
			showtext = TRUE;
			showlink = FALSE;
		}

		if( bdy->filename == NULL )
		   bdy->filename = g_strdup( "textfile" );

		/* FIXME: this is leaked.  */
		info = balsa_save_file_info_new( NULL, message, bdy );
		
		/* create text and info, set them up */
		if( showlink ) {
		   GnomeCanvasItem *link;
		   
		   link = balsa_message_text_item( 
		      linktext, group, 0.0, next_part_height( group ), 
		      NULL);
		   balsa_message_text_item_set_bg( link, group, BGLINKCOLOR );
		   gtk_signal_connect( GTK_OBJECT( link ), "event", 
				       GTK_SIGNAL_FUNC( item_event ), info );
		}
		
		/* conditionally add the text item to the canvas */
		if( showtext ) {
		   GnomeCanvasItem *text;
		   gchar *font_name;
		   
		   font_name = find_body_font(bdy);
		   if(balsa_app.browse_wrap) 
		      wrap_string(ptr, balsa_app.wraplength);
		   text = balsa_message_text_item( 
		      ptr, group, 0.0, next_part_height (group), font_name );
		   if(font_name) g_free(font_name);

		   if( showlink == FALSE ) {
		      gtk_signal_connect( GTK_OBJECT( text ), "event", 
					  GTK_SIGNAL_FUNC( text_event ), 
					  info );
		   }
		}
		   
		g_free( ptr );
	}		
	
	fclose( s.fpout );
	unlink( tmp_file_name );
}


static void
part2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{

  switch (bdy->type)
    {
    case TYPEOTHER:
      if (balsa_app.debug)
	fprintf (stderr, "part: other\n");
      other2canvas (message, bdy, fp, group);
      break;
    case TYPEAUDIO:
      if (balsa_app.debug)
	fprintf (stderr, "part: audio\n");
      audio2canvas (message, bdy, fp, group);
      break;
    case TYPEAPPLICATION:
      if (balsa_app.debug){
	fprintf (stderr, "part: application\n");
	fprintf (stderr, "subtype: %s\n", bdy->subtype);
      }
      application2canvas(message, bdy, fp, group);
      break;
    case TYPEIMAGE:
      if (balsa_app.debug)
	fprintf (stderr, "part: image\n");
      image2canvas (message, bdy, fp, group);
      break;
    case TYPEMESSAGE:
      if (balsa_app.debug)
	fprintf (stderr, "part: message\n");
      message2canvas (message, bdy, fp, group);
      if (balsa_app.debug)
	      fprintf (stderr, "part end: multipart\n");
      break;
    case TYPEMULTIPART:
      if (balsa_app.debug)
	fprintf (stderr, "part: multipart\n");
      multipart2canvas (message, bdy, fp, group);
      if (balsa_app.debug)
	fprintf (stderr, "part end: multipart\n");
      break;
    case TYPETEXT:
      if (balsa_app.debug)
	fprintf (stderr, "part: text\n");
      mimetext2canvas (message, bdy, fp, group);
      break;
    case TYPEVIDEO:
      if (balsa_app.debug)
	fprintf (stderr, "part: video\n");
      video2canvas (message, bdy, fp, group);
      break;
    }

}

static gboolean
content2canvas (Message * message, GnomeCanvasGroup * group)
{
  GList *body_list;
  Body *body;
  FILE *msg_stream;
  gchar msg_filename[PATH_MAX];

  part_idx = 0;
  part_nesting_depth = 0;


  switch (message->mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      {
	snprintf (msg_filename, PATH_MAX, "%s/%s", MAILBOX_LOCAL (message->mailbox)->path, message_pathname  (message));
	msg_stream = fopen (msg_filename, "r");
	if (!msg_stream || ferror (msg_stream))
	  {
	    fprintf (stderr, _("Open of %s failed. Errno = %d, "),
		     msg_filename, errno);
	    perror (NULL);
	    return FALSE;
	  }
	break;
      }
    case MAILBOX_IMAP:
      msg_stream = fopen (MAILBOX_IMAP (message->mailbox)->tmp_file_path, "r");
      break;
    default:
      msg_stream = fopen (MAILBOX_LOCAL (message->mailbox)->path, "r");
      break;
    }

  body_list = message->body_list;
  while (body_list)
    {
      body = (Body *) body_list->data;
      part2canvas (message, body->mutt_body, msg_stream, group);
      body_list = g_list_next (body_list);
    }
  return TRUE;
}

static gchar *
save_mime_part (Message * message, BODY * body)
{
  static char msg_filename[PATH_MAX + 1];
  STATE s;

  msg_filename[0] = '\0';

  switch (message->mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      {
	snprintf (msg_filename, PATH_MAX, "%s/%s", MAILBOX_LOCAL (message->mailbox)->path, message_pathname (message));
	s.fpin = fopen (msg_filename, "r");
	break;
      }
    case MAILBOX_IMAP:
    case MAILBOX_POP3:
      s.fpin = fopen (MAILBOX_IMAP (message->mailbox)->tmp_file_path, "r");
      break;
    default:
      s.fpin = fopen (MAILBOX_LOCAL (message->mailbox)->path, "r");
      break;
    }

  mutt_mktemp (msg_filename);

  s.prefix = 0;
  if( (s.fpout = fopen (msg_filename, "w")) == NULL) {
      return NULL;
  }
  fseek (s.fpin, body->offset, 0);
  mutt_decode_attachment (body, &s);
  fclose (s.fpin);
  fclose (s.fpout);

  return msg_filename;
}
