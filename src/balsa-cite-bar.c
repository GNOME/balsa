/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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

static void balsa_cite_bar_measure  (GtkWidget    * widget,
                                     GtkOrientation orientation,
                                     gint           for_size,
                                     gint         * minimum,
                                     gint         * natural,
                                     gint         * minimum_baseline,
                                     gint         * natural_baseline);
static void balsa_cite_bar_snapshot (GtkWidget * widget,
                                     GtkSnapshot * snapshot);

G_DEFINE_TYPE(BalsaCiteBar, balsa_cite_bar, GTK_TYPE_WIDGET)

static void
balsa_cite_bar_class_init(BalsaCiteBarClass * class)
{
    GtkWidgetClass *widget_class;

    widget_class = (GtkWidgetClass *) class;

    widget_class->measure  = balsa_cite_bar_measure;
    widget_class->snapshot = balsa_cite_bar_snapshot;
}

static void
balsa_cite_bar_init(BalsaCiteBar * cite_bar)
{
    gtk_widget_set_has_surface(GTK_WIDGET(cite_bar), FALSE);
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
balsa_cite_bar_measure(GtkWidget    * widget,
                       GtkOrientation orientation,
                       gint           for_size,
                       gint         * minimum,
                       gint         * natural,
                       gint         * minimum_baseline,
                       gint         * natural_baseline)
{
    BalsaCiteBar *cite_bar;

    cite_bar = BALSA_CITE_BAR(widget);

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        *minimum = *natural =
            cite_bar->bars * (cite_bar->width + cite_bar->space) -
            cite_bar->space;
    } else {
        *minimum = *natural = cite_bar->height;
        *minimum_baseline = *natural_baseline = 0;
    }
}

static void
balsa_cite_bar_snapshot(GtkWidget * widget, GtkSnapshot * snapshot)
{
    GtkStyleContext *context;
    GdkRGBA rgba;
    int n;
    BalsaCiteBar *cite_bar;
    graphene_rect_t bounds;

    context = gtk_widget_get_style_context(widget);
    gtk_style_context_get_color(context, &rgba);

    cite_bar = BALSA_CITE_BAR(widget);
    graphene_rect_init(&bounds, 0.0, 0.0,
                       (float) cite_bar->width, (float) cite_bar->height);

    for (n = 0; n < cite_bar->bars; n++) {
        gtk_snapshot_append_color(snapshot, &rgba, &bounds, "CiteBar");
        bounds.origin.x += (float) (cite_bar->width + cite_bar->space);
    }
}
