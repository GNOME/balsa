/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef   __LIBBALSA_HTML_H__
# define  __LIBBALSA_HTML_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#  include <gtk/gtk.h>
#include "libbalsa.h"

/* We need this enum even if we're not using GtkHtml. */
typedef enum {
    LIBBALSA_HTML_TYPE_NONE = 0,
    LIBBALSA_HTML_TYPE_HTML,
    LIBBALSA_HTML_TYPE_ENRICHED,
    LIBBALSA_HTML_TYPE_RICHTEXT
} LibBalsaHTMLType;

# ifdef HAVE_HTML_WIDGET

typedef void (*LibBalsaHtmlCallback) (const gchar * uri);

GtkWidget *libbalsa_html_new(LibBalsaMessageBody * body,
                             LibBalsaHtmlCallback hover_cb,
                             LibBalsaHtmlCallback clicked_cb);
void libbalsa_html_to_string(gchar ** text, size_t len);
gboolean libbalsa_html_can_zoom(GtkWidget * widget);
void libbalsa_html_zoom(GtkWidget * widget, gint in_out);
gboolean libbalsa_html_can_select(GtkWidget * widget);
void libbalsa_html_select_all(GtkWidget * widget);
void libbalsa_html_copy(GtkWidget * widget);
guint libbalsa_html_filter(LibBalsaHTMLType html_type, gchar ** text,
			   guint len);

typedef void (*LibBalsaHtmlSearchCallback)(const gchar * text,
                                           gboolean      found,
                                           gpointer      data);
gboolean libbalsa_html_can_search(GtkWidget * widget);
void libbalsa_html_search(GtkWidget                * widget,
                          const gchar              * text,
                          gboolean                   find_forward,
                          gboolean                   wrap,
                          LibBalsaHtmlSearchCallback search_cb,
                          gpointer                   cb_data);
gboolean libbalsa_html_get_selection_bounds(GtkWidget * widget,
                                            GdkRectangle *
                                            selection_bounds);

#define LIBBALSA_HTML_POPUP_EVENT "libbalsa-html-popup-event"
GtkWidget *libbalsa_html_popup_menu_widget(GtkWidget * widget);
GtkWidget *libbalsa_html_get_view_widget(GtkWidget * widget);

gboolean libbalsa_html_can_print(GtkWidget * widget);
void libbalsa_html_print(GtkWidget * widget);

# endif				/* HAVE_HTML_WIDGET */

LibBalsaHTMLType libbalsa_html_type(const gchar * mime_type);

#endif				/* __LIBBALSA_HTML_H__ */
