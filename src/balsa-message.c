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

#include "balsa-app.h"
#include "mailbackend.h"
#include "balsa-message.h"
#include "mime.h"
#include "misc.h"

static gchar tmp_file_name[PATH_MAX + 1];

static gint part_idx;
static gint part_nesting_depth;

/* widget */
static void balsa_message_class_init (BalsaMessageClass * klass);
static void balsa_message_init (BalsaMessage * bmessage);
static void balsa_message_size_request (GtkWidget * widget, GtkRequisition * requisition);
static void balsa_message_size_allocate (GtkWidget * widget, GtkAllocation * allocation);

static void headers2canvas (BalsaMessage * bmessage, Message * message);
static void body2canvas (BalsaMessage * bmessage, Message * message);

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
  GnomeCanvasGroup *bm_group;
  double x1, x2, y1, y2;

  g_return_if_fail (bmessage != NULL);
  g_return_if_fail (message != NULL);

  if (bmessage->message == message)
    return;

  if (bmessage->headers)
    {
      gtk_object_destroy (GTK_OBJECT (bmessage->headers));

      gtk_object_destroy (GTK_OBJECT (bmessage->body));
    }

  headers2canvas (bmessage, message);
  body2canvas (bmessage, message);
  bm_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (bmessage)->root);

  gnome_canvas_item_get_bounds (GNOME_CANVAS_ITEM (bm_group), &x1, &y1, &x2, &y2);
  gnome_canvas_set_scroll_region (GNOME_CANVAS (bmessage), x1 - 10, y1 - 10, x2 + 10, y2 + 10);
}

static GnomeCanvasItem *
balsa_message_text_item (gchar * text, GnomeCanvasGroup * group, double x, double y)
{
  GnomeCanvasItem *new;
  new = gnome_canvas_item_new (group,
			       GNOME_TYPE_CANVAS_TEXT,
			       "x", x,
			       "y", y,
			       "anchor", GTK_ANCHOR_NW,
       "font", "-adobe-helvetica-medium-r-normal--12-*-72-72-p-*-iso8859-1",
			       "text", text, NULL);
  return new;
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

  if (o > t)
    return o;
  else
    return t;
}

static void
headers2canvas (BalsaMessage * bmessage, Message * message)
{
  double next_height = 0;
  double x1, x2, y1, y2;

  GnomeCanvasGroup *bm_root;
  GnomeCanvasGroup *row[2];
  GnomeCanvasItem *item;
  GnomeCanvasItem *data;

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

  if (message->date)
    {
      /* this is the first row, so we'll use 0.0 here */
      item = balsa_message_text_item ("Date:", row[0], 0.0, 0.0);
      data = balsa_message_text_item (message->date, row[1], 0.0, 0.0);
    }

  if (message->to_list)
    {
      next_height = next_row_height (row);
      item = balsa_message_text_item ("To:", row[0], 0.0, next_height);
      data = balsa_message_text_item (make_string_from_list (message->to_list),
				      row[1], 0.0, next_height);
    }

  if (message->cc_list)
    {
      next_height = next_row_height (row);
      item = balsa_message_text_item ("Cc:", row[0], 0.0, next_height);
      data = balsa_message_text_item (make_string_from_list (message->cc_list),
				      row[1], 0.0, next_height);
    }

  if (message->from)
    {
      gchar *from;
      next_height = next_row_height (row);

      item = balsa_message_text_item ("From:", row[0], 0.0, next_height);

      if (message->from->personal)
	from = g_strdup_printf ("%s <%s>", message->from->personal, message->from->mailbox);
      else
	from = g_strdup (message->from->mailbox);

      data = balsa_message_text_item (from, row[1], 0.0, next_height);
      g_free (from);
    }

  if (message->subject)
    {
      next_height = next_row_height (row);
      item = balsa_message_text_item ("Subject:", row[0], 0.0, next_height);
      data = balsa_message_text_item (message->subject, row[1], 0.0, next_height);
    }

  gnome_canvas_item_get_bounds (GNOME_CANVAS_ITEM (row[0]), &x1, &y1, &x2, &y2);
  gnome_canvas_item_move (GNOME_CANVAS_ITEM (row[1]), x2 - x1 + 30, 0.0);
}


static void
body2canvas (BalsaMessage * bmessage, Message * message)
{
  GnomeCanvasGroup *bm_root;
  double x1, x2, y1, y2;
  GString *str;
  gchar *text;

  bm_root = GNOME_CANVAS_GROUP (GNOME_CANVAS (bmessage)->root);

  gnome_canvas_item_get_bounds (GNOME_CANVAS_ITEM (bmessage->headers), &x1, &y1, &x2, &y2);

  bmessage->body =
    GNOME_CANVAS_GROUP (gnome_canvas_item_new (bm_root,
					       GNOME_TYPE_CANVAS_GROUP,
					       "x", (double) 10.0,
					       "y", (double) (y2 - y1) + 20,
					       NULL));
  message_body_ref (message);
  content2canvas (message, bmessage->body);
  message_body_unref (message);
}

void
other2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  STATE s;
  gchar *text;
  size_t alloced;

  fseek (fp, bdy->offset, 0);
  s.fpin = fp;
  mutt_mktemp (tmp_file_name);

  s.fpout = fopen (tmp_file_name, "r+");
  s.prefix = '\0';
  mutt_decode_attachment (bdy, &s);
  fflush (s.fpout);
  alloced = readfile (s.fpout, &text);
  if (text)
    text[alloced - 1] = '\0';

  balsa_message_text_item (text, group, 0.0, 0.0);

  g_free (text);
  fclose (s.fpout);
  unlink (tmp_file_name);
}

void
audio2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  balsa_message_text_item ("--AUDIO--", group, 0.0, 0.0);
}


void
application2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  gchar link_bfr[128];
  PARAMETER *bdy_parameter = bdy->parameter;

  balsa_message_text_item ("--APPLICATION--", group, 0.0, 0.0);
#if 0
  obstack_append_string (canvas_bfr,
			 "<tr><td bgcolor=\"#f0f0f0\"> "
		       "You received an encoded file of type application/");
  obstack_append_string (canvas_bfr, bdy->subtype);
  obstack_append_string (canvas_bfr, "<BR>");
  obstack_append_string (canvas_bfr, "<P>The parameters of this message are:<BR>");
  obstack_append_string (canvas_bfr, "<table border=0><tr><th>Attribute</th><th>Value</th></tr>\n");
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

void
image2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  gnome_canvas_item_new (group,
			 GNOME_TYPE_CANVAS_TEXT,
			 "x", 0.0,
			 "y", 0.0,
			 "anchor", GTK_ANCHOR_NW,
       "font", "-adobe-helvetica-medium-r-normal--12-*-72-72-p-*-iso8859-1",
			 "text", "--IMAGE--",
			 NULL);
}

void
message2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  gnome_canvas_item_new (group,
			 GNOME_TYPE_CANVAS_TEXT,
			 "x", 0.0,
			 "y", 0.0,
			 "anchor", GTK_ANCHOR_NW,
       "font", "-adobe-helvetica-medium-r-normal--12-*-72-72-p-*-iso8859-1",
			 "text", "--MESSAGE--",
			 NULL);
}

void
multipart2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
#if 0
  BODY *p;
  PARAMETER *bdy_parameter = bdy->parameter;

  if (balsa_app.debug)
    {
      obstack_append_string (canvas_bfr, "<tr><td>Multipart message parameter are:<BR>");
      obstack_append_string (canvas_bfr, "<table border=\"0\" width=\"50%\" bgcolor=\"#dddddd\">\n");
      obstack_append_string (canvas_bfr, "<tr><th>Attribute</th><th>Value</th></tr>\n");
      while (bdy_parameter)
	{
	  obstack_append_string (canvas_bfr, "<tr><td>");
	  obstack_append_string (canvas_bfr, bdy_parameter->attribute);
	  obstack_append_string (canvas_bfr, "</td><td>");
	  obstack_append_string (canvas_bfr, bdy_parameter->value);
	  obstack_append_string (canvas_bfr, "</td></tr>");
	  bdy_parameter = bdy_parameter->next;
	}
      obstack_append_string (canvas_bfr, "</table></td></tr>");
    }
  for (p = bdy->parts; p; p = p->next)
    {
      part2canvas (message, p, fp, canvas_bfr);
    }
#endif
}


void
video2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  gnome_canvas_item_new (group,
			 GNOME_TYPE_CANVAS_TEXT,
			 "x", 0.0,
			 "y", 0.0,
			 "anchor", GTK_ANCHOR_NW,
       "font", "-adobe-helvetica-medium-r-normal--12-*-72-72-p-*-iso8859-1",
			 "text", "--VIDEO--",
			 NULL);
}

void
mimetext2canvas (Message * message, BODY * bdy, FILE * fp, GnomeCanvasGroup * group)
{
  STATE s;
  gchar *ptr = 0;
  size_t alloced;


  fseek (fp, bdy->offset, 0);
  s.fpin = fp;
  s.prefix = '\0';
  mutt_mktemp (tmp_file_name);
  s.prefix = 0;
  s.fpout = fopen (tmp_file_name, "w+");
  mutt_decode_attachment (bdy, &s);
  fflush (s.fpout);
  alloced = readfile (s.fpout, &ptr);
  if (ptr)
    {
      ptr[alloced - 1] = '\0';
      if (strcmp (bdy->subtype, "canvas") == 0)
	{
#if 0
	  obstack_append_string (canvas_bfr, ptr);
	  g_free (ptr);
	  unlink (tmp_file_name);
#endif
	  return;
	}
      gnome_canvas_item_new (group,
			     GNOME_TYPE_CANVAS_TEXT,
			     "x", 0.0,
			     "y", 0.0,
			     "anchor", GTK_ANCHOR_NW,
       "font", "-adobe-helvetica-medium-r-normal--12-*-72-72-p-*-iso8859-1",
			     "text", ptr,
			     NULL);
      g_free (ptr);
    }
  fclose (s.fpout);
  unlink (tmp_file_name);
  return;
}


void
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
      if (balsa_app.debug)
	fprintf (stderr, "part: application\n");
      application2canvas (message, bdy, fp, group);
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

gboolean
content2canvas (Message * message, GnomeCanvasGroup * group)
{
  GList *body_list;
  Body *body;
  FILE *msg_stream;
  gchar msg_filename[PATH_MAX];
  static gchar *canvas_buffer_content = (gchar *) - 1;


  part_idx = 0;
  part_nesting_depth = 0;


  switch (message->mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      {
	snprintf (msg_filename, PATH_MAX, "%s/%s", MAILBOX_LOCAL (message->mailbox)->path, message_pathname (message));
	msg_stream = fopen (msg_filename, "r");
	if (!msg_stream || ferror (msg_stream))
	  {
	    fprintf (stderr, "Open of %s failed. Errno = %d, ",
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
