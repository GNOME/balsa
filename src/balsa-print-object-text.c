/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-print-object-text.h"

#include <gtk/gtk.h>
#include <string.h>
#include <glib/gi18n.h>
#include "libbalsa.h"
#include "rfc2445.h"
#include "balsa-icons.h"
#include "balsa-print-object.h"
#include "balsa-print-object-decor.h"
#include "balsa-print-object-default.h"


typedef enum {
    PHRASE_BF = 0,
    PHRASE_EM,
    PHRASE_UL,
    PHRASE_TYPE_COUNT
} PhraseType;

typedef struct {
    PhraseType phrase_type;
    guint start_index;
    guint end_index;
} PhraseRegion;


/* object related functions */
static void balsa_print_object_text_class_init(BalsaPrintObjectTextClass * klass);
static void balsa_print_object_text_init(GTypeInstance * instance,
					 gpointer g_class);
static void balsa_print_object_text_finalize(GObject * self);

static void balsa_print_object_text_draw(BalsaPrintObject * self,
					 GtkPrintContext * context,
					 cairo_t * cairo_ctx);

static GList * collect_attrs(GList * all_attr, guint offset, guint len);
static PangoAttrList * phrase_list_to_pango(GList * phrase_list);
static GList * phrase_highlight(const gchar * buffer, gunichar tag_char,
				PhraseType tag_type, GList * phrase_list);


static BalsaPrintObjectClass *parent_class = NULL;


GType
balsa_print_object_text_get_type()
{
    static GType balsa_print_object_text_type = 0;

    if (!balsa_print_object_text_type) {
	static const GTypeInfo balsa_print_object_text_info = {
	    sizeof(BalsaPrintObjectTextClass),
	    NULL,		/* base_init */
	    NULL,		/* base_finalize */
	    (GClassInitFunc) balsa_print_object_text_class_init,
	    NULL,		/* class_finalize */
	    NULL,		/* class_data */
	    sizeof(BalsaPrintObjectText),
	    0,			/* n_preallocs */
	    (GInstanceInitFunc) balsa_print_object_text_init
	};

	balsa_print_object_text_type =
	    g_type_register_static(BALSA_TYPE_PRINT_OBJECT,
				   "BalsaPrintObjectText",
				   &balsa_print_object_text_info, 0);
    }

    return balsa_print_object_text_type;
}


static void
balsa_print_object_text_class_init(BalsaPrintObjectTextClass * klass)
{
    parent_class = g_type_class_ref(BALSA_TYPE_PRINT_OBJECT);
    BALSA_PRINT_OBJECT_CLASS(klass)->draw =
	balsa_print_object_text_draw;
    G_OBJECT_CLASS(klass)->finalize = balsa_print_object_text_finalize;
}


static void
balsa_print_object_text_init(GTypeInstance * instance, gpointer g_class)
{
    BalsaPrintObjectText *po = BALSA_PRINT_OBJECT_TEXT(instance);

    po->text = NULL;
    po->attributes = NULL;
}


static void
balsa_print_object_text_finalize(GObject * self)
{
    BalsaPrintObjectText *po = BALSA_PRINT_OBJECT_TEXT(self);

    g_list_free_full(po->attributes, g_free);
    g_free(po->text);

    G_OBJECT_CLASS(parent_class)->finalize(self);
}

/* prepare a text/plain part, which gets
 * - citation bars and colourisation of cited text (prefs dependant)
 * - syntax highlighting (prefs dependant)
 * - RFC 3676 "flowed" processing */
GList *
balsa_print_object_text_plain(GList *list, GtkPrintContext * context,
			      LibBalsaMessageBody * body,
			      BalsaPrintSetup * psetup)
{
    GRegex *rex;
    gchar *textbuf;
    PangoFontDescription *font;
    gdouble c_at_x;
    gdouble c_use_width;
    guint first_page;
    gchar * par_start;
    gchar * eol;
    gint par_len;

    /* set up the regular expression for qouted text */
    if (!(rex = balsa_quote_regex_new()))
	return balsa_print_object_default(list, context, body, psetup);

    /* start on new page if less than 2 lines can be printed */
    if (psetup->c_y_pos + 2 * P_TO_C(psetup->p_body_font_height) >
	psetup->c_height) {
	psetup->c_y_pos = 0;
	psetup->page_count++;
    }
    c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    c_use_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;

    /* copy the text body to a buffer */
    if (body->buffer)
	textbuf = g_strdup(body->buffer);
    else
	libbalsa_message_body_get_content(body, &textbuf, NULL);

    /* fake an empty buffer if textbuf is NULL */
    if (!textbuf)
	textbuf = g_strdup("");

    /* be sure the we have correct utf-8 stuff here... */
    libbalsa_utf8_sanitize(&textbuf, balsa_app.convert_unknown_8bit, NULL);

    /* apply flowed if requested */
    if (libbalsa_message_body_is_flowed(body)) {
	GString *flowed;

	flowed =
	    libbalsa_process_text_rfc2646(textbuf, G_MAXINT, FALSE, FALSE,
					  FALSE,
					  libbalsa_message_body_is_delsp
					  (body));
	g_free(textbuf);
	textbuf = flowed->str;
	g_string_free(flowed, FALSE);
    }

    /* get the font */
    font = pango_font_description_from_string(balsa_app.print_body_font);

    /* loop over paragraphs */
    par_start = textbuf;
    eol = strchr(par_start, '\n');
    par_len = eol ? eol - par_start : (gint) strlen(par_start);
    while (*par_start) {
	GString *level_buf;
	guint curr_level;
	guint cite_level;
	GList *par_parts;
	GList *this_par_part;
	GList *attr_list;
	PangoLayout *layout;
	PangoAttrList *pango_attr_list;
	GArray *attr_offs;
	gdouble c_at_y;

	level_buf = NULL;
	curr_level = 0;		/* just to make the compiler happy */
	do {
	    gchar *thispar;
	    guint cite_idx;

	    thispar = g_strndup(par_start, par_len);

	    /* get the cite level and strip off the prefix */
	    if (libbalsa_match_regex(thispar, rex, &cite_level, &cite_idx))
            {
		gchar *new;

		new = thispar + cite_idx;
		if (g_unichar_isspace(g_utf8_get_char(new)))
		    new = g_utf8_next_char(new);
		new = g_strdup(new);
		g_free(thispar);
		thispar = new;
	    }

	    /* glue paragraphs with the same cite level together */
	    if (!level_buf || (curr_level == cite_level)) {
		if (!level_buf) {
		    level_buf = g_string_new(thispar);
		    curr_level = cite_level;
		} else {
		    level_buf = g_string_append_c(level_buf, '\n');
		    level_buf = g_string_append(level_buf, thispar);
		}

		par_start = eol ? eol + 1 : par_start + par_len;
		eol = strchr(par_start, '\n');
		par_len = eol ? eol - par_start : (gint) strlen(par_start);
	    }

	    g_free(thispar);
	} while (*par_start && (curr_level == cite_level));
	
	/* configure the layout so we can use Pango to split the text into pages */
	layout = gtk_print_context_create_pango_layout(context);
	pango_layout_set_font_description(layout, font);
	pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);

	/* leave place for the citation bars */
	pango_layout_set_width(layout,
			       C_TO_P(c_use_width - curr_level * C_LABEL_SEP));

	/* highlight structured phrases if requested */
	if (balsa_app.print_highlight_phrases) {
	    attr_list =
		phrase_highlight(level_buf->str, '*', PHRASE_BF, NULL);
	    attr_list =
		phrase_highlight(level_buf->str, '/', PHRASE_EM, attr_list);
	    attr_list =
		phrase_highlight(level_buf->str, '_', PHRASE_UL, attr_list);
	} else
	    attr_list = NULL;

	/* start on new page if less than one line can be printed */
	if (psetup->c_y_pos + P_TO_C(psetup->p_body_font_height) >
	    psetup->c_height) {
	    psetup->c_y_pos = 0;
	    psetup->page_count++;
	}

	/* split paragraph if necessary */
	pango_attr_list = phrase_list_to_pango(attr_list);
	first_page = psetup->page_count - 1;
	c_at_y = psetup->c_y_pos;
	par_parts =
	    split_for_layout(layout, level_buf->str, pango_attr_list,
			     psetup, FALSE, &attr_offs);
	if (pango_attr_list)
	    pango_attr_list_unref(pango_attr_list);
	g_object_unref(G_OBJECT(layout));
	g_string_free(level_buf, TRUE);

	/* each part is a new text object */
	this_par_part = par_parts;
	while (this_par_part) {
	    BalsaPrintObjectText *pot;

	    pot = g_object_new(BALSA_TYPE_PRINT_OBJECT_TEXT, NULL);
	    g_assert(pot != NULL);
	    BALSA_PRINT_OBJECT(pot)->on_page = first_page++;
	    BALSA_PRINT_OBJECT(pot)->c_at_x = c_at_x;
	    BALSA_PRINT_OBJECT(pot)->c_at_y = psetup->c_y0 + c_at_y;
	    BALSA_PRINT_OBJECT(pot)->depth = psetup->curr_depth;
	    c_at_y = 0.0;
	    BALSA_PRINT_OBJECT(pot)->c_width = c_use_width;
	    /* note: height is calculated when the object is drawn */
	    pot->text = (gchar *) this_par_part->data;
	    pot->cite_level = curr_level;
	    pot->attributes =
		collect_attrs(attr_list,
			      g_array_index(attr_offs, guint, 0),
			      strlen(pot->text));

	    list = g_list_append(list, pot);
	    g_array_remove_index(attr_offs, 0);
	    this_par_part = this_par_part->next;
	}
	g_list_free_full(attr_list, g_free);
	g_list_free(par_parts);
	g_array_free(attr_offs, TRUE);
    }

    /* clean up */
    pango_font_description_free(font);
    g_free(textbuf);
    g_regex_unref(rex);
    return list;
}


/* prepare a text part which is simply printed "as is" without all the bells
 * and whistles of text/plain (see above) */
GList *
balsa_print_object_text(GList *list, GtkPrintContext * context,
			LibBalsaMessageBody * body,
			BalsaPrintSetup * psetup)
{
    gchar *textbuf;
    PangoFontDescription *font;
    gdouble c_at_x;
    gdouble c_use_width;
    guint first_page;
    GList *par_parts;
    GList *this_par_part;
    PangoLayout *layout;
    gdouble c_at_y;

    /* start on new page if less than 2 lines can be printed */
    if (psetup->c_y_pos + 2 * P_TO_C(psetup->p_body_font_height) >
	psetup->c_height) {
	psetup->c_y_pos = 0;
	psetup->page_count++;
    }
    c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    c_use_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;

    /* copy the text body to a buffer */
    if (body->buffer)
	textbuf = g_strdup(body->buffer);
    else
	libbalsa_message_body_get_content(body, &textbuf, NULL);

    /* fake an empty buffer if textbuf is NULL */
    if (!textbuf)
	textbuf = g_strdup("");

    /* be sure the we have correct utf-8 stuff here... */
    libbalsa_utf8_sanitize(&textbuf, balsa_app.convert_unknown_8bit, NULL);

    /* get the font */
    font = pango_font_description_from_string(balsa_app.print_body_font);

    /* configure the layout so we can use Pango to split the text into pages */
    layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout, C_TO_P(c_use_width));

    /* split paragraph if necessary */
    first_page = psetup->page_count - 1;
    c_at_y = psetup->c_y_pos;
    par_parts = split_for_layout(layout, textbuf, NULL, psetup, FALSE, NULL);
    g_object_unref(G_OBJECT(layout));
    pango_font_description_free(font);
    g_free(textbuf);

    /* each part is a new text object */
    this_par_part = par_parts;
    while (this_par_part) {
	BalsaPrintObjectText *pot;

	pot = g_object_new(BALSA_TYPE_PRINT_OBJECT_TEXT, NULL);
	g_assert(pot != NULL);
	BALSA_PRINT_OBJECT(pot)->on_page = first_page++;
	BALSA_PRINT_OBJECT(pot)->c_at_x = c_at_x;
	BALSA_PRINT_OBJECT(pot)->c_at_y = psetup->c_y0 + c_at_y;
	BALSA_PRINT_OBJECT(pot)->depth = psetup->curr_depth;
	c_at_y = 0.0;
	BALSA_PRINT_OBJECT(pot)->c_width = c_use_width;
	/* note: height is calculated when the object is drawn */
	pot->text = (gchar *) this_par_part->data;
	pot->cite_level = 0;
	pot->attributes = NULL;

	list = g_list_append(list, pot);
	this_par_part = this_par_part->next;
    }
    g_list_free(par_parts);

    return list;
}


/* note: a vcard is an icon plus a series of labels/text, so this function actually
 * returns a BalsaPrintObjectDefault... */

#define ADD_VCARD_FIELD(buf, labwidth, layout, field, descr)		\
    do {								\
	if (field) {							\
	    gint label_width = p_string_width_from_layout(layout, descr); \
	    if (label_width > labwidth)					\
		labwidth = label_width;					\
	    if ((buf)->len > 0)						\
		g_string_append_c(buf, '\n');				\
	    g_string_append_printf(buf, "%s\t%s", descr, field);	\
	}								\
    } while(0)

GList *
balsa_print_object_text_vcard(GList * list,
			      GtkPrintContext * context,
			      LibBalsaMessageBody * body,
			      BalsaPrintSetup * psetup)
{
    BalsaPrintObjectDefault *pod;
    BalsaPrintObject *po;
    PangoFontDescription *header_font;
    PangoLayout *test_layout;
    PangoTabArray *tabs;
    GString *desc_buf;
    gdouble c_max_height;
    LibBalsaAddress * addr = NULL;
    gchar *textbuf;

    /* check if we can create an address from the body and fall back to default if 
    * this fails */
    if (body->buffer)
	textbuf = g_strdup(body->buffer);
    else
	libbalsa_message_body_get_content(body, &textbuf, NULL);
    if (textbuf)
        addr = libbalsa_address_new_from_vcard(textbuf, body->charset);
    if (!addr) {
	g_free(textbuf);
	return balsa_print_object_text(list, context, body, psetup);
    }

    /* proceed with the address information */
    pod = g_object_new(BALSA_TYPE_PRINT_OBJECT_DEFAULT, NULL);
    g_assert(pod != NULL);
    po = BALSA_PRINT_OBJECT(pod);

    /* create the part */
    po->depth = psetup->curr_depth;
    po->c_width =
	psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;

    /* get the stock contacts icon or the mime type icon on fail */
    pod->pixbuf =
	gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
				 BALSA_PIXMAP_IDENTITY, 48,
				 GTK_ICON_LOOKUP_USE_BUILTIN, NULL);
    if (!pod->pixbuf) {
	gchar *conttype = libbalsa_message_body_get_mime_type(body);

	pod->pixbuf = libbalsa_icon_finder(NULL, conttype, NULL, NULL, 48);
    }
    pod->c_image_width = gdk_pixbuf_get_width(pod->pixbuf);
    pod->c_image_height = gdk_pixbuf_get_height(pod->pixbuf);


    /* create a layout for calculating the maximum label width */
    header_font =
	pango_font_description_from_string(balsa_app.print_header_font);
    test_layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(test_layout, header_font);
    pango_font_description_free(header_font);

    /* add fields from the address */
    desc_buf = g_string_new("");
    pod->p_label_width = 0;
    ADD_VCARD_FIELD(desc_buf, pod->p_label_width, test_layout,
		    addr->full_name,    _("Full Name"));
    ADD_VCARD_FIELD(desc_buf, pod->p_label_width, test_layout,
		    addr->nick_name,    _("Nick Name"));
    ADD_VCARD_FIELD(desc_buf, pod->p_label_width, test_layout,
		    addr->first_name,   _("First Name"));
    ADD_VCARD_FIELD(desc_buf, pod->p_label_width, test_layout,
		    addr->last_name,    _("Last Name"));
    ADD_VCARD_FIELD(desc_buf, pod->p_label_width, test_layout,
		    addr->organization, _("Organization"));
    if (addr->address_list)
        ADD_VCARD_FIELD(desc_buf, pod->p_label_width, test_layout,
			(const gchar *) addr->address_list->data, _("Email Address"));
    g_object_unref(addr);

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


/* add a text/calendar object */

#define ADD_VCAL_FIELD(buf, labwidth, layout, field, descr)		\
    do {								\
	if (field) {							\
	    gint label_width = p_string_width_from_layout(layout, descr); \
	    if (label_width > labwidth)					\
		labwidth = label_width;					\
	    if ((buf)->len > 0)						\
		g_string_append_c(buf, '\n');				\
	    g_string_append_printf(buf, "%s\t%s", descr, field);	\
	}								\
    } while(0)

#define ADD_VCAL_DATE(buf, labwidth, layout, date, descr)               \
    do {                                                                \
        if (date != (time_t) -1) {                                      \
            gchar * _dstr =                                             \
                libbalsa_date_to_utf8(date, balsa_app.date_string);     \
            ADD_VCAL_FIELD(buf, labwidth, layout, _dstr, descr);        \
            g_free(_dstr);                                              \
        }                                                               \
    } while (0)

#define ADD_VCAL_ADDRESS(buf, labwidth, layout, addr, descr)            \
    do {                                                                \
        if (addr) {                                                     \
            gchar * _astr = libbalsa_vcal_attendee_to_str(addr);        \
            ADD_VCAL_FIELD(buf, labwidth, layout, _astr, descr);        \
            g_free(_astr);                                              \
        }                                                               \
    } while (0)


GList *
balsa_print_object_text_calendar(GList * list,
                                 GtkPrintContext * context,
                                 LibBalsaMessageBody * body,
                                 BalsaPrintSetup * psetup)
{
    BalsaPrintObjectDefault *pod;
    BalsaPrintObject *po;
    PangoFontDescription *header_font;
    PangoLayout *test_layout;
    PangoTabArray *tabs;
    GString *desc_buf;
    LibBalsaVCal * vcal_obj;
    GList * this_ev;
    guint first_page;
    GList *par_parts;
    GList *this_par_part;
    gdouble c_at_y;

    /* check if we can evaluate the body as calendar object and fall back
     * to text if not */
    if (!(vcal_obj = libbalsa_vcal_new_from_body(body)))
	return balsa_print_object_text(list, context, body, psetup);

    /* proceed with the address information */
    pod = g_object_new(BALSA_TYPE_PRINT_OBJECT_DEFAULT, NULL);
    g_assert(pod != NULL);
    po = BALSA_PRINT_OBJECT(pod);

    /* create the part */
    po->depth = psetup->curr_depth;
    po->c_width =
	psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;

    /* get the stock calendar icon or the mime type icon on fail */
    pod->pixbuf =
	gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
				 "x-office-calendar", 48,
				 GTK_ICON_LOOKUP_USE_BUILTIN, NULL);
    if (!pod->pixbuf) {
	gchar *conttype = libbalsa_message_body_get_mime_type(body);

	pod->pixbuf = libbalsa_icon_finder(NULL, conttype, NULL, NULL, 48);
    }
    pod->c_image_width = gdk_pixbuf_get_width(pod->pixbuf);
    pod->c_image_height = gdk_pixbuf_get_height(pod->pixbuf);

    /* move to the next page if the icon doesn't fit */
    if (psetup->c_y_pos + pod->c_image_height > psetup->c_height) {
	psetup->c_y_pos = 0;
	psetup->page_count++;
    }

    /* create a layout for calculating the maximum label width and for splitting
     * the body (if necessary) */
    header_font =
	pango_font_description_from_string(balsa_app.print_header_font);
    test_layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(test_layout, header_font);
    pango_font_description_free(header_font);

    /* add fields from the events*/
    desc_buf = g_string_new("");
    pod->p_label_width = 0;
    for (this_ev = vcal_obj->vevent; this_ev != NULL; this_ev = this_ev->next) {
        LibBalsaVEvent * event = (LibBalsaVEvent *) this_ev->data;

        if (desc_buf->len > 0)
            g_string_append_c(desc_buf, '\n');
        ADD_VCAL_FIELD(desc_buf, pod->p_label_width, test_layout,
                       event->summary, _("Summary"));
        ADD_VCAL_ADDRESS(desc_buf, pod->p_label_width, test_layout,
                         event->organizer, _("Organizer"));
        ADD_VCAL_DATE(desc_buf, pod->p_label_width, test_layout,
                      event->start, _("Start"));
        ADD_VCAL_DATE(desc_buf, pod->p_label_width, test_layout,
                      event->end, _("End"));
        ADD_VCAL_FIELD(desc_buf, pod->p_label_width, test_layout,
                       event->location, _("Location"));
        if (event->attendee) {
            GList * att = event->attendee;
            gchar * this_att;

            this_att =
                libbalsa_vcal_attendee_to_str(LIBBALSA_ADDRESS(att->data));
            att = att->next;
            ADD_VCAL_FIELD(desc_buf, pod->p_label_width, test_layout,
                           this_att, att ? _("Attendees") : _("Attendee"));
            g_free(this_att);
            for (; att != NULL; att = att->next) {
                this_att =
                    libbalsa_vcal_attendee_to_str(LIBBALSA_ADDRESS(att->data));
                g_string_append_printf(desc_buf, "\n\t%s", this_att);
                g_free(this_att);
            }
        }
        if (event->description) {
            gchar ** desc_lines = g_strsplit(event->description, "\n", -1);
            gint i;

            ADD_VCAL_FIELD(desc_buf, pod->p_label_width, test_layout,
                           desc_lines[0], _("Description"));
            for (i = 1; desc_lines[i]; i++)
                g_string_append_printf(desc_buf, "\n\t%s", desc_lines[i]);
            g_strfreev(desc_lines);
        }
    }
    g_object_unref(vcal_obj);

    /* add a small space between label and value */
    pod->p_label_width += C_TO_P(C_LABEL_SEP);

    /* configure the layout so we can split the text */
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

    /* split paragraph if necessary */
    first_page = psetup->page_count - 1;
    c_at_y = psetup->c_y_pos;
    par_parts =
        split_for_layout(test_layout, desc_buf->str, NULL, psetup, TRUE, NULL);
    g_string_free(desc_buf, TRUE);

    /* set the parameters of the first part */
    pod->description = (gchar *) par_parts->data;
    pod->c_text_height =
	P_TO_C(p_string_height_from_layout(test_layout, pod->description));
    po->on_page = first_page++;
    po->c_at_x = psetup->c_x0 + po->depth * C_LABEL_SEP;
    po->c_at_y = psetup->c_y0 + c_at_y;
    po->c_height = MAX(pod->c_image_height, pod->c_text_height);
    list = g_list_append(list, pod);

    /* add more parts */
    for (this_par_part = par_parts->next; this_par_part != NULL;
         this_par_part = this_par_part->next) {
        BalsaPrintObjectDefault * new_pod;
        BalsaPrintObject *new_po;
        
        /* create a new object */
        new_pod = g_object_new(BALSA_TYPE_PRINT_OBJECT_DEFAULT, NULL);
        g_assert(new_pod != NULL);
        new_po = BALSA_PRINT_OBJECT(new_pod);

        /* fill data */
        new_pod->p_label_width = pod->p_label_width;
        new_pod->c_image_width = pod->c_image_width;
        new_pod->description = (gchar *) this_par_part->data;
        new_pod->c_text_height =
            P_TO_C(p_string_height_from_layout(test_layout, new_pod->description));
        new_po->on_page = first_page++;
        new_po->c_at_x = psetup->c_x0 + po->depth * C_LABEL_SEP;
        new_po->c_at_y = psetup->c_y0;
        new_po->c_height = new_pod->c_text_height;
        new_po->depth = psetup->curr_depth;
        new_po->c_width =
            psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;
        
        /* append */
        list = g_list_append(list, new_pod);
    }
    g_list_free(par_parts);
    g_object_unref(G_OBJECT(test_layout));

    return list;
}


static void
balsa_print_object_text_draw(BalsaPrintObject * self,
			     GtkPrintContext * context,
			     cairo_t * cairo_ctx)
{
    BalsaPrintObjectText *po;
    PangoFontDescription *font;
    gint p_height;
    PangoLayout *layout;
    PangoAttrList *attr_list;

    po = BALSA_PRINT_OBJECT_TEXT(self);
    g_assert(po != NULL);

    /* prepare */
    font = pango_font_description_from_string(balsa_app.print_body_font);
    layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_width(layout,
			   C_TO_P(self->c_width - po->cite_level * C_LABEL_SEP));
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, po->text, -1);
    if ((attr_list = phrase_list_to_pango(po->attributes))) {
	pango_layout_set_attributes(layout, attr_list);
	pango_attr_list_unref(attr_list);
    }
    pango_layout_get_size(layout, NULL, &p_height);
    if (po->cite_level > 0) {
	cairo_save(cairo_ctx);
	if (balsa_app.print_highlight_cited) {
	    gint k = (po->cite_level - 1) % MAX_QUOTED_COLOR;

	    cairo_set_source_rgb(cairo_ctx,
				 balsa_app.quoted_color[k].red,
				 balsa_app.quoted_color[k].green,
				 balsa_app.quoted_color[k].blue);
	}
    }
    cairo_move_to(cairo_ctx, self->c_at_x + po->cite_level * C_LABEL_SEP,
		  self->c_at_y);
    pango_cairo_show_layout(cairo_ctx, layout);
    g_object_unref(G_OBJECT(layout));
    if (po->cite_level > 0) {
	guint n;

	cairo_new_path(cairo_ctx);
	cairo_set_line_width(cairo_ctx, 1.0);
	for (n = 0; n < po->cite_level; n++) {
	    gdouble c_xpos = self->c_at_x + 0.5 + n * C_LABEL_SEP;

	    cairo_move_to(cairo_ctx, c_xpos, self->c_at_y);
	    cairo_line_to(cairo_ctx, c_xpos, self->c_at_y + P_TO_C(p_height));
	}
	cairo_stroke(cairo_ctx);
	cairo_restore(cairo_ctx);
    }

    self->c_height = P_TO_C(p_height);	/* needed to properly print borders */
}


#define UNICHAR_PREV(p)  g_utf8_get_char(g_utf8_prev_char(p))

static GList *
phrase_highlight(const gchar * buffer, gunichar tag_char,
		 PhraseType tag_type, GList * phrase_list)
{
    gchar *utf_start;

    /* find the tag char in the text and scan the buffer for
       <buffer start or whitespace><tag char><alnum><any text><alnum><tagchar>
       <whitespace, punctuation or buffer end> */
    utf_start = g_utf8_strchr(buffer, -1, tag_char);
    while (utf_start) {
	gchar *s_next = g_utf8_next_char(utf_start);

	if ((utf_start == buffer
	     || g_unichar_isspace(UNICHAR_PREV(utf_start)))
	    && *s_next != '\0'
	    && g_unichar_isalnum(g_utf8_get_char(s_next))) {
	    gchar *utf_end;
	    gchar *line_end;
	    gchar *e_next;

	    /* found a proper start sequence - find the end or eject */
	    if (!(utf_end = g_utf8_strchr(s_next, -1, tag_char)))
		return phrase_list;
	    line_end = g_utf8_strchr(s_next, -1, '\n');
	    e_next = g_utf8_next_char(utf_end);
	    while (!g_unichar_isalnum(UNICHAR_PREV(utf_end)) ||
		   !(*e_next == '\0' ||
		     g_unichar_isspace(g_utf8_get_char(e_next)) ||
		     g_unichar_ispunct(g_utf8_get_char(e_next)))) {
		if (!(utf_end = g_utf8_strchr(e_next, -1, tag_char)))
		    return phrase_list;
		e_next = g_utf8_next_char(utf_end);
	    }

	    /* append the attribute if there is no line break */
	    if (!line_end || line_end >= e_next) {
		PhraseRegion *new_region = g_new0(PhraseRegion, 1);

		new_region->phrase_type = tag_type;
		new_region->start_index = utf_start - buffer;
		new_region->end_index = e_next - buffer;
		phrase_list = g_list_prepend(phrase_list, new_region);

		/* set the next start properly */
		utf_start =
		    *e_next ? g_utf8_strchr(e_next, -1, tag_char) : NULL;
	    } else
		utf_start =
		    *s_next ? g_utf8_strchr(s_next, -1, tag_char) : NULL;
	} else
	    /* no start sequence, find the next start tag char */
	    utf_start =
		*s_next ? g_utf8_strchr(s_next, -1, tag_char) : NULL;
    }

    return phrase_list;
}


static PangoAttrList *
phrase_list_to_pango(GList * phrase_list)
{
    PangoAttrList *attr_list;
    PangoAttribute *ph_attr[PHRASE_TYPE_COUNT];
    gint n;

    if (!phrase_list)
	return NULL;

    attr_list = pango_attr_list_new();
    ph_attr[PHRASE_BF] = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    ph_attr[PHRASE_EM] = pango_attr_style_new(PANGO_STYLE_ITALIC);
    ph_attr[PHRASE_UL] = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);

    while (phrase_list) {
	PhraseRegion *region = (PhraseRegion *) phrase_list->data;
	PangoAttribute *new_attr;

	new_attr = pango_attribute_copy(ph_attr[region->phrase_type]);
	new_attr->start_index = region->start_index;
	new_attr->end_index = region->end_index;
	pango_attr_list_insert(attr_list, new_attr);

	phrase_list = phrase_list->next;
    }

    for (n = 0; n < PHRASE_TYPE_COUNT; n++)
	pango_attribute_destroy(ph_attr[n]);

    return attr_list;
}


static GList *
collect_attrs(GList * all_attr, guint offset, guint len)
{
    GList *attr = NULL;

    while (all_attr) {
	PhraseRegion *region = (PhraseRegion *) all_attr->data;

	if ((region->start_index >= offset
	     && region->start_index <= offset + len)
	    || (region->end_index >= offset
		&& region->end_index <= offset + len)) {
	    PhraseRegion *this_reg =
		g_memdup(region, sizeof(PhraseRegion));

	    if (this_reg->start_index < offset)
		this_reg->start_index = 0;
	    else
		this_reg->start_index -= offset;
	    if (this_reg->end_index > offset + len)
		this_reg->end_index = len;
	    else
		this_reg->end_index -= offset;
	    attr = g_list_prepend(attr, this_reg);
	}
	all_attr = all_attr->next;
    }

    return attr;
}
