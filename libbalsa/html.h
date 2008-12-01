/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef   __LIBBALSA_HTML_H__
# define  __LIBBALSA_HTML_H__

#include "config.h"

# if defined(HAVE_GTKHTML2)
/* gtkhtml2 uses deprecated api */
#  undef GTK_DISABLE_DEPRECATED
# endif

#  include <gtk/gtk.h>

/* We need this enum even if we're not using GtkHtml. */
typedef enum {
    LIBBALSA_HTML_TYPE_NONE = 0,
    LIBBALSA_HTML_TYPE_HTML,
    LIBBALSA_HTML_TYPE_ENRICHED,
    LIBBALSA_HTML_TYPE_RICHTEXT
} LibBalsaHTMLType;

# ifdef HAVE_GTKHTML

typedef void (*LibBalsaHtmlCallback) (const gchar * uri);

GtkWidget *libbalsa_html_new(const gchar * text, size_t len,
			     const gchar * charset,
			     gpointer message,
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

#if defined(HAVE_GNOME) && !defined(HAVE_GTK_PRINT)

#  include <libgnomeprint/gnome-print.h>

gboolean libbalsa_html_can_print(void);
typedef void (*LibBalsaHTMLPrintCallback) (GtkWidget * widget,
					   GnomePrintContext *
					   print_context, gdouble x,
					   gdouble y, gdouble width,
					   gdouble height,
					   gpointer user_data);
void libbalsa_html_print(GtkWidget * widget,
			 GnomePrintContext * print_context,
			 gdouble header_height, gdouble footer_height,
			 LibBalsaHTMLPrintCallback header_print,
			 LibBalsaHTMLPrintCallback footer_print,
			 gpointer user_data);
gint libbalsa_html_print_get_pages_num(GtkWidget * widget,
				       GnomePrintContext * print_context,
				       gdouble header_height,
				       gdouble footer_height);

#endif /* defined(HAVE_GNOME) && !defined(HAVE_GTK_PRINT) */

# endif				/* HAVE_GTKHTML */

LibBalsaHTMLType libbalsa_html_type(const gchar * mime_type);

#endif				/* __LIBBALSA_HTML_H__ */
