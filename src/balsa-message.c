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

#include <gtk-xmhtml/gtk-xmhtml.h>
#include <stdio.h>
#include <string.h>
#include "balsa-message.h"
/*
   #define HTML_HEAD "<html><body bgcolor=#ffffff><pre>"
   #define HTML_FOOT "</pre></body></html>"
 */

#define HTML_HEAD "<html><body bgcolor=#ffffff><p><tt>\n"
#define HTML_FOOT "</tt></p></body></html>\n"

static GString *text2html (char *buff);


/* widget */
static void balsa_message_class_init (BalsaMessageClass * klass);
static void balsa_message_init (BalsaMessage * bmessage);
static void balsa_message_size_request (GtkWidget * widget, GtkRequisition * requisition);
static void balsa_message_size_allocate (GtkWidget * widget, GtkAllocation * allocation);



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


static void
balsa_message_init (BalsaMessage * bmessage)
{
  GTK_WIDGET_SET_FLAGS (bmessage, GTK_NO_WINDOW);

  bmessage->current_mailbox = NULL;
  bmessage->current_msgno = 0;


  /* create the HTML widget to render the message */
  GTK_BIN (bmessage)->child = gtk_xmhtml_new ();
  gtk_widget_set_parent (GTK_BIN (bmessage)->child, GTK_WIDGET (bmessage));
  gtk_xmhtml_source (GTK_XMHTML (GTK_BIN (bmessage)->child), "");
  gtk_widget_show (GTK_BIN (bmessage)->child);
  gtk_widget_ref (GTK_BIN (bmessage)->child);
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
balsa_message_new ()
{
  BalsaMessage *bmessage;
  bmessage = gtk_type_new (balsa_message_get_type ());
  return GTK_WIDGET (bmessage);
}


void
balsa_message_clear (BalsaMessage * bmessage)
{
  g_return_if_fail (bmessage != NULL);

  bmessage->current_mailbox = NULL;
  bmessage->current_msgno = 0;
  gtk_xmhtml_source (GTK_XMHTML (GTK_BIN (bmessage)->child), "");
}


void
balsa_message_set (BalsaMessage * bmessage,
		   Mailbox * mailbox,
		   glong msgno)
{
  g_return_if_fail (bmessage != NULL);

  bmessage->current_mailbox = mailbox;
  bmessage->current_msgno = msgno;

  /* set message contents */
  gtk_xmhtml_source (GTK_XMHTML (GTK_BIN (bmessage)->child), "Message Dude");
}



static GString *
text2html (char *buff)
{
  int i = 0, len = strlen (buff);
  GString *gs = g_string_new (NULL);


  for (i = 0; i < len; i++)
    {
      if (buff[i] == '\r' && buff[i + 1] == '\n' &&
	  buff[i + 2] == '\r' && buff[i + 3] == '\n')
	{
	  gs = g_string_append (gs, "</tt></p><p><tt>\n");
	  i += 3;
	}
      else if (buff[i] == '\r' && buff[i + 1] == '\n')
	{
	  gs = g_string_append (gs, "<br>\n");
	  i++;
	}
      else if (buff[i] == '\n' && buff[i + 1] == '\r')
	{
	  gs = g_string_append (gs, "<br>\n");
	  i++;
	}
      else if (buff[i] == '\n')
	{
	  gs = g_string_append (gs, "<br>\n");
	}
      else if (buff[i] == '\r')
	{
	  gs = g_string_append (gs, "<br>\n");
	}
      else if (buff[i] == ' ' && buff[i + 1] == ' ' && buff[i + 2] == ' ')
	{
	  gs = g_string_append (gs, "&nbsp; &nbsp;");
	  i += 2;
	}
      else if (buff[i] == ' ' && buff[i + 1] == ' ')
	{
	  gs = g_string_append (gs, "&nbsp; ");
	  i++;
	}
      else
	switch (buff[i])
	  {
	    /* for single spaces (not multiple (look above)) do *not*
	     * replace with a &nbsp; or lines will not wrap! bad
	     * thing(tm)
	     */
	  case '\t':
	    gs = g_string_append(gs, "&nbsp; &nbsp; &nbsp; &nbsp; ");
	    break;
	  case ' ':
	    gs = g_string_append (gs, " ");
	    break;
	  case '<':
	    gs = g_string_append (gs, "&lt;");
	    break;
	  case '>':
	    gs = g_string_append (gs, "&gt;");
	    break;
	  case '"':
	    gs = g_string_append (gs, "&quot;");
	    break;
	  case '&':
	    gs = g_string_append (gs, "&amp;");
	    break;
/* 
 * Weird stuff, but stuff that should be taken care of too
 * I might be missing something, lemme know?
 */
	  case '©':
	    gs = g_string_append (gs, "&copy;");
	    break;
	  case '®':
	    gs = g_string_append (gs, "&reg;");
	    break;
	  case 'à':
	    gs = g_string_append (gs, "&agrave;");
	    break;
	  case 'À':
	    gs = g_string_append (gs, "&Agrave;");
	    break;
	  case 'â':
	    gs = g_string_append (gs, "&acirc;");
	    break;
	  case 'ä':
	    gs = g_string_append (gs, "&auml;");
	    break;
	  case 'Ä':
	    gs = g_string_append (gs, "&Auml;");
	    break;
	  case 'Â':
	    gs = g_string_append (gs, "&Acirc;");
	    break;
	  case 'å':
	    gs = g_string_append (gs, "&aring;");
	    break;
	  case 'Å':
	    gs = g_string_append (gs, "&Aring;");
	    break;
	  case 'æ':
	    gs = g_string_append (gs, "&aelig;");
	    break;
	  case 'Æ':
	    gs = g_string_append (gs, "&AElig;");
	    break;
	  case 'ç':
	    gs = g_string_append (gs, "&ccedil;");
	    break;
	  case 'Ç':
	    gs = g_string_append (gs, "&Ccedil;");
	    break;
	  case 'é':
	    gs = g_string_append (gs, "&eacute;");
	    break;
	  case 'É':
	    gs = g_string_append (gs, "&Eacute;");
	    break;
	  case 'è':
	    gs = g_string_append (gs, "&egrave;");
	    break;
	  case 'È':
	    gs = g_string_append (gs, "&Egrave;");
	    break;
	  case 'ê':
	    gs = g_string_append (gs, "&ecirc;");
	    break;
	  case 'Ê':
	    gs = g_string_append (gs, "&Ecirc;");
	    break;
	  case 'ë':
	    gs = g_string_append (gs, "&euml;");
	    break;
	  case 'Ë':
	    gs = g_string_append (gs, "&Euml;");
	    break;
	  case 'ï':
	    gs = g_string_append (gs, "&iuml;");
	    break;
	  case 'Ï':
	    gs = g_string_append (gs, "&Iuml;");
	    break;
	  case 'ô':
	    gs = g_string_append (gs, "&ocirc;");
	    break;
	  case 'Ô':
	    gs = g_string_append (gs, "&Ocirc;");
	    break;
	  case 'ö':
	    gs = g_string_append (gs, "&ouml;");
	    break;
	  case 'Ö':
	    gs = g_string_append (gs, "&Ouml;");
	    break;
	  case 'ø':
	    gs = g_string_append (gs, "&oslash;");
	    break;
	  case 'Ø':
	    gs = g_string_append (gs, "&Oslash;");
	    break;
	  case 'ß':
	    gs = g_string_append (gs, "&szlig;");
	    break;
	  case 'ù':
	    gs = g_string_append (gs, "&ugrave;");
	    break;
	  case 'Ù':
	    gs = g_string_append (gs, "&Ugrave;");
	    break;
	  case 'û':
	    gs = g_string_append (gs, "&ucirc;");
	    break;
	  case 'Û':
	    gs = g_string_append (gs, "&Ucirc;");
	    break;
	  case 'ü':
	    gs = g_string_append (gs, "&uuml;");
	    break;
	  case 'Ü':
	    gs = g_string_append (gs, "&Uuml;");
	    break;

	  default:
	    gs = g_string_append_c (gs, buff[i]);
	    break;
	  }
    }


  return gs;
}
