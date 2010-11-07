/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others
 * Written by (C) Albrecht Dreﬂ <albrecht.dress@arcor.de> 2007
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

#include "balsa-cite-bar.h"

struct _BalsaCiteBar {
    GtkWidget widget;

    /* Dimensions of each citation bar */
    gint width;
    gint height;

    /* bar count and spacing */
    gint bars;
    gint space;
};

struct _BalsaCiteBarClass {
    GtkWidgetClass parent_class;
};

static void balsa_cite_bar_size_request(GtkWidget      * widget,
                                        GtkRequisition * requisition);
static gboolean balsa_cite_bar_expose  (GtkWidget      * widget,
                                        GdkEventExpose * event);

G_DEFINE_TYPE(BalsaCiteBar, balsa_cite_bar, GTK_TYPE_WIDGET)

static GtkWidgetClass *parent_class = NULL;

static void
balsa_cite_bar_class_init(BalsaCiteBarClass * class)
{
    GtkWidgetClass *widget_class;

    widget_class = (GtkWidgetClass *) class;

    parent_class = g_type_class_peek_parent(class);

    widget_class->expose_event = balsa_cite_bar_expose;
    widget_class->size_request = balsa_cite_bar_size_request;
}

static void
balsa_cite_bar_init(BalsaCiteBar * cite_bar)
{
#if GTK_CHECK_VERSION(2, 18, 0)
    gtk_widget_set_has_window(GTK_WIDGET(cite_bar), FALSE);
#else                           /* GTK_CHECK_VERSION(2, 18, 0) */
    GTK_WIDGET_SET_FLAGS(GTK_WIDGET(cite_bar), GTK_NO_WINDOW);
#endif                          /* GTK_CHECK_VERSION(2, 18, 0) */ 
}

GtkWidget *
balsa_cite_bar_new(gint height, gint bars, gint dimension)
{
    BalsaCiteBar *cite_bar;

    g_return_val_if_fail(dimension > 1, NULL);

    cite_bar = g_object_new(BALSA_TYPE_CITE_BAR, NULL);

    cite_bar->height = height;
    cite_bar->bars = bars;

    /* the width is 1/4 of the dimension, the spaceing 3/4, but both
     * at least 1 pixel */
    cite_bar->width = dimension / 4;
    if (cite_bar->width == 0)
        cite_bar->width = 1;
    cite_bar->space = dimension - cite_bar->width;

    return GTK_WIDGET(cite_bar);
}

void
balsa_cite_bar_resize(BalsaCiteBar * cite_bar, gint height)
{
    g_return_if_fail(BALSA_IS_CITE_BAR(cite_bar));

    cite_bar->height = height;
    gtk_widget_queue_resize(GTK_WIDGET(cite_bar));
}

static void
balsa_cite_bar_size_request(GtkWidget * widget,
                            GtkRequisition * requisition)
{
    BalsaCiteBar *cite_bar = BALSA_CITE_BAR(widget);

    requisition->width =
        cite_bar->bars * (cite_bar->width + cite_bar->space) -
        cite_bar->space;
    requisition->height = cite_bar->height;
}

static gboolean
balsa_cite_bar_expose(GtkWidget * widget, GdkEventExpose * event)
{
    if (!event->count) {
        BalsaCiteBar *cite_bar = BALSA_CITE_BAR(widget);
        GdkWindow *window = gtk_widget_get_window(widget);
        cairo_t *cr = gdk_cairo_create(window);
        GtkStyle *style = gtk_widget_get_style(widget);
        GtkAllocation allocation;
        int n;

#if GTK_CHECK_VERSION(2, 18, 0)
        gtk_widget_get_allocation(widget, &allocation);
#else                           /* GTK_CHECK_VERSION(2, 18, 0) */
        allocation.x = widget->allocation.x;
        allocation.y = widget->allocation.y;
#endif                          /* GTK_CHECK_VERSION(2, 18, 0) */ 
        gdk_cairo_set_source_color(cr, &style->fg[GTK_STATE_NORMAL]);
        for (n = 0; n < cite_bar->bars; n++) {
            cairo_rectangle(cr, allocation.x, allocation.y,
                            cite_bar->width, cite_bar->height);
            cairo_fill(cr);
            allocation.x += cite_bar->width + cite_bar->space;
        }
        cairo_destroy(cr);
    }

    return FALSE;
}
