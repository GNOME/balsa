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

#include "libmutt/mutt.h"

#define obstack_chunk_alloc g_malloc
#define obstack_chunk_free  g_free

#include <obstack.h>

#include <gtk-xmhtml/gtk-xmhtml.h>
#include <stdio.h>
#include <string.h>

#include "balsa-message.h"
#include "misc.h"

#define HTML_HEAD "<html><body bgcolor=#ffffff><p><tt>\n"
#define HTML_FOOT "</tt></p></body></html>\n"

/* mime */
gchar *content2html (Message * message);

/* widget */
static void balsa_message_class_init (BalsaMessageClass * klass);
static void balsa_message_init (BalsaMessage * bmessage);
static void balsa_message_size_request (GtkWidget * widget, GtkRequisition * requisition);
static void balsa_message_size_allocate (GtkWidget * widget, GtkAllocation * allocation);

/* static */

static GtkBinClass *parent_class = NULL;

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
	(GtkArgGetFunc) NULL
      };

      balsa_message_type = gtk_type_unique (gtk_bin_get_type (), &balsa_message_info);
    }

  return balsa_message_type;
}


static void
balsa_message_class_init (BalsaMessageClass * klass)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;

  object_class = (GtkObjectClass *) klass;
  widget_class = (GtkWidgetClass *) klass;
  container_class = (GtkContainerClass *) klass;

  parent_class = gtk_type_class (gtk_widget_get_type ());

  widget_class->size_request = balsa_message_size_request;
  widget_class->size_allocate = balsa_message_size_allocate;
}

char *urls [] = {
	"unknown", "named (...)", "jump (#...)",
	"file_local (file.html)", "file_remote (file://foo.bar/file)",
	"ftp", "http", "gopher", "wais", "news", "telnet", "mailto",
	"exec:foo_bar", "internal"
};

static void
balsa_message_handle_mime_part(GtkObject* xmhtml_widget, gpointer data)
{
  XmHTMLAnchorCallbackStruct *cbs = (XmHTMLAnchorCallbackStruct *) data;
  Message* message = 0;
  BODY*    body = 0;
  char*    ptr;
  int rc;

  printf ("click!\n");
  printf ("URLtype: %d, %s\n", cbs->url_type, urls [cbs->url_type]);
  printf ("line:    %d\n", cbs->line);
  printf ("href:    %s\n", cbs->href);
  printf ("target:  %s\n", cbs->target);
  printf ("rel:     %s\n", cbs->rel);
  printf ("rev:     %s\n", cbs->rev);
  printf ("title:   %s\n", cbs->title);
  printf ("doit:    %d\n", cbs->doit);
  printf ("visited: %d\n", cbs->visited);
  ptr = strchr(cbs->href,':');
  if (!ptr)
    {
      g_warning("Malformed URL '%s' in message\n", cbs->href);
      return;
    }
  rc = sscanf(ptr+3, "%p:%p", &message, &body);
  printf("sscanf returns %d, ptr = '%s', message is at %p, body is at %p\n", rc, ptr+3, message, body);

  printf("Save a '%s/%s'?\n", mime_content_type2str(body->type), body->subtype);
}



static void
balsa_message_init (BalsaMessage * bmessage)
{
  GTK_WIDGET_SET_FLAGS (bmessage, GTK_NO_WINDOW);

  bmessage->message = NULL;

  /* create the HTML widget to render the message */
  GTK_BIN (bmessage)->child = gtk_xmhtml_new ();
  gtk_widget_set_parent (GTK_BIN (bmessage)->child, GTK_WIDGET (bmessage));
  gtk_xmhtml_source (GTK_XMHTML (GTK_BIN (bmessage)->child), "");
  gtk_widget_show (GTK_BIN (bmessage)->child);
  gtk_widget_ref (GTK_BIN (bmessage)->child);
  gtk_signal_connect(GTK_OBJECT(GTK_BIN(bmessage)->child),
		     "activate",
		     GTK_SIGNAL_FUNC(balsa_message_handle_mime_part),
		     0);
}


static void
balsa_message_size_request (GtkWidget * widget,
			    GtkRequisition * requisition)
{
  GtkWidget *child;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_MESSAGE (widget));
  g_return_if_fail (requisition != NULL);

  child = GTK_BIN (widget)->child;

  requisition->width = 0;
  requisition->height = 0;

  if (GTK_WIDGET_VISIBLE (child))
    {
      gtk_widget_size_request (child, &child->requisition);
      requisition->width = child->requisition.width;
      requisition->height = child->requisition.height;
    }
}


static void
balsa_message_size_allocate (GtkWidget * widget,
			     GtkAllocation * allocation)
{
  GtkBin *bin;
  GtkWidget *child;
  GtkAllocation child_allocation;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_MESSAGE (widget));
  g_return_if_fail (allocation != NULL);

  bin = GTK_BIN (widget);
  widget->allocation = *allocation;

  child = bin->child;

  if (GTK_WIDGET_REALIZED (widget))
    {
      if (!GTK_WIDGET_VISIBLE (child))
	gtk_widget_show (child);

      child_allocation.x = allocation->x + GTK_CONTAINER (widget)->border_width;
      child_allocation.y = allocation->y + GTK_CONTAINER (widget)->border_width;
      child_allocation.width = allocation->width -
	2 * GTK_CONTAINER (widget)->border_width;
      child_allocation.height = allocation->height -
	2 * GTK_CONTAINER (widget)->border_width;

      gtk_widget_size_allocate (child, &child_allocation);
    }
}


GtkWidget *
balsa_message_new (void)
{
  BalsaMessage *bmessage;
  bmessage = gtk_type_new (balsa_message_get_type ());
  return GTK_WIDGET (bmessage);
}


void
balsa_message_clear (BalsaMessage * bmessage)
{
  g_return_if_fail (bmessage != NULL);

  bmessage->message = NULL;
  gtk_xmhtml_source (GTK_XMHTML (GTK_BIN (bmessage)->child), "");
}


void
balsa_message_set (BalsaMessage * bmessage,
		   Message * message)
{
  gchar *buff;

  g_return_if_fail (bmessage != NULL);
  g_return_if_fail (message != NULL);
  
  if (bmessage->message == message)
    return;
  message_body_unref (bmessage->message);
  bmessage->message = message;
  message_body_ref (bmessage->message);

  /* set message contents */
  buff = content2html (message);
  gtk_xmhtml_source (GTK_XMHTML (GTK_BIN (bmessage)->child), buff);
  message_body_unref (bmessage->message);
}

