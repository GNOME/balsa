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
  bmessage = gtk_type_new (balsa_message_get_type ());
  gnome_canvas_item_new (gnome_canvas_root (GNOME_CANVAS (bmessage)),
                               gnome_canvas_rect_get_type (),
	                            "x1", 0.0, "y1", 0.0,
                              "x2", 1000.0, "y2", 1000.0,
				   "fill_color", "white", NULL);
  
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
    (*GTK_WIDGET_CLASS (parent_class)->size_allocate) (widget, allocation) ;

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

  headers2canvas (bmessage, message);
}

void
headers2canvas (BalsaMessage * bmessage, Message * message)
{
  double max_width=0;
  double x1, x2, y1, y2;

  GnomeCanvasGroup * bm_root;
  GnomeCanvasItem *date_item=NULL, *date_data=NULL;
  
  bm_root = GNOME_CANVAS_GROUP (GNOME_CANVAS (bmessage)->root);

  bmessage->headers = 
    GNOME_CANVAS_GROUP (gnome_canvas_item_new (bm_root,
					       GNOME_TYPE_CANVAS_GROUP, NULL));
  
  /* create header items */

  if (message->date)
    {
      date_item = gnome_canvas_item_new (bmessage->headers,
					 GNOME_TYPE_CANVAS_TEXT,
					 "x", (double) 0.0,
					 "y", (double) 0.0,
					 "font", "fixed",
					 "text", "Date", NULL);

      gnome_canvas_item_get_bounds (date_item, &x1, &y1, &x2, &y2);

      max_width = (x2 - x1);
    }

  /* create data items (second column) */

  if (date_item)
    {
      date_data = gnome_canvas_item_new (bmessage->headers,
					 GNOME_TYPE_CANVAS_TEXT,
					 "x", (double) 0.0,
					 "y", (double) 0.0,
					 "font", "fixed",
					 "text", message->date,
					 NULL);

      gnome_canvas_item_move (date_data, max_width + 10, 0);
    }

}
  
   
