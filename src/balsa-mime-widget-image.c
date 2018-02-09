/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include "balsa-mime-widget-image.h"

#include "balsa-app.h"
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include <glib/gi18n.h>


static void
balsa_mime_widget_image_gesture_pressed_cb(GtkGestureMultiPress *multi_press,
                                           gint                  n_press,
                                           gdouble               x,
                                           gdouble               y,
                                           gpointer              user_data)
{
    GtkMenu *menu = user_data;
    GtkGesture *gesture;
    const GdkEvent *event;

    gesture = GTK_GESTURE(multi_press);
    event = gtk_gesture_get_last_event(gesture, gtk_gesture_get_last_updated_sequence(gesture));
    g_return_if_fail(event != NULL);

    if (gdk_event_triggers_context_menu(event)) {
        gtk_menu_popup_at_pointer(menu, event);
    }
}

static gboolean
img_check_size(GtkImage ** widget_p)
{
    GtkImage *image;
    GtkWidget *viewport;
    gint orig_width;
    GdkPixbuf *pixbuf;
    gint curr_w, dst_w;
    GtkAllocation allocation;

    image = *widget_p;
    if (image == NULL) {
        g_free(widget_p);
	return FALSE;
    }
    g_object_remove_weak_pointer(G_OBJECT(image), (gpointer *) widget_p);
    g_free(widget_p);

    viewport = gtk_widget_get_ancestor(GTK_WIDGET(image), GTK_TYPE_VIEWPORT);

    pixbuf = g_object_get_data(G_OBJECT(image), "pixbuf");
    orig_width = gdk_pixbuf_get_width(pixbuf);

    g_object_set_data(G_OBJECT(image), "check_size_sched",
                      GINT_TO_POINTER(FALSE));
    if (!(viewport != NULL && pixbuf != NULL && orig_width > 0)) {
        return FALSE;
    }

    switch (gtk_image_get_storage_type(image)) {
        case GTK_IMAGE_SURFACE:
            curr_w = cairo_image_surface_get_width(gtk_image_get_surface(image));
            break;
        case GTK_IMAGE_TEXTURE:
            curr_w = gdk_texture_get_width(gtk_image_get_texture(image));
            break;
        default:
            curr_w = 0;
    }

    gtk_widget_get_allocation(viewport, &allocation);
    dst_w = allocation.width;
    gtk_widget_get_allocation(gtk_bin_get_child(GTK_BIN(viewport)),
                              &allocation);
    dst_w -= allocation.width;
    gtk_widget_get_allocation(gtk_widget_get_parent(GTK_WIDGET(image)),
                              &allocation);
    dst_w += allocation.width;
    dst_w -= 16;                /* Magic number? */
    dst_w = CLAMP(dst_w, 32, orig_width);

    if (dst_w != curr_w) {
	GdkPixbuf *scaled_pixbuf;
	gint dst_h;

	dst_h = (gfloat)dst_w / (gfloat)orig_width * gdk_pixbuf_get_height(pixbuf);
	scaled_pixbuf = gdk_pixbuf_scale_simple(pixbuf, dst_w, dst_h,
						GDK_INTERP_BILINEAR);
	gtk_image_set_from_pixbuf(image, scaled_pixbuf);
	g_object_unref(scaled_pixbuf);
    }

    return FALSE;
}

static void
img_realize_cb(GtkWidget * widget, gpointer user_data)
{
    if (g_object_get_data(G_OBJECT(widget), "pixbuf") != NULL &&
        !GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "check_size_sched"))) {
        GtkWidget **widget_p;

        widget_p = g_new(GtkWidget *, 1);
        g_object_set_data(G_OBJECT(widget), "check_size_sched",
                          GINT_TO_POINTER(TRUE));
        *widget_p = widget;
        g_object_add_weak_pointer(G_OBJECT(widget), (gpointer *) widget_p);
        g_idle_add((GSourceFunc) img_check_size, widget_p);
    }
}

/*
 * Public method
 */

BalsaMimeWidget *
balsa_mime_widget_new_image(BalsaMessage * bm,
                            LibBalsaMessageBody * mime_body,
			    const gchar * content_type, gpointer data)
{
    GdkPixbuf *pixbuf;
    GtkWidget *image;
    GError * load_err = NULL;
    GtkGesture *gesture;
    BalsaMimeWidget *mw;

    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);

    pixbuf = libbalsa_message_body_get_pixbuf(mime_body, &load_err);
    if (pixbuf == NULL) {
	if (load_err != NULL) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
			      _("Error loading attached image: %s\n"),
			      load_err->message);
	    g_error_free(load_err);
	}
	return NULL;
    }

    image = gtk_image_new_from_icon_name("image-missing");

    g_object_set_data_full(G_OBJECT(image), "pixbuf", pixbuf, g_object_unref);

    gesture = gtk_gesture_multi_press_new(GTK_WIDGET(image));
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
    g_object_set_data_full(G_OBJECT(image), "balsa-gesture", gesture, g_object_unref);
    g_signal_connect(gesture, "pressed",
                     G_CALLBACK(balsa_mime_widget_image_gesture_pressed_cb), data);

    mw = (BalsaMimeWidget *) g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    balsa_mime_widget_set_widget(mw, image);
    g_signal_connect(image, "realize", G_CALLBACK(img_realize_cb), NULL);

    return mw;
}
