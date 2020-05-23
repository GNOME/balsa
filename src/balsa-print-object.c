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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-print-object.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libbalsa.h>
#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-print-object-decor.h"
#include "balsa-print-object-default.h"
#include "balsa-print-object-header.h"
#include "balsa-print-object-image.h"
#include "balsa-print-object-text.h"
#include "balsa-print-object-html.h"


typedef struct {
    gint on_page;
    guint depth;

    BalsaPrintRect rect;
} BalsaPrintObjectPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(BalsaPrintObject, balsa_print_object, G_TYPE_OBJECT)


/* object related functions */
static void balsa_print_object_destroy(GObject * object);


static void
balsa_print_object_init(BalsaPrintObject *self)
{
	BalsaPrintObjectPrivate *priv = balsa_print_object_get_instance_private(self);

	priv->on_page = 0;
	priv->depth = 0;
	priv->rect.c_at_x = 0.0;
	priv->rect.c_at_y = 0.0;
	priv->rect.c_width = 0.0;
	priv->rect.c_height = 0.0;
}


static void
balsa_print_object_class_init(BalsaPrintObjectClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = balsa_print_object_destroy;
    klass->draw = balsa_print_object_draw;
}


static GList *
balsa_print_object_emb_message(GList * list, GtkPrintContext * context,
				     LibBalsaMessageBody * mime_body,
				     BalsaPrintSetup * psetup)
{
    list = balsa_print_object_frame_begin(list, NULL, psetup);
    return balsa_print_object_header_from_body(list, context, mime_body, psetup);
}

static GList *
balsa_print_object_emb_headers(GList * list, GtkPrintContext * context,
	     LibBalsaMessageBody * mime_body,
	     BalsaPrintSetup * psetup)
{
    list = balsa_print_object_frame_begin(list, _("message headers"), psetup);
    list = balsa_print_object_header_from_body(list, context, mime_body, psetup);
    return balsa_print_object_frame_end(list, psetup);
}

static GList *
balsa_print_object_mp_crypto(GList * list, GtkPrintContext * context,
                               LibBalsaMessageBody * mime_body,
                               BalsaPrintSetup * psetup)
{
    return balsa_print_object_header_crypto(list, context, mime_body, psetup);
}


GList *
balsa_print_objects_append_from_body(GList * list,
				     GtkPrintContext * context,
				     LibBalsaMessageBody * mime_body,
				     BalsaPrintSetup * psetup)
{
    static const struct {
        char * type;  /* MIME type */
        int use_len;  /* length for strncasecmp or -1 for strcasecmp */
        GList * (*handler)(GList *, GtkPrintContext *, LibBalsaMessageBody *,
                           BalsaPrintSetup *);
    } pr_handlers[] = {
        { "text/html",                     -1, balsa_print_object_html },
        { "text/enriched",                 -1, balsa_print_object_default },
        { "text/richtext",                 -1, balsa_print_object_default },
        { "text/x-vcard",                  -1, balsa_print_object_text_vcard },
        { "text/directory",                -1, balsa_print_object_text_vcard },
        { "text/calendar",                 -1, balsa_print_object_text_calendar },
        { "text/plain",                    -1, balsa_print_object_text_plain },
        { "text/rfc822-headers",	   -1, balsa_print_object_emb_headers },
        { "text/",                          5, balsa_print_object_text },
        { "image/",                         6, balsa_print_object_image },
        { "message/rfc822",                -1, balsa_print_object_emb_message },
        { "application/pgp-signature",     -1, balsa_print_object_mp_crypto },
        { "application/pkcs7-signature",   -1, balsa_print_object_mp_crypto },
        { "application/x-pkcs7-signature", -1, balsa_print_object_mp_crypto },
        { NULL,                            -1, balsa_print_object_default }
    };
    gchar *conttype;
    GList *result;
    int n;

    conttype = libbalsa_message_body_get_mime_type(mime_body);
    for (n = 0;
         pr_handlers[n].type &&
             ((pr_handlers[n].use_len == -1 &&
               strcmp(pr_handlers[n].type, conttype) != 0) ||
              (pr_handlers[n].use_len > 0 &&
               strncmp(pr_handlers[n].type, conttype, pr_handlers[n].use_len) != 0));
         n++);
    result = pr_handlers[n].handler(list, context, mime_body, psetup);
    g_free(conttype);
    return result;
}


void
balsa_print_object_draw(BalsaPrintObject * self, GtkPrintContext * context,
			cairo_t * cairo_ctx)
{
	BalsaPrintObjectPrivate *priv = balsa_print_object_get_instance_private(self);
    guint level;

    g_return_if_fail(BALSA_IS_PRINT_OBJECT(self) && (context != NULL) && (cairo_ctx != NULL));

    BALSA_PRINT_OBJECT_CLASS(G_OBJECT_GET_CLASS(self))->draw(self, context, cairo_ctx);

    /* print borders if the depth is > 0 */
    if (priv->depth == 0)
	return;

    /* print the requested number of border lines */
    cairo_save(cairo_ctx);
    cairo_set_line_width(cairo_ctx, 0.25);
    cairo_new_path(cairo_ctx);
    for (level = priv->depth; level > 0U; level--) {
    	gdouble level_sep = level * C_LABEL_SEP;
    	BalsaPrintRect *rect = &priv->rect;

    	cairo_move_to(cairo_ctx, rect->c_at_x - level_sep, rect->c_at_y);
    	cairo_line_to(cairo_ctx, rect->c_at_x - level_sep, rect->c_at_y + rect->c_height);
    	cairo_move_to(cairo_ctx, rect->c_at_x + rect->c_width + level_sep, rect->c_at_y);
    	cairo_line_to(cairo_ctx, rect->c_at_x + rect->c_width + level_sep, rect->c_at_y + rect->c_height);
    }
    cairo_stroke(cairo_ctx);
    cairo_restore(cairo_ctx);
}


gint
balsa_print_object_get_page(BalsaPrintObject *self)
{
	BalsaPrintObjectPrivate *priv = balsa_print_object_get_instance_private(self);

	g_return_val_if_fail(BALSA_IS_PRINT_OBJECT(self), -1);
	return priv->on_page;
}


const BalsaPrintRect *
balsa_print_object_get_rect(BalsaPrintObject *self)
{
	BalsaPrintObjectPrivate *priv = balsa_print_object_get_instance_private(self);

	g_return_val_if_fail(BALSA_IS_PRINT_OBJECT(self), NULL);
	return &priv->rect;
}


void
balsa_print_object_set_page_depth(BalsaPrintObject *self,
								  gint              page,
								  guint             depth)
{
	BalsaPrintObjectPrivate *priv = balsa_print_object_get_instance_private(self);

	g_return_if_fail(BALSA_IS_PRINT_OBJECT(self));
	priv->depth = depth;
	priv->on_page = page;
}


void
balsa_print_object_set_rect(BalsaPrintObject     *self,
							const BalsaPrintRect *rect)
{
	BalsaPrintObjectPrivate *priv = balsa_print_object_get_instance_private(self);

	g_return_if_fail(BALSA_IS_PRINT_OBJECT(self));
	memcpy(&priv->rect, rect, sizeof(BalsaPrintRect));
}


void
balsa_print_object_set_height(BalsaPrintObject *self,
							  gdouble           height)
{
	BalsaPrintObjectPrivate *priv = balsa_print_object_get_instance_private(self);

	g_return_if_fail(BALSA_IS_PRINT_OBJECT(self));
	priv->rect.c_height = height;
}


static void
balsa_print_object_destroy(GObject *object)
{
    G_OBJECT_CLASS(balsa_print_object_parent_class)->finalize(object);
}


/*  == various print helper functions ==  */

/* return the width of the passed string in Pango units */
gint
p_string_width_from_layout(PangoLayout * layout, const gchar * text)
{
    gint width;

    pango_layout_set_text(layout, text, -1);
    pango_layout_get_size(layout, &width, NULL);
    return width;
}


/* return the height of the passed string in Pango units */
gint
p_string_height_from_layout(PangoLayout * layout, const gchar * text)
{
    gint height;

    pango_layout_set_text(layout, text, -1);
    pango_layout_get_size(layout, NULL, &height);
    return height;
}


/* print a GdkPixbuf to cairo at the specified position and with the
 * specified scale */
gboolean
cairo_print_pixbuf(cairo_t * cairo_ctx, const GdkPixbuf * pixbuf,
		   gdouble c_at_x, gdouble c_at_y, gdouble scale)
{
    guchar *raw_image;
    gint n_chans;
    guint32 *surface_buf;
    gint width;
    gint height;
    gint rowstride;
    guint32 *dest;
    cairo_format_t format;
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    cairo_matrix_t matrix;

    /* paranoia checks */
    g_return_val_if_fail(cairo_ctx && pixbuf, FALSE);

    /* must have 8 bpp */
    g_return_val_if_fail(gdk_pixbuf_get_bits_per_sample(pixbuf) == 8,
			 FALSE);

    /* must have 3 (no alpha) or 4 (with alpha) channels */
    n_chans = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(n_chans == 3 || n_chans == 4, FALSE);

    /* allocate a new buffer */
    /* FIXME: does this work on 64 bit machines if the witdth is odd? */
    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);
    if (!(surface_buf = g_new0(guint32, width * height)))
	return FALSE;

    /* copy pixbuf to a cairo buffer */
    dest = surface_buf;
    raw_image = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    if (n_chans == 4) {
	/* 4 channels: copy 32-bit vals, converting R-G-B-Alpha to
	 * Alpha-R-G-B... */
	gint line;

	format = CAIRO_FORMAT_ARGB32;
	for (line = 0; line < height; line++) {
	    guchar *src = raw_image + line * rowstride;
	    gint col;

	    for (col = width; col; col--, src += 4)
		*dest++ = (((((src[3] << 8) + src[0]) << 8) + src[1]) << 8) + src[2];
	}
    } else {
	/* 3 channels: copy 3 byte R-G-B to Alpha-R-G-B... */
	gint line;

	format = CAIRO_FORMAT_RGB24;
	for (line = 0; line < height; line++) {
	    guchar *src = raw_image + line * rowstride;
	    gint col;

	    for (col = width; col; col--, src += 3)
		*dest++ = (((src[0] << 8) + src[1]) << 8) + src[2];
	}
    }

    /* save current state */
    cairo_save(cairo_ctx);

    /* create the curface */
    surface =
	cairo_image_surface_create_for_data((unsigned char *) surface_buf,
					    format, width, height,
					    4 * width);
    cairo_set_source_surface(cairo_ctx, surface, c_at_x, c_at_y);

    /* scale */
    pattern = cairo_get_source(cairo_ctx);
    cairo_pattern_get_matrix(pattern, &matrix);
    matrix.xx /= scale;
    matrix.yy /= scale;
    matrix.x0 /= scale;
    matrix.y0 /= scale;
    cairo_pattern_set_matrix(pattern, &matrix);

    /* clip around the image */
    cairo_new_path(cairo_ctx);
    cairo_move_to(cairo_ctx, c_at_x, c_at_y);
    cairo_line_to(cairo_ctx, c_at_x + width * scale, c_at_y);
    cairo_line_to(cairo_ctx, c_at_x + width * scale,
		  c_at_y + height * scale);
    cairo_line_to(cairo_ctx, c_at_x, c_at_y + height * scale);
    cairo_close_path(cairo_ctx);
    cairo_clip(cairo_ctx);

    /* paint, restore and clean up */
    cairo_paint(cairo_ctx);
    cairo_restore(cairo_ctx);
    cairo_surface_destroy(surface);
    g_free(surface_buf);

    return TRUE;
}


/* split a text buffer into chunks using the passed Pango layout */
GList *
split_for_layout(PangoLayout * layout, const gchar * text,
		 PangoAttrList * attributes, BalsaPrintSetup * psetup,
		 gboolean is_header, GArray ** offsets)
{
    GList *split_list = NULL;
    PangoLayoutIter *iter;
    const gchar *start;
    gint p_offset;
    gboolean add_tab;
    gint p_y0;
    gint p_y1;
    gint p_y_pos;
    gint p_height;

    /* set the text and its attributes, then get an iter */
    pango_layout_set_text(layout, text, -1);
    if (attributes)
	pango_layout_set_attributes(layout, attributes);
    if (offsets)
	*offsets = g_array_new(FALSE, FALSE, sizeof(guint));
    iter = pango_layout_get_iter(layout);

    /* loop over lines */
    start = text;
    p_offset = 0;
    add_tab = FALSE;
    p_y_pos = C_TO_P(psetup->c_y_pos);
    p_height = C_TO_P(psetup->c_height);
    do {
	pango_layout_iter_get_line_yrange(iter, &p_y0, &p_y1);
	if (p_y_pos + p_y1 - p_offset > p_height) {
	    gint index;
	    gint tr;
	    gchar *chunk;
	    gboolean ends_with_nl;

	    if (offsets) {
		guint offs = start - text;

		*offsets = g_array_append_val(*offsets, offs);
	    }
	    pango_layout_xy_to_index(layout, 0, p_y0, &index, &tr);
	    ends_with_nl = text[index - 1] == '\n';
	    if (ends_with_nl)
		index--;
	    chunk = g_strndup(start, text + index - start);
	    if (add_tab)
		split_list =
		    g_list_append(split_list,
				  g_strconcat("\t", chunk, NULL));
	    else
		split_list = g_list_append(split_list, g_strdup(chunk));
	    add_tab = is_header && !ends_with_nl;
	    g_free(chunk);
	    start = text + index;
	    if (ends_with_nl)
		start++;
	    if (*start == '\0')
		p_y_pos = p_height;
	    else {
		p_y_pos = 0;
		psetup->page_count++;
	    }
	    p_offset = p_y0;
	}
    } while (pango_layout_iter_next_line(iter));
    pango_layout_iter_free(iter);

    /* append any remaining stuff */
    if (*start != '\0') {
	p_y_pos += p_y1 - p_offset;
	if (offsets) {
	    guint offs = start - text;

	    *offsets = g_array_append_val(*offsets, offs);
	}
	if (add_tab)
	    split_list =
		g_list_append(split_list, g_strconcat("\t", start, NULL));
	else
	    split_list = g_list_append(split_list, g_strdup(start));
    }

    /* remember the new y position in cairo units */
    psetup->c_y_pos = P_TO_C(p_y_pos);

    /* return the list */
    return split_list;
}
