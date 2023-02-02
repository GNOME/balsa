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
static void balsa_print_object_text_destroy(GObject * self);

static void balsa_print_object_text_draw(BalsaPrintObject * self,
					 GtkPrintContext * context,
					 cairo_t * cairo_ctx);

static GList * collect_attrs(GList * all_attr, guint offset, guint len);
static PangoAttrList * phrase_list_to_pango(GList * phrase_list);
static GList * phrase_highlight(const gchar * buffer, gunichar tag_char,
				PhraseType tag_type, GList * phrase_list);


struct _BalsaPrintObjectText {
    BalsaPrintObject parent;

    gint p_label_width;
    gchar *text;
    guint cite_level;
    GList *attributes;
};


G_DEFINE_TYPE(BalsaPrintObjectText, balsa_print_object_text, BALSA_TYPE_PRINT_OBJECT)


static void
balsa_print_object_text_class_init(BalsaPrintObjectTextClass *klass)
{
    BALSA_PRINT_OBJECT_CLASS(klass)->draw =	balsa_print_object_text_draw;
    G_OBJECT_CLASS(klass)->finalize = balsa_print_object_text_destroy;
}


static void
balsa_print_object_text_init(BalsaPrintObjectText *self)
{
	self->text = NULL;
	self->attributes = NULL;
}


static void
balsa_print_object_text_destroy(GObject * self)
{
    BalsaPrintObjectText *po = BALSA_PRINT_OBJECT_TEXT(self);

    g_list_free_full(po->attributes, g_free);
    g_free(po->text);

    G_OBJECT_CLASS(balsa_print_object_text_parent_class)->finalize(self);
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
    BalsaPrintRect rect;
    guint first_page;
    gchar * par_start;
    gchar * eol;
    gint par_len;

    /* set up the regular expression for qouted text */
    if (!(rex = balsa_quote_regex_new()))
	return balsa_print_object_default(list, context, body, psetup);

    /* start on new page if less than 2 lines can be printed */
    if (psetup->c_y_pos + 2 * P_TO_C(psetup->p_body_font_height) > psetup->c_height) {
    	psetup->c_y_pos = 0;
    	psetup->page_count++;
    }
    rect.c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    rect.c_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;
    rect.c_height = -1.0;	/* height is calculated when the object is drawn */

    /* copy the text body to a buffer */
    if (body->buffer != NULL) {
    	textbuf = g_strdup(body->buffer);
    } else {
    	libbalsa_message_body_get_content(body, &textbuf, NULL);
    }

    /* fake an empty buffer if textbuf is NULL */
    if (textbuf == NULL) {
    	textbuf = g_strdup("");
    }

    /* be sure the we have correct utf-8 stuff here... */
    libbalsa_utf8_sanitize(&textbuf, balsa_app.convert_unknown_8bit, NULL);

    /* apply flowed if requested */
    if (libbalsa_message_body_is_flowed(body)) {
    	GString *flowed;

    	flowed = libbalsa_process_text_rfc2646(textbuf, G_MAXINT, FALSE, FALSE,
    					FALSE, libbalsa_message_body_is_delsp(body));
    	g_free(textbuf);
    	textbuf = g_string_free(flowed, FALSE);
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
    	pango_layout_set_width(layout, C_TO_P(rect.c_width - curr_level * C_LABEL_SEP));

    	/* highlight structured phrases if requested */
    	if (balsa_app.print_highlight_phrases) {
    		attr_list = phrase_highlight(level_buf->str, '*', PHRASE_BF, NULL);
    		attr_list = phrase_highlight(level_buf->str, '/', PHRASE_EM, attr_list);
    		attr_list = phrase_highlight(level_buf->str, '_', PHRASE_UL, attr_list);
    	} else {
    		attr_list = NULL;
    	}

    	/* start on new page if less than one line can be printed */
    	if (psetup->c_y_pos + P_TO_C(psetup->p_body_font_height) > psetup->c_height) {
    		psetup->c_y_pos = 0;
    		psetup->page_count++;
    	}

    	/* split paragraph if necessary */
    	pango_attr_list = phrase_list_to_pango(attr_list);
    	first_page = psetup->page_count - 1;
    	c_at_y = psetup->c_y_pos;
    	par_parts = split_for_layout(layout, level_buf->str, pango_attr_list, psetup, FALSE, &attr_offs);
    	if (pango_attr_list != NULL) {
    		pango_attr_list_unref(pango_attr_list);
    	}
    	g_object_unref(layout);
    	g_string_free(level_buf, TRUE);

    	/* each part is a new text object */
    	for (this_par_part = par_parts; this_par_part != NULL; this_par_part = this_par_part->next) {
    		BalsaPrintObjectText *pot;

    		pot = g_object_new(BALSA_TYPE_PRINT_OBJECT_TEXT, NULL);
    		g_assert(pot != NULL);
    		balsa_print_object_set_page_depth(BALSA_PRINT_OBJECT(pot), first_page++, psetup->curr_depth);
    		rect.c_at_y = psetup->c_y0 + c_at_y;
    		balsa_print_object_set_rect(BALSA_PRINT_OBJECT(pot), &rect);
    		c_at_y = 0.0;
    		pot->text = (gchar *) this_par_part->data;
    		pot->cite_level = curr_level;
    		pot->attributes = collect_attrs(attr_list, g_array_index(attr_offs, guint, 0), strlen(pot->text));

    		list = g_list_append(list, pot);
    		g_array_remove_index(attr_offs, 0);
    	}
    	if (attr_list) {
    		g_list_free_full(attr_list, g_free);
    	}
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
    BalsaPrintRect rect;
    guint first_page;
    GList *par_parts;
    GList *this_par_part;
    PangoLayout *layout;
    gdouble c_at_y;

    /* start on new page if less than 2 lines can be printed */
    if (psetup->c_y_pos + 2 * P_TO_C(psetup->p_body_font_height) > psetup->c_height) {
    	psetup->c_y_pos = 0;
    	psetup->page_count++;
    }
    rect.c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    rect.c_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;
    rect.c_height = -1.0;	/* height is calculated when the object is drawn */

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
    pango_layout_set_width(layout, C_TO_P(rect.c_width));

    /* split paragraph if necessary */
    first_page = psetup->page_count - 1;
    c_at_y = psetup->c_y_pos;
    par_parts = split_for_layout(layout, textbuf, NULL, psetup, FALSE, NULL);
    g_object_unref(layout);
    pango_font_description_free(font);
    g_free(textbuf);

    /* each part is a new text object */
    for (this_par_part = par_parts; this_par_part != NULL; this_par_part = this_par_part->next) {
    	BalsaPrintObjectText *pot;

    	pot = g_object_new(BALSA_TYPE_PRINT_OBJECT_TEXT, NULL);
    	g_assert(pot != NULL);
    	balsa_print_object_set_page_depth(BALSA_PRINT_OBJECT(pot), first_page++, psetup->curr_depth);
    	rect.c_at_y = psetup->c_y0 + c_at_y;
    	balsa_print_object_set_rect(BALSA_PRINT_OBJECT(pot), &rect);
    	c_at_y = 0.0;

    	pot->text = (gchar *) this_par_part->data;
    	pot->cite_level = 0;
    	pot->attributes = NULL;

    	list = g_list_append(list, pot);
    }
    g_list_free(par_parts);

    return list;
}

static GdkPixbuf *
get_icon(const gchar         *icon_name,
		 LibBalsaMessageBody *body)
{
    gint width;
    gint height;
    GdkPixbuf *pixbuf;

    if (!gtk_icon_size_lookup(GTK_ICON_SIZE_DND, &width, &height)) {
        width = 16;
    }

    pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), icon_name, width, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    if (pixbuf == NULL) {
    	gchar *conttype = libbalsa_message_body_get_mime_type(body);

    	pixbuf = libbalsa_icon_finder(NULL, conttype, NULL, NULL, GTK_ICON_SIZE_DND);
    	g_free(conttype);
    }

    return pixbuf;
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
balsa_print_object_text_vcard(GList               *list,
			      	  	  	  GtkPrintContext     *context,
							  LibBalsaMessageBody *body,
							  BalsaPrintSetup     *psetup)
{
    GdkPixbuf *pixbuf;
    PangoFontDescription *header_font;
    PangoLayout *test_layout;
    GString *desc_buf;
    gint p_label_width;
    LibBalsaAddress * address = NULL;
    gchar *textbuf;
    GList *result;

    g_return_val_if_fail((context != NULL) && (body != NULL) && (psetup != NULL), list);

    /* check if we can create an address from the body and fall back to default if 
     * this fails */
    if (body->buffer != NULL) {
    	textbuf = g_strdup(body->buffer);
    } else {
    	libbalsa_message_body_get_content(body, &textbuf, NULL);
    }
    if (textbuf != NULL) {
    	address = libbalsa_address_new_from_vcard(textbuf, body->charset);
    }
    if (address == NULL) {
    	g_free(textbuf);
    	return balsa_print_object_text(list, context, body, psetup);
    }

    /* get the identity icon or the mime type icon on fail */
    pixbuf = get_icon("x-office-address-book", body);

    /* create a layout for calculating the maximum label width */
    header_font = pango_font_description_from_string(balsa_app.print_header_font);
    test_layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(test_layout, header_font);
    pango_font_description_free(header_font);

    /* add fields from the address */
    desc_buf = g_string_new("");
    p_label_width = 0;
    ADD_VCARD_FIELD(desc_buf, p_label_width, test_layout, libbalsa_address_get_full_name(address), _("Full Name"));
    ADD_VCARD_FIELD(desc_buf, p_label_width, test_layout, libbalsa_address_get_nick_name(address), _("Nick Name"));
    ADD_VCARD_FIELD(desc_buf, p_label_width, test_layout, libbalsa_address_get_first_name(address), _("First Name"));
    ADD_VCARD_FIELD(desc_buf, p_label_width, test_layout, libbalsa_address_get_last_name(address), _("Last Name"));
    ADD_VCARD_FIELD(desc_buf, p_label_width, test_layout, libbalsa_address_get_organization(address), _("Organization"));
    ADD_VCARD_FIELD(desc_buf, p_label_width, test_layout, libbalsa_address_get_addr(address), _("Email Address"));
    g_object_unref(address);

    /* add a small space between label and value */
    p_label_width += C_TO_P(C_LABEL_SEP);

    /* create the part and clean up */
    textbuf = g_string_free(desc_buf, FALSE);
    result = balsa_print_object_default_full(list, context, pixbuf, textbuf, p_label_width, psetup);
    g_object_unref(pixbuf);
    g_free(textbuf);
    g_object_unref(test_layout);

    return result;
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

#define ADD_VCAL_DATE(buf, labwidth, layout, event, date_id, descr)						\
	G_STMT_START {                                                                		\
    	gchar *_dstr = libbalsa_vevent_time_str(event, date_id, balsa_app.date_string);	\
        if (_dstr != NULL) {                                      						\
            ADD_VCAL_FIELD(buf, labwidth, layout, _dstr, descr);        				\
            g_free(_dstr);                                              				\
        }                                                               				\
    } G_STMT_END

#define ADD_VCAL_ADDRESS(buf, labwidth, layout, addr, descr)            \
    do {                                                                \
        if (addr) {                                                     \
            gchar * _astr = libbalsa_vcal_attendee_to_str(addr);        \
            ADD_VCAL_FIELD(buf, labwidth, layout, _astr, descr);        \
            g_free(_astr);                                              \
        }                                                               \
    } while (0)


GList *
balsa_print_object_text_calendar(GList               *list,
                                 GtkPrintContext     *context,
                                 LibBalsaMessageBody *body,
                                 BalsaPrintSetup     *psetup)
{
    LibBalsaVCal *vcal_obj;
    GdkPixbuf *pixbuf;
    PangoFontDescription *header_font;
    PangoLayout *test_layout;
    GString *desc_buf;
    gint p_label_width;
    gchar *textbuf;
    guint event_no;

    g_return_val_if_fail((context != NULL) && (body != NULL) && (psetup != NULL), list);

    /* check if we can evaluate the body as calendar object and fall back to text if not */
    vcal_obj = libbalsa_vcal_new_from_body(body);
    if (vcal_obj == NULL) {
    	return balsa_print_object_text(list, context, body, psetup);
    }

    /* get the stock calendar icon or the mime type icon on fail */
    pixbuf = get_icon("x-office-calendar", body);

    /* create a layout for calculating the maximum label width */
    header_font = pango_font_description_from_string(balsa_app.print_header_font);
    test_layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(test_layout, header_font);
    pango_font_description_free(header_font);

    /* add fields from the events*/
    desc_buf = g_string_new(NULL);
    g_string_append_printf(desc_buf, _("This is an iTIP calendar “%s” message."), libbalsa_vcal_method_str(vcal_obj));
    p_label_width = 0;
    for (event_no = 0U; event_no < libbalsa_vcal_vevent_count(vcal_obj); event_no++) {
        LibBalsaVEvent *event = libbalsa_vcal_vevent(vcal_obj, event_no);
        gchar *buffer;
        const gchar *description;
        guint attendees;

        g_string_append_c(desc_buf, '\n');
        ADD_VCAL_FIELD(desc_buf, p_label_width, test_layout, libbalsa_vevent_summary(event), _("Summary:"));
        if (libbalsa_vevent_status(event) != ICAL_STATUS_NONE) {
        	ADD_VCAL_FIELD(desc_buf, p_label_width, test_layout, libbalsa_vevent_status_str(event), _("Status:"));
        }
        ADD_VCAL_ADDRESS(desc_buf, p_label_width, test_layout, libbalsa_vevent_organizer(event), _("Organizer:"));
        ADD_VCAL_DATE(desc_buf, p_label_width, test_layout, event, VEVENT_DATETIME_STAMP, _("Created:"));
        ADD_VCAL_DATE(desc_buf, p_label_width, test_layout, event, VEVENT_DATETIME_START, _("Start:"));
        ADD_VCAL_DATE(desc_buf, p_label_width, test_layout, event, VEVENT_DATETIME_END, _("End:"));

        buffer = libbalsa_vevent_duration_str(event);
        if (buffer != NULL) {
        	ADD_VCAL_FIELD(desc_buf, p_label_width, test_layout, buffer, _("Duration:"));
        	g_free(buffer);
        }

        buffer = libbalsa_vevent_recurrence_str(event, balsa_app.date_string);
        if (buffer != NULL) {
        	ADD_VCAL_FIELD(desc_buf, p_label_width, test_layout, buffer, _("Recurrence:"));
        	g_free(buffer);
        }

        ADD_VCAL_FIELD(desc_buf, p_label_width, test_layout, libbalsa_vevent_location(event), _("Location:"));

        attendees = libbalsa_vevent_attendees(event);
        if (attendees > 0U) {
            gchar *this_att;
            guint n;

            this_att = libbalsa_vcal_attendee_to_str(libbalsa_vevent_attendee(event, 0U));
            ADD_VCAL_FIELD(desc_buf, p_label_width, test_layout, this_att,
                           ngettext("Attendee:", "Attendees:", attendees));
            g_free(this_att);
            for (n = 1U; n < attendees; n++) {
                this_att = libbalsa_vcal_attendee_to_str(libbalsa_vevent_attendee(event, n));
                g_string_append_printf(desc_buf, "\n\t%s", this_att);
                g_free(this_att);
            }
        }

        description = libbalsa_vevent_description(event);
        if (description != NULL) {
            gchar **desc_lines = g_strsplit(description, "\n", -1);
            gint i;

            ADD_VCAL_FIELD(desc_buf, p_label_width, test_layout, desc_lines[0], _("Description:"));
            for (i = 1; desc_lines[i]; i++) {
                g_string_append_printf(desc_buf, "\n\t%s", desc_lines[i]);
            }
            g_strfreev(desc_lines);
        }

        if (libbalsa_vevent_category_count(event) > 0U) {
        	gchar **cat_lines;
            gint i;

            buffer = libbalsa_vevent_category_str(event);
        	cat_lines = g_strsplit(buffer, "\n", -1);
        	g_free(buffer);
        	ADD_VCAL_FIELD(desc_buf, p_label_width, test_layout, cat_lines[0],
        		ngettext("Category:", "Categories:", libbalsa_vevent_category_count(event)));
            for (i = 1; cat_lines[i]; i++) {
                g_string_append_printf(desc_buf, "\n\t%s", cat_lines[i]);
            }
            g_strfreev(cat_lines);
        }
    }
    g_object_unref(vcal_obj);

    /* add a small space between label and value */
    p_label_width += C_TO_P(C_LABEL_SEP);

    /* create the part and clean up */
    textbuf = g_string_free(desc_buf, FALSE);
    list = balsa_print_object_default_full(list, context, pixbuf, textbuf, p_label_width, psetup);
    g_object_unref(pixbuf);
    g_free(textbuf);
    g_object_unref(test_layout);

    return list;
}


static void
balsa_print_object_text_draw(BalsaPrintObject * self,
			     GtkPrintContext * context,
			     cairo_t * cairo_ctx)
{
    BalsaPrintObjectText *po;
    const BalsaPrintRect *rect;
    PangoFontDescription *font;
    gint p_height;
    PangoLayout *layout;
    PangoAttrList *attr_list;

    po = BALSA_PRINT_OBJECT_TEXT(self);
    rect = balsa_print_object_get_rect(self);
    g_assert(po != NULL);

    /* prepare */
    font = pango_font_description_from_string(balsa_app.print_body_font);
    layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_width(layout, C_TO_P(rect->c_width - po->cite_level * C_LABEL_SEP));
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
    cairo_move_to(cairo_ctx, rect->c_at_x + po->cite_level * C_LABEL_SEP, rect->c_at_y);
    pango_cairo_show_layout(cairo_ctx, layout);
    g_object_unref(layout);
    if (po->cite_level > 0) {
	guint n;

	cairo_new_path(cairo_ctx);
	cairo_set_line_width(cairo_ctx, 1.0);
	for (n = 0; n < po->cite_level; n++) {
	    gdouble c_xpos = rect->c_at_x + 0.5 + n * C_LABEL_SEP;

	    cairo_move_to(cairo_ctx, c_xpos, rect->c_at_y);
	    cairo_line_to(cairo_ctx, c_xpos, rect->c_at_y + P_TO_C(p_height));
	}
	cairo_stroke(cairo_ctx);
	cairo_restore(cairo_ctx);
    }

    balsa_print_object_set_height(self, P_TO_C(p_height));	/* needed to properly print borders */
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

	phrase_list = g_list_next(phrase_list);
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
	    PhraseRegion *this_reg;

	    this_reg = g_new(PhraseRegion, 1);
            *this_reg = *region;

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
	all_attr = g_list_next(all_attr);
    }

    return attr;
}
