/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#include "config.h"
#include "balsa-app.h"
#include "i18n.h"
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include "balsa-mime-widget-image.h"


static gboolean balsa_image_button_press_cb(GtkWidget * widget, GdkEventButton * event,
					    GtkMenu * menu);
static gboolean img_check_size(GtkImage ** widget_p);


BalsaMimeWidget *
balsa_mime_widget_new_image(BalsaMessage * bm, LibBalsaMessageBody * mime_body,
			    const gchar * content_type, gpointer data)
{
    GdkPixbuf *pixbuf;
    GtkWidget *image;
    GError * load_err = NULL;
    BalsaMimeWidget *mw;

    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);

    pixbuf = libbalsa_message_body_get_pixbuf(mime_body, &load_err);
    if (!pixbuf) {
	if (load_err) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
			      _("Error loading attached image: %s\n"),
			      load_err->message);
	    g_error_free(load_err);
	}
	return NULL;
    }

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    mw->widget = gtk_event_box_new();
    image = gtk_image_new_from_stock(GTK_STOCK_MISSING_IMAGE,
                                     GTK_ICON_SIZE_BUTTON);
    g_object_set_data(G_OBJECT(image), "orig-width",
		      GINT_TO_POINTER(gdk_pixbuf_get_width(pixbuf)));
    g_object_set_data(G_OBJECT(image), "mime-body", mime_body);
    g_object_unref(pixbuf);
    gtk_container_add(GTK_CONTAINER(mw->widget), image);
    gtk_widget_modify_bg(mw->widget, GTK_STATE_NORMAL,
			 &GTK_WIDGET(bm)->style->light[GTK_STATE_NORMAL]);

    g_signal_connect(G_OBJECT(mw->widget), "button-press-event",
                     G_CALLBACK(balsa_image_button_press_cb), data);

    return mw;
}


void
balsa_mime_widget_image_resize_all(GtkWidget * widget, gpointer user_data)
{
    if (GTK_IS_CONTAINER(widget))
        gtk_container_foreach(GTK_CONTAINER(widget),
			      balsa_mime_widget_image_resize_all, NULL);
    else if (GTK_IS_IMAGE(widget) &&
             g_object_get_data(G_OBJECT(widget), "orig-width") &&
             g_object_get_data(G_OBJECT(widget), "mime-body") &&
             !GPOINTER_TO_INT(g_object_get_data
                              (G_OBJECT(widget), "check_size_sched"))) {
        GtkWidget **widget_p = g_new(GtkWidget *, 1);
        g_object_set_data(G_OBJECT(widget), "check_size_sched",
                          GINT_TO_POINTER(TRUE));
        *widget_p = widget;
        g_object_add_weak_pointer(G_OBJECT(widget), (gpointer) widget_p);
        g_idle_add((GSourceFunc) img_check_size, widget_p);
    }
}


static gboolean
balsa_image_button_press_cb(GtkWidget * widget, GdkEventButton * event,
                            GtkMenu * menu)
{
    if (menu && event->type == GDK_BUTTON_PRESS && event->button == 3) {
        gtk_menu_popup(menu, NULL, NULL, NULL, NULL,
                       event->button, event->time);
        return TRUE;
    } else
        return FALSE;
}

static gboolean
img_check_size(GtkImage ** widget_p)
{
    GtkImage *widget;
    GtkWidget *viewport;
    gint orig_width;
    LibBalsaMessageBody * mime_body;
    gint curr_w, dst_w;

    gdk_threads_enter();

    widget = *widget_p;
    g_free(widget_p);
    if (!widget) {
        gdk_threads_leave();
	return FALSE;
    }
    g_object_remove_weak_pointer(G_OBJECT(widget), (gpointer) widget_p);

    viewport = gtk_widget_get_ancestor(GTK_WIDGET(widget), GTK_TYPE_VIEWPORT);
    orig_width = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
                                                        "orig-width"));
    mime_body = (LibBalsaMessageBody *)g_object_get_data(G_OBJECT(widget), "mime-body");

    g_object_set_data(G_OBJECT(widget), "check_size_sched",
                      GINT_TO_POINTER(FALSE));
    g_return_val_if_fail(viewport && mime_body && orig_width > 0,
                         (gdk_threads_leave(), FALSE));

    if (gtk_image_get_storage_type(widget) == GTK_IMAGE_PIXBUF)
	curr_w = gdk_pixbuf_get_width(gtk_image_get_pixbuf(widget));
    else
	curr_w = 0;
    dst_w = viewport->allocation.width - 
	(gtk_bin_get_child(GTK_BIN(viewport))->allocation.width - 
	 GTK_WIDGET(widget)->parent->allocation.width) - 4;
    if (dst_w < 32)
	dst_w = 32;
    if (dst_w > orig_width)
	dst_w = orig_width;
    if (dst_w != curr_w) {
	GdkPixbuf *pixbuf, *scaled_pixbuf;
	GError *load_err = NULL;
	gint dst_h;

	pixbuf = libbalsa_message_body_get_pixbuf(mime_body, &load_err);
        if (!pixbuf) {
	    if (load_err) {
		balsa_information(LIBBALSA_INFORMATION_ERROR,
			          _("Error loading attached image: %s\n"),
			          load_err->message);
		g_error_free(load_err);
	    }
            gdk_threads_leave();
	    return FALSE;
	}
	dst_h = (gfloat)dst_w /
	    (gfloat)orig_width * gdk_pixbuf_get_height(pixbuf);
	scaled_pixbuf = gdk_pixbuf_scale_simple(pixbuf, dst_w, dst_h,
						GDK_INTERP_BILINEAR);
	g_object_unref(pixbuf);
	gtk_image_set_from_pixbuf(widget, scaled_pixbuf);
	g_object_unref(scaled_pixbuf);
	
    }

    gdk_threads_leave();
    return FALSE;
}
