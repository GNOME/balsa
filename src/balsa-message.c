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

#define HTML_HEAD "<html><body bgcolor=#ffffff><pre>"
#define HTML_FOOT "</pre></body></html>"


static GString *text2html (char *buff);


/* widget */
static void balsa_message_class_init (BalsaMessageClass * klass);
static void balsa_message_init (BalsaMessage * bmessage);
static void balsa_message_size_request (GtkWidget * widget,
					GtkRequisition * requisition);
static void balsa_message_size_allocate (GtkWidget * widget,
					 GtkAllocation * allocation);


/* debugging */
static void debug_mime_content (BODY * body);



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

  container_class->add = NULL;
  container_class->remove = NULL;
}

static void
balsa_message_init (BalsaMessage * bmessage)
{
  GTK_WIDGET_SET_FLAGS (bmessage, GTK_NO_WINDOW);

  bmessage->current_stream = NULL;
  bmessage->current_mesgno = 0;

  /* bring HTML widget to life with a little message for
   * now */
  GTK_BIN (bmessage)->child = gtk_xmhtml_new ();
  gtk_widget_set_parent (GTK_BIN (bmessage)->child, GTK_WIDGET (bmessage));
  gtk_xmhtml_source (GTK_XMHTML (GTK_BIN (bmessage)->child), "");
  gtk_widget_show (GTK_BIN (bmessage)->child);
  gtk_widget_ref (GTK_BIN (bmessage)->child);
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

  bmessage->current_stream = NULL;
  bmessage->current_mesgno = 0;
  gtk_xmhtml_source (GTK_XMHTML (GTK_BIN (bmessage)->child), "");
}

void
balsa_message_set (BalsaMessage * bmessage,
		   MAILSTREAM * stream,
		   glong mesgno)
{
  BODY *body;
  STRINGLIST *lines, *cur;
  gchar *c, *buff;
  GString *gs;

  g_return_if_fail (bmessage != NULL);
  g_return_if_fail (stream != NIL);

  bmessage->current_stream = stream;
  bmessage->current_mesgno = mesgno;

  lines = mail_newstringlist ();
  cur = lines;

  /* look into the parts of the message */
  mail_fetchstructure (stream, mesgno, &body);

  debug_mime_content (body);

  /* HTML header */
  buff = g_malloc (strlen (HTML_HEAD) + 1);
  strcpy (buff, HTML_HEAD);

  /* mail message header */
  cur->text.size = strlen (cur->text.data = (unsigned char *) cpystr ("Date"));
  cur = cur->next = mail_newstringlist ();
  cur->text.size = strlen (cur->text.data = (unsigned char *) cpystr ("From"));
  cur = cur->next = mail_newstringlist ();
  cur->text.size = strlen (cur->text.data = (unsigned char *) cpystr (">From"));
  cur = cur->next = mail_newstringlist ();
  cur->text.size = strlen (cur->text.data = (unsigned char *) cpystr ("Subject"));
  cur = cur->next = mail_newstringlist ();
  cur->text.size = strlen (cur->text.data = (unsigned char *) cpystr ("To"));
  cur = cur->next = mail_newstringlist ();
  cur->text.size = strlen (cur->text.data = (unsigned char *) cpystr ("cc"));
  cur = cur->next = mail_newstringlist ();
  cur->text.size = strlen (cur->text.data = (unsigned char *) cpystr ("Newsgroups"));

  c = mail_fetchheader_full (stream, mesgno, lines, NIL, NIL);
  gs = text2html (c);
  buff = g_realloc (buff, strlen (buff) + strlen (gs->str) + 1);
  strcat (buff, gs->str);
  g_string_free (gs, 1);

  mail_free_stringlist (&lines);

  /* message body */
  c = mail_fetchtext (stream, mesgno);

   gs = text2html (c);
   buff = g_realloc (buff, strlen (buff) + strlen (gs->str) + 1);
/*
  buff = g_realloc (buff, strlen (buff) + strlen (c) + 1);
  strcat (buff, c);
*/
   strcat (buff, gs->str);
   g_string_free (gs, 1);

  /* HTML footer */
  buff = g_realloc (buff, strlen (buff) + strlen (HTML_FOOT) + 1);
  strcat (buff, HTML_FOOT);

#ifdef DEBUG
  fprintf (stderr, buff);
#endif
/* set message contents */
  gtk_xmhtml_source (GTK_XMHTML (GTK_BIN (bmessage)->child), buff);
  g_free (buff);
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

/* debug */
void
debug_mime_content (BODY * body)
{
  gint i;
  char tmp[MAILTMPLEN];
  char *s = tmp;
  PARAMETER *par;
  PART *part;

  if (body->type == TYPEMULTIPART)
    {
      for (i = 0, part = body->nested.part; part; i++, part = part->next)
	{
	  debug_mime_content (&part->body);
	}
    }
  else
    {
      sprintf (s, "%s", body_types[body->type]);

      /* non-multipart, output one line descriptor */
      if (body->subtype)
	sprintf (s += strlen (s), "/%s", body->subtype);

      if (body->description)
	sprintf (s += strlen (s), " (%s)", body->description);

      if (par = body->parameter)
	do
	  sprintf (s += strlen (s), ";%s=%s", par->attribute, par->value);
	while (par = par->next);

      if (body->id)
	sprintf (s += strlen (s), ", id = %s", body->id);

      /* bytes or lines depending upon body type */
      switch (body->type)
	{
	case TYPEMESSAGE:	/* encapsulated message */
	case TYPETEXT:		/* plain text */
	  sprintf (s += strlen (s), " (%ld lines)", body->size.lines);
	  break;

	default:
	  sprintf (s += strlen (s), " (%ld bytes)", body->size.bytes);
	  break;
	}

      /* output this line */
      g_print ("%s\n", tmp);
    }
}

static GString *
text2html (char *buff)
{
  int i = 0, len = strlen (buff);
  GString *gs = g_string_new (NULL);

  for (i = 0; i < len; i++)
    {
      switch (buff[i])
	{
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
