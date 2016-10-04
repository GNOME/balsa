/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-print-object-default.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "balsa-print-object.h"
#include "libbalsa-vfs.h"


/* object related functions */
static void
balsa_print_object_default_class_init(BalsaPrintObjectDefaultClass *
				      klass);
static void balsa_print_object_default_init(GTypeInstance * instance,
					    gpointer g_class);
static void balsa_print_object_default_destroy(GObject * self);

static void balsa_print_object_default_draw(BalsaPrintObject * self,
					    GtkPrintContext * context,
					    cairo_t * cairo_ctx);


static BalsaPrintObjectClass *parent_class = NULL;


GType
balsa_print_object_default_get_type()
{
    static GType balsa_print_object_default_type = 0;

    if (!balsa_print_object_default_type) {
	static const GTypeInfo balsa_print_object_default_info = {
	    sizeof(BalsaPrintObjectDefaultClass),
	    NULL,		/* base_init */
	    NULL,		/* base_finalize */
	    (GClassInitFunc) balsa_print_object_default_class_init,
	    NULL,		/* class_finalize */
	    NULL,		/* class_data */
	    sizeof(BalsaPrintObjectDefault),
	    0,			/* n_preallocs */
	    (GInstanceInitFunc) balsa_print_object_default_init
	};

	balsa_print_object_default_type =
	    g_type_register_static(BALSA_TYPE_PRINT_OBJECT,
				   "BalsaPrintObjectDefault",
				   &balsa_print_object_default_info, 0);
    }

    return balsa_print_object_default_type;
}


static void
balsa_print_object_default_class_init(BalsaPrintObjectDefaultClass * klass)
{
    parent_class = g_type_class_ref(BALSA_TYPE_PRINT_OBJECT);
    BALSA_PRINT_OBJECT_CLASS(klass)->draw =
	balsa_print_object_default_draw;
    G_OBJECT_CLASS(klass)->finalize = balsa_print_object_default_destroy;
}


static void
balsa_print_object_default_init(GTypeInstance * instance, gpointer g_class)
{
    BalsaPrintObjectDefault *po = BALSA_PRINT_OBJECT_DEFAULT(instance);

    po->pixbuf = NULL;
    po->description = NULL;
}


static void
balsa_print_object_default_destroy(GObject * self)
{
    BalsaPrintObjectDefault *po = BALSA_PRINT_OBJECT_DEFAULT(self);

    if (po->pixbuf)
	g_object_unref(po->pixbuf);
    g_free(po->description);

    G_OBJECT_CLASS(parent_class)->finalize(self);
}


GList *
balsa_print_object_default(GList * list,
			   GtkPrintContext * context,
			   LibBalsaMessageBody * body,
			   BalsaPrintSetup * psetup)
{
    BalsaPrintObjectDefault *pod;
    BalsaPrintObject *po;
    gchar *conttype;
    PangoFontDescription *header_font;
    PangoLayout *test_layout;
    PangoTabArray *tabs;
    GString *desc_buf;
    gdouble c_max_height;
    gchar *part_desc;

    pod = g_object_new(BALSA_TYPE_PRINT_OBJECT_DEFAULT, NULL);
    g_assert(pod != NULL);
    po = BALSA_PRINT_OBJECT(pod);

    /* create the part */
    po->depth = psetup->curr_depth;
    po->c_width =
	psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;

    /* get a pixbuf according to the mime type */
    conttype = libbalsa_message_body_get_mime_type(body);
    pod->pixbuf =
	libbalsa_icon_finder(NULL, conttype, NULL, NULL,
                             GTK_ICON_SIZE_DND);
    pod->c_image_width = gdk_pixbuf_get_width(pod->pixbuf);
    pod->c_image_height = gdk_pixbuf_get_height(pod->pixbuf);


    /* create a layout for calculating the maximum label width */
    header_font =
	pango_font_description_from_string(balsa_app.print_header_font);
    test_layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(test_layout, header_font);
    pango_font_description_free(header_font);
    desc_buf = g_string_new("");

    /* add type and filename (if available) */
    pod->p_label_width =
	p_string_width_from_layout(test_layout, _("Type:"));
    if ((part_desc = libbalsa_vfs_content_description(conttype)))
	g_string_append_printf(desc_buf, "%s\t%s (%s)", _("Type:"),
			       part_desc, conttype);
    else
	g_string_append_printf(desc_buf, "%s\t%s", _("Type:"), conttype);
    g_free(part_desc);
    g_free(conttype);
    if (body->filename) {
	gint p_fnwidth =
	    p_string_width_from_layout(test_layout, _("File name:"));

	if (p_fnwidth > pod->p_label_width)
	    pod->p_label_width = p_fnwidth;
	g_string_append_printf(desc_buf, "\n%s\t%s", _("File name:"),
			       body->filename);
    }

    /* add a small space between label and value */
    pod->p_label_width += C_TO_P(C_LABEL_SEP);

    /* configure the layout so we can calculate the text height */
    pango_layout_set_indent(test_layout, -pod->p_label_width);
    tabs =
	pango_tab_array_new_with_positions(1, FALSE, PANGO_TAB_LEFT,
					   pod->p_label_width);
    pango_layout_set_tabs(test_layout, tabs);
    pango_tab_array_free(tabs);
    pango_layout_set_width(test_layout,
			   C_TO_P(po->c_width -
				  4 * C_LABEL_SEP - pod->c_image_width));
    pango_layout_set_alignment(test_layout, PANGO_ALIGN_LEFT);
    pod->c_text_height =
	P_TO_C(p_string_height_from_layout(test_layout, desc_buf->str));
    pod->description = g_string_free(desc_buf, FALSE);

    /* check if we should move to the next page */
    c_max_height = MAX(pod->c_text_height, pod->c_image_height);
    if (psetup->c_y_pos + c_max_height > psetup->c_height) {
	psetup->c_y_pos = 0;
	psetup->page_count++;
    }

    /* remember the extent */
    po->on_page = psetup->page_count - 1;
    po->c_at_x = psetup->c_x0 + po->depth * C_LABEL_SEP;
    po->c_at_y = psetup->c_y0 + psetup->c_y_pos;
    po->c_width = psetup->c_width - 2 * po->depth * C_LABEL_SEP;
    po->c_height = c_max_height;

    /* adjust the y position */
    psetup->c_y_pos += c_max_height;

    return g_list_append(list, po);
}


static void
balsa_print_object_default_draw(BalsaPrintObject * self,
				GtkPrintContext * context,
				cairo_t * cairo_ctx)
{
    BalsaPrintObjectDefault *pod;
    gdouble c_max_height;
    gdouble c_offset;
    PangoLayout *layout;
    PangoFontDescription *font;
    PangoTabArray *tabs;

    /* set up */
    pod = BALSA_PRINT_OBJECT_DEFAULT(self);
    g_assert(pod != NULL);
    c_max_height = MAX(pod->c_text_height, pod->c_image_height);
    c_offset = pod->c_image_width + 4 * C_LABEL_SEP;

    /* print the icon */
    if (pod->pixbuf)
        cairo_print_pixbuf(cairo_ctx, pod->pixbuf, self->c_at_x,
                           self->c_at_y, 1.0);

    /* print the description */
    font = pango_font_description_from_string(balsa_app.print_header_font);
    layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_indent(layout, -pod->p_label_width);
    tabs =
	pango_tab_array_new_with_positions(1, FALSE, PANGO_TAB_LEFT,
					   pod->p_label_width);
    pango_layout_set_tabs(layout, tabs);
    pango_tab_array_free(tabs);
    pango_layout_set_width(layout, C_TO_P(self->c_width - c_offset));
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_text(layout, pod->description, -1);
    cairo_move_to(cairo_ctx, self->c_at_x + c_offset,
		  self->c_at_y + (c_max_height -
				  pod->c_text_height) * 0.5);
    pango_cairo_show_layout(cairo_ctx, layout);
    g_object_unref(G_OBJECT(layout));
}
