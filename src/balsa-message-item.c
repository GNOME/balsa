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
#include "balsa-message-item.h"
#include "misc.h"

/* mime */
gchar *content2html (Message * message);

/* static */

char *urls[] =
{
  "unknown", "named (...)", "jump (#...)",
  "file_local (file.html)", "file_remote (file://foo.bar/file)",
  "ftp", "http", "gopher", "wais", "news", "telnet", "mailto",
  "exec:foo_bar", "internal"
};

struct balsa_save_to_file_info
  {
    GtkWidget *file_entry;
    Message *msg;
    BODY *body;
  };

static void
save_MIME_part (GtkObject * o, struct balsa_save_to_file_info *info)
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
      gnome_dialog_set_modal (GNOME_DIALOG (msgbox));
      gnome_dialog_run (GNOME_DIALOG (msgbox));
      return;
    }
  filename = gtk_entry_get_text (GTK_ENTRY (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (file_entry))));
  s.prefix = 0;
  s.fpout = fopen (filename, "w");
  fseek (s.fpin, info->body->offset, 0);
  if (!s.fpout)
    {
      char msg[1024];
      GtkWidget *msgbox;

      snprintf (msg, 1023, _ (" Open of %s failed:%s "), filename, strerror (errno));
      msgbox = gnome_message_box_new (msg, "Error", _ ("Ok"), NULL);
      gnome_dialog_set_modal (GNOME_DIALOG (msgbox));
      gnome_dialog_run (GNOME_DIALOG (msgbox));
      return;
    }
  mutt_decode_attachment (info->body, &s);
  fclose (s.fpin);
  fclose (s.fpout);
}




static void
bm_item_handle_mime_part (GtkObject * xmhtml_widget, gpointer data)
{
  XmHTMLAnchorCallbackStruct *cbs = (XmHTMLAnchorCallbackStruct *) data;
  Message *message = 0;
  BODY *body = 0;
  char *ptr;
  int rc;
  GtkWidget *save_dialog;
  GtkWidget *file_entry;
  struct balsa_save_to_file_info info;

  ptr = strchr (cbs->href, ':');
  if (!ptr)
    {
      char msg[1024];
      GtkWidget *msgbox;

      snprintf (msg, 1023, _ ("Invalif balsa internal URL fomt '%s' detected"), cbs->href);
      msgbox = gnome_message_box_new (msg, "Error", _ ("Ok"), NULL);
      gnome_dialog_set_modal (GNOME_DIALOG (msgbox));
      gnome_dialog_run (GNOME_DIALOG (msgbox));
      return;
    }
  rc = sscanf (ptr + 3, "%p:%p", &message, &body);
  if (rc != 2)
    {
      char msg[1024];
      GtkWidget *msgbox;

      snprintf (msg, 1023, _ ("Invalif balsa internal URL fomt '%s' detected"), cbs->href);
      msgbox = gnome_message_box_new (msg, "Error", _ ("Ok"), NULL);
      gnome_dialog_set_modal (GNOME_DIALOG (msgbox));
      gnome_dialog_run (GNOME_DIALOG (msgbox));
      return;
    }
  save_dialog = gnome_dialog_new (_ ("Save MIME Part"),
				  _ ("Save"), _ ("Cancel"), NULL);
  file_entry = gnome_file_entry_new ("Balsa_MIME_Saver",
				     _ ("Save MIME Part"));
  info.file_entry = file_entry;
  info.msg = message;
  info.body = body;

  if (body->filename)
    {
      gtk_entry_set_text (GTK_ENTRY (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (file_entry))), body->filename);
    }
  gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (save_dialog)->vbox), file_entry, FALSE, FALSE, 10);
  gtk_widget_show (file_entry);
  gnome_dialog_button_connect (GNOME_DIALOG (save_dialog), 0, save_MIME_part, &info);
  gnome_dialog_set_modal (GNOME_DIALOG (save_dialog));
  gnome_dialog_run_and_hide (GNOME_DIALOG (save_dialog));
  gtk_widget_destroy (save_dialog);
}

GnomeCanvasItem *
bm_item_new (BalsaMessage * bm)
{
  GnomeCanvasGroup *parent;

  GnomeCanvasItem *new;
  GtkWidget *html;

  parent = gnome_canvas_root(GNOME_CANVAS(bm));

  g_return_val_if_fail (parent != NULL, NULL);
  g_return_val_if_fail (GNOME_IS_CANVAS_GROUP (parent), NULL);

#if 0
  html = gtk_label_new("Hey!");
  gtk_widget_show(html);
  
  html = gtk_xmhtml_new ();

  /* create the HTML widget to render the message */
  gtk_xmhtml_source (GTK_XMHTML (html), "");
  gtk_widget_show (html);
  gtk_widget_ref (html);
  gtk_signal_connect (GTK_OBJECT (html),
		      "activate",
		      GTK_SIGNAL_FUNC (bm_item_handle_mime_part),
		      NULL);
  new = gnome_canvas_item_new (parent, gnome_canvas_widget_get_type(), "widget", html, NULL);
#endif

  new = gnome_canvas_item_new(parent,
		  gnome_canvas_rect_get_type(),
		  "x1", 0, "y1", 0,
		  "x2", 500, "y2", 500,
		  "fill_color", "black", NULL);
  
  return GNOME_CANVAS_ITEM (new);
}

void
bm_item_set (BalsaMessage * bmessage,
		GnomeCanvasItem *item,
		Message * message)
{
  gchar *buff;
  GtkWidget *html;

  g_return_if_fail (bmessage != NULL);
  g_return_if_fail (message != NULL);
#if 0
  html = GNOME_CANVAS_WIDGET(item)->widget;
  
  if (bmessage->message == message)
    return;

  message_body_unref (bmessage->message);
  bmessage->message = message;
  message_body_ref (bmessage->message);
  /* set message contents */
  buff = content2html (message);

  gtk_xmhtml_source (GTK_XMHTML (html), buff);

  message_body_unref (bmessage->message);
#endif
}
