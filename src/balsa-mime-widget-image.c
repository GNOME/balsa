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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include "balsa-mime-widget-image.h"

#include "balsa-app.h"
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include <glib/gi18n.h>

/*
 * GObject class definitions
 */
struct _BalsaMimeWidgetImage {
    BalsaMimeWidget  parent_object;

    guint img_check_size_id;
    GdkPixbuf *pixbuf;
    GtkWidget *image;
};

G_DEFINE_TYPE(BalsaMimeWidgetImage,
              balsa_mime_widget_image,
              BALSA_TYPE_MIME_WIDGET);

static void
balsa_mime_widget_image_init(BalsaMimeWidgetImage * mwi)
{
#ifdef G_OBJECT_NEEDS_INITIALZING
    mwi->img_check_size_id = 0;
    mwi->pixbuf = NULL;
#endif /* G_OBJECT_NEEDS_INITIALZING */
}

static void
balsa_mime_widget_image_dispose(GObject * object)
{
    BalsaMimeWidgetImage *mwi = BALSA_MIME_WIDGET_IMAGE(object);

    if (mwi->img_check_size_id > 0) {
        g_source_remove(mwi->img_check_size_id);
        mwi->img_check_size_id = 0;
    }
    g_clear_object(&mwi->pixbuf);

    G_OBJECT_CLASS(balsa_mime_widget_image_parent_class)->dispose(object);
}

static void
balsa_mime_widget_image_class_init(BalsaMimeWidgetImageClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = balsa_mime_widget_image_dispose;
}

/*
 * Callbacks
 */

static gboolean
img_check_size(BalsaMimeWidgetImage * mwi)
{
    GtkWidget *widget;
    GtkImage *image;
    GtkWidget *viewport;
    gint orig_width;
    GdkPixbuf *pixbuf;
    gint curr_w, dst_w;
    GtkAllocation allocation;

    mwi->img_check_size_id = 0;

    widget = GTK_WIDGET(mwi);
    viewport = gtk_widget_get_ancestor(widget, GTK_TYPE_VIEWPORT);
    if (viewport == NULL) {
        return G_SOURCE_REMOVE;
    }

    pixbuf = mwi->pixbuf;
    if (pixbuf == NULL) {
        return G_SOURCE_REMOVE;
    }

    orig_width = gdk_pixbuf_get_width(pixbuf);
    if (orig_width <= 0) {
        return G_SOURCE_REMOVE;
    }

    image = GTK_IMAGE(mwi->image);
    if (gtk_image_get_storage_type(image) == GTK_IMAGE_PIXBUF)
        curr_w = gdk_pixbuf_get_width(gtk_image_get_pixbuf(image));
    else
        curr_w = 0;


    gtk_widget_get_allocation(viewport, &allocation);
    dst_w = allocation.width;
    gtk_widget_get_allocation(gtk_bin_get_child(GTK_BIN(viewport)),
                              &allocation);
    dst_w -= allocation.width;
    gtk_widget_get_allocation(gtk_widget_get_parent(widget),
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

    return G_SOURCE_REMOVE;
}

static void
balsa_image_button_press_cb(GtkGestureMultiPress *multi_press_gesture,
                            gint                  n_press,
                            gdouble               x,
                            gdouble               y,
                            gpointer              user_data)
{
    GtkMenu *menu = user_data;
    GtkGesture *gesture;
    GdkEventSequence *sequence;
    const GdkEvent *event;

    gesture  = GTK_GESTURE(multi_press_gesture);
    sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(multi_press_gesture));
    event    = gtk_gesture_get_last_event(gesture, sequence);

    if (gdk_event_triggers_context_menu(event)) {
        gtk_menu_popup_at_pointer(menu, event);
        gtk_gesture_set_sequence_state(gesture, sequence, GTK_EVENT_SEQUENCE_CLAIMED);
    }
}

static void
img_size_allocate_cb(BalsaMimeWidgetImage *mwi, GtkAllocation *allocation, gpointer user_data)
{
    if (mwi->pixbuf != NULL && mwi->img_check_size_id == 0) {
        mwi->img_check_size_id = g_idle_add((GSourceFunc) img_check_size, mwi);
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
    BalsaMimeWidgetImage *mwi;
    BalsaMimeWidget *mw;
    GtkWidget *widget;
    GtkGesture *gesture;

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

    mwi = g_object_new(BALSA_TYPE_MIME_WIDGET_IMAGE, NULL);
    mwi->pixbuf = pixbuf;

    mw = (BalsaMimeWidget *) mwi;

    widget = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(mw), widget);

    gesture = gtk_gesture_multi_press_new(widget);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
    g_signal_connect(gesture, "pressed",
                     G_CALLBACK(balsa_image_button_press_cb), data);

    mwi->image = image = gtk_image_new_from_icon_name("image-missing",
                                                      GTK_ICON_SIZE_BUTTON);
    g_signal_connect_swapped(image, "size-allocate",
                             G_CALLBACK(img_size_allocate_cb), mwi);
    gtk_container_add(GTK_CONTAINER(widget), image);

    return mw;
}
