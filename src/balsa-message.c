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

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "balsa-message.h"
#include "balsa-message-item.h"
#include "misc.h"

/* widget */
static void balsa_message_class_init (BalsaMessageClass * klass);
static void balsa_message_init (BalsaMessage * bmessage);
static void balsa_message_size_request (GtkWidget * widget, GtkRequisition * requisition);
static void balsa_message_size_allocate (GtkWidget * widget, GtkAllocation * allocation);

void headers2canvas (BalsaMessage * bmessage, Message * message);

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
  bmessage->html = NULL;
}

GtkWidget *
balsa_message_new (void)
{
  BalsaMessage *bmessage;
  GtkStyle *style;

  gtk_widget_push_visual (gdk_imlib_get_visual ());
  gtk_widget_push_colormap (gdk_imlib_get_colormap ());
  bmessage = gtk_type_new (balsa_message_get_type ());
  gtk_widget_pop_visual ();
  gtk_widget_pop_colormap ();

  style = gtk_widget_get_style (GTK_WIDGET (bmessage));

  gdk_color_white (gdk_imlib_get_colormap (), &style->bg[GTK_STATE_NORMAL]);

  gtk_widget_set_style (GTK_WIDGET (bmessage), style);

  /*
     gnome_canvas_item_new (gnome_canvas_root (GNOME_CANVAS (bmessage)),
     gnome_canvas_rect_get_type (),
     "x1", 0.0, "y1", 0.0,
     "x2", 1000.0, "y2", 1000.0,
     "fill_color", "white", NULL);
   */
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
  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_MESSAGE (widget));
  g_return_if_fail (allocation != NULL);

  if (GTK_WIDGET_CLASS (parent_class)->size_allocate)
    (*GTK_WIDGET_CLASS (parent_class)->size_allocate) (widget, allocation);

  gnome_canvas_set_scroll_region (GNOME_CANVAS (widget), 0, 0, allocation->width, allocation->height);

}


void
balsa_message_set (BalsaMessage * bmessage,
		   Message * message)
{
  g_return_if_fail (bmessage != NULL);
  g_return_if_fail (message != NULL);

  if (bmessage->message == message)
    return;

  if (bmessage->headers)
    gtk_object_destroy (GTK_OBJECT (bmessage->headers));

  headers2canvas (bmessage, message);
}

void
headers2canvas (BalsaMessage * bmessage, Message * message)
{
  double max_width = 0;
  double text_height = 0;
  double next_height = 0;
  double x1, x2, y1, y2;

  GnomeCanvasGroup *bm_root;
  GnomeCanvasItem *date_item = NULL, *date_data = NULL;
  GnomeCanvasItem *to_item = NULL, *to_data = NULL;
  GnomeCanvasItem *cc_item = NULL, *cc_data = NULL;
  GnomeCanvasItem *from_item = NULL, *from_data = NULL;
  GnomeCanvasItem *subject_item = NULL, *subject_data = NULL;

  bm_root = GNOME_CANVAS_GROUP (GNOME_CANVAS (bmessage)->root);

  bmessage->headers =
    GNOME_CANVAS_GROUP (gnome_canvas_item_new (bm_root,
					       GNOME_TYPE_CANVAS_GROUP,
					       "x", (double) 0.0,
					       "y", (double) 0.0,
					       NULL));

  if (message->date)
    {
      date_item = gnome_canvas_item_new (bmessage->headers,
					 GNOME_TYPE_CANVAS_TEXT,
					 "x", (double) 0.0,
					 "y", (double) 0.0,
					 "anchor", GTK_ANCHOR_NW,
					 "font", "fixed",
					 "text", "Date:", NULL);

      gnome_canvas_item_get_bounds (date_item, &x1, &y1, &x2, &y2);

      max_width = (x2 - x1);
      
      if (text_height == 0)
	text_height = y2 - y1;

      next_height += text_height + 3;
    }

  if (message->to_list)
    {
      to_item = gnome_canvas_item_new (bmessage->headers,
				       GNOME_TYPE_CANVAS_TEXT,
				       "x", (double) 0.0,
				       "y", (double) next_height,
				       "anchor", GTK_ANCHOR_NW,
				       "font", "fixed",
				       "text", "To:", NULL);

      gnome_canvas_item_get_bounds (to_item, &x1, &y1, &x2, &y2);

      if ((x2 - x1) > max_width)
	max_width = (x2 - x1);

      if (text_height == 0)
	text_height = y2 - y1;

      next_height += text_height + 3;

    }

  if (message->cc_list)
    {
      cc_item = gnome_canvas_item_new (bmessage->headers,
				       GNOME_TYPE_CANVAS_TEXT,
				       "x", (double) 0.0,
				       "y", (double) next_height,
				       "anchor", GTK_ANCHOR_NW,
				       "font", "fixed",
				       "text", "Cc:", NULL);

      gnome_canvas_item_get_bounds (cc_item, &x1, &y1, &x2, &y2);
      
      if ((x2 - x1) > max_width)
	max_width = (x2 - x1);

      if (text_height == 0)
	text_height = y2 - y1;

      next_height += text_height + 3;
    }

  if (message->from)
    {
      from_item = gnome_canvas_item_new (bmessage->headers,
					 GNOME_TYPE_CANVAS_TEXT,
					 "x", (double) 0.0,
					 "y", (double) next_height,
					 "anchor", GTK_ANCHOR_NW,
					 "font", "fixed",
					 "text", "From:", NULL);

      gnome_canvas_item_get_bounds (from_item, &x1, &y1, &x2, &y2);

      if ((x2 - x1) > max_width)
	max_width = (x2 - x1);

      if (text_height == 0)
	text_height = y2 - y1;

      next_height += text_height + 3;
    }

  if (message->subject)
    {
      subject_item = gnome_canvas_item_new (bmessage->headers,
					    GNOME_TYPE_CANVAS_TEXT,
					    "x", (double) 0.0,
					    "y", (double) next_height,
					    "anchor", GTK_ANCHOR_NW,
					    "font", "fixed",
					    "text", "Subject:", NULL);

      gnome_canvas_item_get_bounds (subject_item, &x1, &y1, &x2, &y2);

      if ((x2 - x1) > max_width)
	max_width = (x2 - x1);

      if (text_height == 0)
	text_height = y2 - y1;

      next_height += text_height + 3;
    }

  next_height = 0;
  max_width += 50;

  if (date_item)
    {
      date_data = gnome_canvas_item_new (bmessage->headers,
					 GNOME_TYPE_CANVAS_TEXT,
					 "x", max_width,
					 "y", next_height,
					 "anchor", GTK_ANCHOR_NW,
					 "font", "fixed",
					 "text", message->date,
					 NULL);

      next_height += text_height + 3;
    }

  if (to_item)
    {
      to_data = gnome_canvas_item_new (bmessage->headers,
				       GNOME_TYPE_CANVAS_TEXT,
				       "x", (double) max_width,
				       "y", (double) next_height,
				       "anchor", GTK_ANCHOR_NW,
				       "font", "fixed",
				       "text",
				       make_string_from_list(message->to_list),
				       NULL);

      next_height += text_height + 3;
    }
  
  if (cc_item)
    {
      cc_data = gnome_canvas_item_new (bmessage->headers,
				       GNOME_TYPE_CANVAS_TEXT,
				       "x", max_width,
				       "y", next_height,
				       "anchor", GTK_ANCHOR_NW,
				       "font", "fixed",
				       "text",
				       make_string_from_list(message->cc_list),
				       NULL);
      
      next_height += text_height + 3;
    }

  if (from_item)
    {
      gchar tbuff[1024];
      
      if (message->from->personal)
	snprintf (tbuff, 1024, "%s <%s>",
		  message->from->personal,
		  message->from->mailbox);
      else
	snprintf (tbuff, 1024, "%s", message->from->mailbox);

      from_data = gnome_canvas_item_new (bmessage->headers,
					 GNOME_TYPE_CANVAS_TEXT,
					 "x", max_width,
					 "y", next_height,
					 "anchor", GTK_ANCHOR_NW,
					 "font", "fixed",
					 "text", tbuff, NULL);

      next_height += text_height + 3;
    }

  if (subject_item)
    {
      subject_data = gnome_canvas_item_new (bmessage->headers,
					    GNOME_TYPE_CANVAS_TEXT,
					    "x", max_width,
					    "y", next_height,
					    "anchor", GTK_ANCHOR_NW,
					    "font", "fixed",
					    "text", message->subject,
					    NULL);
      
      next_height += text_height + 3;
    }    
}
