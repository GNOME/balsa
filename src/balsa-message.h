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

#ifndef __BALSA_MESSAGE_H__
#define __BALSA_MESSAGE_H__

#include <gnome.h>
#include "libbalsa.h"

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define BALSA_MESSAGE(obj)          GTK_CHECK_CAST (obj, balsa_message_get_type (), BalsaMessage)
#define BALSA_MESSAGE_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, balsa_message_get_type (), BalsaMessageClass)
#define BALSA_IS_MESSAGE(obj)       GTK_CHECK_TYPE (obj, balsa_message_get_type ())


  typedef struct _BalsaMessage BalsaMessage;
  typedef struct _BalsaMessageClass BalsaMessageClass;

  typedef struct _BalsaPartInfo BalsaPartInfo;

  struct _BalsaMessage
    {
      GtkViewport parent;

      /* The table widget */
      GtkWidget *table;

      /* Widget to hold headers */
      GtkWidget *header_text;
      ShownHeaders shown_headers;

      /* Widget to hold content */
      GtkWidget *content;
      gboolean content_has_focus;

      /* Widget to hold icons */
      GtkWidget *part_list;
      gint part_count;

      gboolean wrap_text;

      BalsaPartInfo *current_part;
      
      LibBalsaMessage *message;
    };

  struct _BalsaMessageClass
    {
      GtkViewportClass parent_class;

      void (*select_part) (BalsaMessage *message);
    };

  guint balsa_message_get_type (void);
  GtkWidget *balsa_message_new (void);

  void balsa_message_clear (BalsaMessage * bmessage);
  void balsa_message_set (BalsaMessage * bmessage, LibBalsaMessage * message);

  void balsa_message_next_part (BalsaMessage *bmessage);
  void balsa_message_previous_part (BalsaMessage *bmessage);
  void balsa_message_save_current_part (BalsaMessage *bmessage);

  void balsa_message_set_displayed_headers(BalsaMessage *bmessage, 
					   ShownHeaders sh);
  void balsa_message_set_wrap(BalsaMessage *bmessage, gboolean wrap);

  gboolean balsa_message_can_select(BalsaMessage *bmessage);
  void balsa_message_copy_clipboard(BalsaMessage *bmessage);
  void balsa_message_select_all(BalsaMessage *bmessage);

  void reflow_string(gchar* str, gint mode, gint *cur_pos, int width);

   /* a helper functions; FIXME: find more proper location for them.  */
   gchar* get_font_name(const gchar* base, const gchar *charset);
   gchar* get_koi_font_name(const gchar* base, const gchar* code);

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_MESSAGE_H__ */
