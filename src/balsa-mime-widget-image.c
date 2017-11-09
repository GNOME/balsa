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

/*
 * GObject class definitions
 */
struct _BalsaMimeWidgetImage {
    BalsaMimeWidget  parent;
};

struct _BalsaMimeWidgetImageClass {
    BalsaMimeWidgetClass parent;
};

G_DEFINE_TYPE(BalsaMimeWidgetImage,
              balsa_mime_widget_image,
              BALSA_TYPE_MIME_WIDGET);

static void
balsa_mime_widget_image_init(BalsaMimeWidgetImage * mwi)
{
}

static void
balsa_mime_widget_image_dispose(GObject * obj)
{
    (*G_OBJECT_CLASS(balsa_mime_widget_image_parent_class)->
          dispose) (obj);
}

static void
balsa_mime_widget_image_class_init(BalsaMimeWidgetImageClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = balsa_mime_widget_image_dispose;
}
/*
 * End of GObject class definitions
 */

static gboolean balsa_image_button_press_cb(GtkWidget * widget, GdkEventButton * event,
					    GtkMenu * menu);
static gboolean img_check_size(GtkImage ** widget_p);

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
    mw = (BalsaMimeWidget *) mwi;

#ifdef WE_STILL_NEED_AN_EVENT_BOX
    mw->widget = gtk_event_box_new();
#endif

    image = gtk_image_new_from_icon_name("image-missing",
                                         GTK_ICON_SIZE_BUTTON);
    g_object_set_data(G_OBJECT(image), "orig-width",
		      GINT_TO_POINTER(gdk_pixbuf_get_width(pixbuf)));
    g_object_set_data(G_OBJECT(image), "mime-body", mime_body);
    g_object_unref(pixbuf);
#ifdef WE_STILL_NEED_AN_EVENT_BOX
    gtk_container_add(GTK_CONTAINER(mw->widget), image);
#else
    mw->widget = image;
#endif
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
    if (gdk_event_triggers_context_menu((GdkEvent *) event)) {
        gtk_menu_popup_at_pointer(menu, (GdkEvent *) event);
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
    GtkAllocation allocation;

    widget = *widget_p;
    g_free(widget_p);
    if (!widget) {
	return FALSE;
    }
    g_object_remove_weak_pointer(G_OBJECT(widget), (gpointer) widget_p);

    viewport = gtk_widget_get_ancestor(GTK_WIDGET(widget), GTK_TYPE_VIEWPORT);
    orig_width = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
                                                        "orig-width"));
    mime_body = (LibBalsaMessageBody *)g_object_get_data(G_OBJECT(widget), "mime-body");

    g_object_set_data(G_OBJECT(widget), "check_size_sched",
                      GINT_TO_POINTER(FALSE));
    g_warn_if_fail(viewport && mime_body && orig_width > 0);
    if (!(viewport && mime_body && orig_width > 0)) {
        return FALSE;
    }

    if (gtk_image_get_storage_type(widget) == GTK_IMAGE_SURFACE)
	curr_w = cairo_image_surface_get_width(gtk_image_get_surface(widget));
    else
	curr_w = 0;

    gtk_widget_get_allocation(viewport, &allocation);
    dst_w = allocation.width;
    gtk_widget_get_allocation(gtk_bin_get_child(GTK_BIN(viewport)),
                              &allocation);
    dst_w -= allocation.width;
    gtk_widget_get_allocation(gtk_widget_get_parent(GTK_WIDGET(widget)),
                              &allocation);
    dst_w += allocation.width;
    dst_w -= 16;                /* Magic number? */

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

    return FALSE;
}
