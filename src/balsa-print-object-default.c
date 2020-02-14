/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2019 Stuart Parmenter and others
 * Written by (C) Albrecht Dreß <albrecht.dress@arcor.de> 2007
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-print-object-default.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "balsa-print-object.h"
#include "libbalsa-vfs.h"


struct _BalsaPrintObjectDefault {
    BalsaPrintObject parent;

    gint p_label_width;
    gdouble c_image_width;
    gdouble c_image_height;
    gdouble c_text_height;
    gchar *description;
    GdkPixbuf *pixbuf;
};


G_DEFINE_TYPE(BalsaPrintObjectDefault, balsa_print_object_default, BALSA_TYPE_PRINT_OBJECT)


/* object related functions */
static void balsa_print_object_default_destroy(GObject * self);

static void balsa_print_object_default_draw(BalsaPrintObject * self,
					    GtkPrintContext * context,
					    cairo_t * cairo_ctx);


static void
balsa_print_object_default_class_init(BalsaPrintObjectDefaultClass * klass)
{
    BALSA_PRINT_OBJECT_CLASS(klass)->draw = balsa_print_object_default_draw;
    G_OBJECT_CLASS(klass)->finalize = balsa_print_object_default_destroy;
}


static void
balsa_print_object_default_init(BalsaPrintObjectDefault *self)
{
    self->pixbuf = NULL;
    self->description = NULL;
}


static void
balsa_print_object_default_destroy(GObject * self)
{
    BalsaPrintObjectDefault *po = BALSA_PRINT_OBJECT_DEFAULT(self);

    if (po->pixbuf != NULL) {
    	g_object_unref(po->pixbuf);
    }
    g_free(po->description);
    G_OBJECT_CLASS(balsa_print_object_default_parent_class)->finalize(self);
}


GList *
balsa_print_object_default(GList 			   *list,
			   	   	   	   GtkPrintContext 	   *context,
						   LibBalsaMessageBody *body,
						   BalsaPrintSetup     *psetup)
{
	GdkPixbuf *pixbuf;
	gint p_label_width;
    PangoFontDescription *header_font;
    PangoLayout *test_layout;
    gchar *conttype;
    GString *desc_buf;
    gchar *part_desc;
    gchar *description;

    g_return_val_if_fail((context != NULL) && (body != NULL) && (psetup != NULL), list);

    /* get a pixbuf according to the mime type */
    conttype = libbalsa_message_body_get_mime_type(body);
    pixbuf = libbalsa_icon_finder(NULL, conttype, NULL, NULL, GTK_ICON_SIZE_DND);

    /* create a layout for calculating the maximum label width */
    header_font = pango_font_description_from_string(balsa_app.print_header_font);
    test_layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(test_layout, header_font);
    pango_font_description_free(header_font);
    desc_buf = g_string_new("");

    /* add type and filename (if available) */
    p_label_width = p_string_width_from_layout(test_layout, _("Type:"));
    part_desc = libbalsa_vfs_content_description(conttype);
    if (part_desc != NULL) {
    	g_string_append_printf(desc_buf, "%s\t%s (%s)", _("Type:"), part_desc, conttype);
    	g_free(part_desc);
    } else {
    	g_string_append_printf(desc_buf, "%s\t%s", _("Type:"), conttype);
    }
    g_free(conttype);

    if (body->filename != NULL) {
    	gint p_fnwidth = p_string_width_from_layout(test_layout, _("File name:"));

    	if (p_fnwidth > p_label_width) {
    		p_label_width = p_fnwidth;
    	}
    	g_string_append_printf(desc_buf, "\n%s\t%s", _("File name:"), body->filename);
    }
    g_object_unref(test_layout);

    /* add a small space between label and value */
    p_label_width += C_TO_P(C_LABEL_SEP);

    /* create the part and clean up */
    description = g_string_free(desc_buf, FALSE);
    list = balsa_print_object_default_full(list, context, pixbuf, description, p_label_width, psetup);
    g_object_unref(pixbuf);
    g_free(description);

    return list;
}


GList *
balsa_print_object_default_full(GList           *list,
		  	  	  	  	  	  	GtkPrintContext *context,
								GdkPixbuf       *pixbuf,
								const gchar     *description,
								gint             p_label_width,
								BalsaPrintSetup *psetup)
{
    BalsaPrintObjectDefault *pod;
    BalsaPrintObject *po;
    BalsaPrintRect rect;
    PangoFontDescription *header_font;
    PangoLayout *test_layout;
    PangoTabArray *tabs;
    guint first_page;
    GList *par_parts;
    GList *this_par_part;
    gdouble c_at_y;

	g_return_val_if_fail((context != NULL) && (pixbuf != NULL) && (description != NULL) && (psetup != NULL), list);

    pod = g_object_new(BALSA_TYPE_PRINT_OBJECT_DEFAULT, NULL);
    g_assert(pod != NULL);
    po = BALSA_PRINT_OBJECT(pod);

    /* icon: ref pixbuf, get size */
    pod->pixbuf = g_object_ref(pixbuf);
    pod->c_image_width = gdk_pixbuf_get_width(pod->pixbuf);
    pod->c_image_height = gdk_pixbuf_get_height(pod->pixbuf);

    /* move to the next page if the icon doesn't fit */
    if (psetup->c_y_pos + pod->c_image_height > psetup->c_height) {
    	psetup->c_y_pos = 0.0;
    	psetup->page_count++;
    }

    /* some basics */
    rect.c_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;
    pod->p_label_width = p_label_width;

    /* configure a layout so we can calculate the text height */
    header_font = pango_font_description_from_string(balsa_app.print_header_font);
    test_layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(test_layout, header_font);
    pango_font_description_free(header_font);
    pango_layout_set_indent(test_layout, -pod->p_label_width);
    tabs = pango_tab_array_new_with_positions(1, FALSE, PANGO_TAB_LEFT, pod->p_label_width);
    pango_layout_set_tabs(test_layout, tabs);
    pango_tab_array_free(tabs);
    pango_layout_set_width(test_layout, C_TO_P(rect.c_width - 4 * C_LABEL_SEP - pod->c_image_width));
    pango_layout_set_alignment(test_layout, PANGO_ALIGN_LEFT);

    /* split text if necessary */
    first_page = psetup->page_count - 1;
    c_at_y = psetup->c_y_pos;
    par_parts = split_for_layout(test_layout, description, NULL, psetup, TRUE, NULL);

    /* set the parameters of the first part */
    pod->description = (gchar *) par_parts->data;
    pod->c_text_height = P_TO_C(p_string_height_from_layout(test_layout, pod->description));
    balsa_print_object_set_page_depth(po, first_page++, psetup->curr_depth);
    rect.c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    rect.c_at_y = psetup->c_y0 + c_at_y;
    rect.c_height = MAX(pod->c_image_height, pod->c_text_height);
    balsa_print_object_set_rect(po, &rect);
    list = g_list_append(list, pod);

    /* adjust printing position calculated by split_for_layout if a single text chunk is smaller than the pixmap */
    if ((par_parts->next == NULL) && (pod->c_image_height > pod->c_text_height)) {
    	psetup->c_y_pos += pod->c_image_height - pod->c_text_height;
    }

    /* add more parts */
    for (this_par_part = par_parts->next; this_par_part != NULL; this_par_part = this_par_part->next) {
        BalsaPrintObjectDefault *new_pod;
        BalsaPrintObject *new_po;

        /* create a new object */
        new_pod = g_object_new(BALSA_TYPE_PRINT_OBJECT_DEFAULT, NULL);
        g_assert(new_pod != NULL);
        new_po = BALSA_PRINT_OBJECT(new_pod);

        /* fill data */
        new_pod->p_label_width = pod->p_label_width;
        new_pod->c_image_width = pod->c_image_width;
        new_pod->description = (gchar *) this_par_part->data;
        new_pod->c_text_height = P_TO_C(p_string_height_from_layout(test_layout, new_pod->description));
        balsa_print_object_set_page_depth(new_po, first_page++, psetup->curr_depth);
        rect.c_at_y = psetup->c_y0;
        rect.c_height = new_pod->c_text_height;
        balsa_print_object_set_rect(new_po, &rect);

        /* append */
        list = g_list_append(list, new_pod);
    }
    g_list_free(par_parts);
    g_object_unref(test_layout);

    return list;
}


static void
balsa_print_object_default_draw(BalsaPrintObject * self,
				GtkPrintContext * context,
				cairo_t * cairo_ctx)
{
    BalsaPrintObjectDefault *pod;
    const BalsaPrintRect *rect;
    gdouble c_max_height;
    gdouble c_offset;
    PangoLayout *layout;
    PangoFontDescription *font;
    PangoTabArray *tabs;

    /* set up */
    pod = BALSA_PRINT_OBJECT_DEFAULT(self);
    rect = balsa_print_object_get_rect(self);
    g_assert(pod != NULL);

    c_max_height = MAX(pod->c_text_height, pod->c_image_height);
    c_offset = pod->c_image_width + 4 * C_LABEL_SEP;

    /* print the icon */
    if (pod->pixbuf != NULL) {
        cairo_print_pixbuf(cairo_ctx, pod->pixbuf, rect->c_at_x, rect->c_at_y, 1.0);
    }

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
    pango_layout_set_width(layout, C_TO_P(rect->c_width - c_offset));
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_text(layout, pod->description, -1);
    cairo_move_to(cairo_ctx, rect->c_at_x + c_offset,
    		rect->c_at_y + (c_max_height - pod->c_text_height) * 0.5);
    pango_cairo_show_layout(cairo_ctx, layout);
    g_object_unref(layout);
}
