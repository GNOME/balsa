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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-mime-widget-vcalendar.h"

#include "libbalsa.h"
#include "rfc2445.h"
#include "send.h"
#include "balsa-app.h"
#include <glib/gi18n.h>
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"


static GtkWidget *balsa_vevent_widget(LibBalsaVEvent  *event,
									  LibBalsaVCal    *vcal,
									  gboolean         may_reply,
									  InternetAddress *sender);
static void vevent_reply(GObject * button, GtkWidget * box);


BalsaMimeWidget *
balsa_mime_widget_new_vcalendar(BalsaMessage * bm,
				LibBalsaMessageBody * mime_body,
				const gchar * content_type, gpointer data)
{
    LibBalsaVCal *vcal_obj;
    BalsaMimeWidget *mw;
    GtkWidget *label;
    gchar *text;
    gchar *markup_buf;
    guint event_no;
    LibBalsaMessage *lbm = balsa_message_get_message(bm);
    gboolean may_reply = FALSE;
    InternetAddress *sender = NULL;

    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);
    g_return_val_if_fail(lbm != NULL, NULL);

    vcal_obj = libbalsa_vcal_new_from_body(mime_body);
    if (!vcal_obj)
	return NULL;

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

	/* Translators: #1 calendar message type */
    text = g_strdup_printf(_("This is an iTIP calendar “%s” message."),
			   libbalsa_vcal_method_str(vcal_obj));
    markup_buf = g_markup_printf_escaped("<b>%s</b>", text);
    g_free(text);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL (label), markup_buf);
    g_free(markup_buf);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(mw), label);

    /* a reply may be created only for unread requests */
    if ((libbalsa_vcal_method(vcal_obj) == ICAL_METHOD_REQUEST) &&
	LIBBALSA_MESSAGE_IS_UNREAD(lbm)) {
        LibBalsaMessageHeaders *headers;

	may_reply = TRUE;
        headers = libbalsa_message_get_headers(lbm);

	/* Note: usually, a VEvent reply is sent to the ORGANIZER; use the Reply-To:
	 * or From: address as fallback only if it is not specified */
	if (headers != NULL) {
	    if (headers->reply_to != NULL)
                sender = internet_address_list_get_address(headers->reply_to, 0);
	    else if (headers->from != NULL)
                sender = internet_address_list_get_address(headers->from, 0);
	}
    }

    /* add events */
    for (event_no = 0U; event_no < libbalsa_vcal_vevent_count(vcal_obj); event_no++) {
    	GtkWidget *frame;
    	GtkWidget *event;

    	frame = gtk_frame_new(NULL);
        gtk_container_add(GTK_CONTAINER(mw), frame);
    	event = balsa_vevent_widget(libbalsa_vcal_vevent(vcal_obj, event_no), vcal_obj, may_reply, sender);
        gtk_container_set_border_width(GTK_CONTAINER(event), 6U);
    	gtk_container_add(GTK_CONTAINER(frame), event);
    }

    g_object_unref(vcal_obj);

    return mw;
}

#define GRID_ATTACH(g,str,label)                                        \
    do {                                                                \
        if (str) {                                                      \
            GtkWidget *lbl = gtk_label_new(label);                      \
            gtk_widget_set_halign(lbl, GTK_ALIGN_START);                \
            gtk_widget_set_valign(lbl, GTK_ALIGN_START);                \
            gtk_grid_attach(g, lbl, 0, row, 1, 1);                      \
            lbl = libbalsa_create_wrap_label(str, FALSE);				\
            gtk_widget_set_valign(lbl, GTK_ALIGN_START);                \
            gtk_widget_set_hexpand(lbl, TRUE);                          \
            gtk_widget_set_vexpand(lbl, TRUE);                          \
            gtk_grid_attach(g, lbl, 1, row, 1, 1);                      \
            row++;                                                      \
        }                                                               \
    } while (0)

#define GRID_ATTACH_DATE(g,event,date_id,label)                        					\
	G_STMT_START {                                                                		\
    	gchar *_dstr = libbalsa_vevent_time_str(event, date_id, balsa_app.date_string);	\
    	if (_dstr != NULL) {															\
            GRID_ATTACH(g, _dstr, label);                               				\
            g_free(_dstr);                                              				\
        }                                                               				\
    } G_STMT_END

#define GRID_ATTACH_ADDRESS(g,addr,label)                               \
    do {                                                                \
        if (addr) {                                                     \
            gchar * _astr = libbalsa_vcal_attendee_to_str(addr);        \
            GRID_ATTACH(g, _astr, label);                               \
            g_free(_astr);                                              \
        }                                                               \
    } while (0)

#define GRID_ATTACH_TEXT(g,text,label)                                  \
    do {                                                                \
        if (text) {                                                     \
            GtkWidget *lbl = gtk_label_new(label);                      \
            GtkTextBuffer *tbuf = gtk_text_buffer_new(NULL);            \
            GtkWidget *tview;                                           \
            gtk_widget_set_halign(lbl, GTK_ALIGN_START);                \
            gtk_widget_set_valign(lbl, GTK_ALIGN_START);                \
            gtk_grid_attach(g, lbl, 0, row, 1, 1);                      \
            gtk_text_buffer_set_text(tbuf, text, -1);                   \
            tview = gtk_text_view_new_with_buffer(tbuf);                \
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tview),           \
                                        GTK_WRAP_WORD_CHAR);            \
            gtk_widget_set_hexpand(tview, TRUE);                        \
            gtk_widget_set_vexpand(tview, TRUE);                        \
            gtk_grid_attach(g, tview, 1, row, 1, 1);                    \
            row++;                                                      \
        }                                                               \
    } while (0)

static GtkWidget *
balsa_vevent_widget(LibBalsaVEvent *event, LibBalsaVCal *vcal, gboolean may_reply,
		    InternetAddress *sender)
{
    GtkGrid *grid;
    int row = 0;
    LibBalsaIdentity *vevent_ident = NULL;
    gchar *answer_to_mail = NULL;
    guint attendee;
    gchar *buffer;
    GString *all_atts = NULL;

    grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 12);
    GRID_ATTACH(grid, libbalsa_vevent_summary(event), _("Summary:"));
    if (libbalsa_vevent_status(event) != ICAL_STATUS_NONE) {
    	GRID_ATTACH(grid, libbalsa_vevent_status_str(event), _("Status:"));
    }
    GRID_ATTACH_ADDRESS(grid, libbalsa_vevent_organizer(event), _("Organizer:"));
    GRID_ATTACH_DATE(grid, event, VEVENT_DATETIME_STAMP, _("Created:"));
    GRID_ATTACH_DATE(grid, event, VEVENT_DATETIME_START, _("Start:"));
    GRID_ATTACH_DATE(grid, event, VEVENT_DATETIME_END, _("End:"));

    buffer = libbalsa_vevent_duration_str(event);
    if (buffer != NULL) {
    	GRID_ATTACH(grid, buffer, _("Duration:"));
    	g_free(buffer);
    }

    buffer = libbalsa_vevent_recurrence_str(event, balsa_app.date_string);
    if (buffer != NULL) {
    	GRID_ATTACH(grid, buffer, _("Recurrence:"));
    	g_free(buffer);
    }

    GRID_ATTACH(grid, libbalsa_vevent_location(event), _("Location:"));
    for (attendee = 0U; attendee < libbalsa_vevent_attendees(event); attendee++) {
	    LibBalsaAddress *lba = libbalsa_vevent_attendee(event, attendee);
	    gchar *this_att = libbalsa_vcal_attendee_to_str(lba);

	    if (all_atts)
		g_string_append_printf(all_atts, "\n%s", this_att);
	    else
		all_atts = g_string_new(this_att);
	    g_free(this_att);

	    if (may_reply && libbalsa_vcal_attendee_rsvp(lba)) {
                const gchar *addr = libbalsa_address_get_addr(lba);
                InternetAddress *ia = internet_address_mailbox_new(NULL, addr);
                GList *list;

                for (list = balsa_app.identities; list; list = list->next) {
                    LibBalsaIdentity *ident = list->data;
                    if (libbalsa_ia_rfc2821_equal(libbalsa_identity_get_address(ident),
                                                  ia)) {
                        vevent_ident = ident;
                        if (libbalsa_vevent_organizer(event) != NULL) {
                            answer_to_mail = libbalsa_address_to_gchar(libbalsa_vevent_organizer(event), 0);
                        } else if (sender != NULL) {
                            answer_to_mail = internet_address_to_string(sender, NULL, FALSE);
                        }
                        break;
                    }
                }
		g_object_unref(ia);
	    }
	}
    if (all_atts != NULL) {
	GRID_ATTACH(grid, all_atts->str,
                    ngettext("Attendee:", "Attendees:", libbalsa_vevent_attendees(event)));
	g_string_free(all_atts, TRUE);
    }

    if (libbalsa_vevent_category_count(event) > 0U) {
    	gchar *categories;

    	categories = libbalsa_vevent_category_str(event);
    	GRID_ATTACH(grid, categories, ngettext("Category:", "Categories:", libbalsa_vevent_category_count(event)));
    	g_free(categories);
    }

    GRID_ATTACH_TEXT(grid, libbalsa_vevent_description(event), _("Description:"));

    if (answer_to_mail != NULL) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *label;
	GtkWidget *bbox;
	GtkWidget *button;

	/* add the callback data to the event object */
	g_object_set_data_full(G_OBJECT(event), "ev:sender",
			       answer_to_mail,
			       (GDestroyNotify) g_free);
        g_object_set_data_full(G_OBJECT(event), "ev:ident",
                               g_object_ref(vevent_ident),
			       (GDestroyNotify) g_object_unref);

	/* pack everything into a box */
	gtk_container_add(GTK_CONTAINER(box), GTK_WIDGET(grid));
	label =
	    gtk_label_new(_("The sender asks you for a reply to this request:"));
	gtk_container_add(GTK_CONTAINER(box), label);
        bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(box), bbox);

        button = libbalsa_add_mnemonic_button_to_box(_("Accept"), bbox, GTK_ALIGN_CENTER);
	g_object_set_data(G_OBJECT(button), "event", event);

	/* Note: we must ref the full VCal object here as time zone information is stored in it.  Only ref'ing the event would thus
	 * lead to a segfault when a reply message is actually created. */
	g_object_set_data_full(G_OBJECT(button), "vcal", g_object_ref(vcal),
		(GDestroyNotify) g_object_unref);
	g_object_set_data(G_OBJECT(button), "mode",
			  GINT_TO_POINTER(ICAL_PARTSTAT_ACCEPTED));
	g_signal_connect(button, "clicked",
			 G_CALLBACK(vevent_reply), bbox);

        button = libbalsa_add_mnemonic_button_to_box(_("Accept tentatively"), bbox, GTK_ALIGN_CENTER);
	g_object_set_data(G_OBJECT(button), "event", event);
	g_object_set_data(G_OBJECT(button), "mode",
			  GINT_TO_POINTER(ICAL_PARTSTAT_TENTATIVE));
	g_signal_connect(button, "clicked",
			 G_CALLBACK(vevent_reply), bbox);

        button = libbalsa_add_mnemonic_button_to_box(_("Decline"), bbox, GTK_ALIGN_CENTER);
	g_object_set_data(G_OBJECT(button), "event", event);
	g_object_set_data(G_OBJECT(button), "mode",
			  GINT_TO_POINTER(ICAL_PARTSTAT_DECLINED));
	g_signal_connect(button, "clicked",
			 G_CALLBACK(vevent_reply), bbox);

	return box;
    } else
	return GTK_WIDGET(grid);
}

#define BUFFER_APPEND(b, l, s)							\
	G_STMT_START {										\
		if (s != NULL) {								\
			g_string_append_printf(b, "%s %s\n", l, s);	\
		}												\
    } G_STMT_END

#define BUFFER_APPEND_DATE(b, l, e, i)											\
	G_STMT_START {																\
    	gchar *_dstr = libbalsa_vevent_time_str(e, i, balsa_app.date_string);	\
    	if (_dstr != NULL) {													\
    		BUFFER_APPEND(b, l, _dstr);											\
            g_free(_dstr);                                         				\
        }                                                          				\
    } G_STMT_END

static void
vevent_reply(GObject * button, GtkWidget * box)
{
    LibBalsaVEvent *event =
	LIBBALSA_VEVENT(g_object_get_data(button, "event"));
    icalparameter_partstat pstat =
	GPOINTER_TO_INT(g_object_get_data(button, "mode"));
    gchar *rcpt;
    LibBalsaMessage *message;
    LibBalsaMessageHeaders *headers;
    LibBalsaMessageBody *body;
    const gchar *summary;
    GString *textbuf;
    gchar *dummy;
    GError *error = NULL;
    LibBalsaMsgCreateResult result;
    LibBalsaIdentity *ident;
    InternetAddress *ia;

    g_return_if_fail(event != NULL);
    rcpt = (gchar *) g_object_get_data(G_OBJECT(event), "ev:sender");
    g_return_if_fail(rcpt != NULL);
    ident = g_object_get_data(G_OBJECT(event), "ev:ident");
    g_return_if_fail(ident != NULL);
    ia = libbalsa_identity_get_address(ident);

    /* make the button box insensitive... */
    gtk_widget_set_sensitive(box, FALSE);

    /* create a message with the header set from the incoming message */
    message = libbalsa_message_new();
    headers = libbalsa_message_get_headers(message);
    headers->from = internet_address_list_new();
    internet_address_list_add(headers->from, ia);
    headers->to_list = internet_address_list_parse(libbalsa_parser_options(), rcpt);
    headers->date = time(NULL);

    /* create the message subject */
    summary = libbalsa_vevent_summary(event);
    dummy = g_strdup_printf("%s: %s",
			    (summary != NULL) ? summary : _("iTIP Calendar Request"),
			    libbalsa_vcal_part_stat_to_str(pstat));
    libbalsa_message_set_subject(message, dummy);
    g_free(dummy);
    libbalsa_message_set_subtype(message, "alternative");

    /* add an informational text part */
    body = libbalsa_message_body_new(message);
    body->charset = g_strdup("utf-8");
    body->content_type = NULL;
    textbuf = g_string_new(NULL);
	/* Translators: #1 message sender display name; #2 sender's event action (e.g. "accepted", "rejected", ...) */
    g_string_append_printf(textbuf, _("%s %s the following iTIP calendar request:\n\n"),
    	internet_address_get_name(ia), libbalsa_vcal_part_stat_to_str(pstat));
    BUFFER_APPEND(textbuf, _("Summary:"), libbalsa_vevent_summary(event));
    BUFFER_APPEND_DATE(textbuf, _("Start:"), event, VEVENT_DATETIME_START);
    BUFFER_APPEND_DATE(textbuf, _("End:"), event, VEVENT_DATETIME_END);
    BUFFER_APPEND(textbuf, _("Location:"), libbalsa_vevent_location(event));
    BUFFER_APPEND(textbuf, _("Description:"), libbalsa_vevent_description(event));
    body->buffer = g_string_free(textbuf, FALSE);
    if (balsa_app.wordwrap) {
        libbalsa_wrap_string(body->buffer, balsa_app.wraplength);
    }
    libbalsa_message_append_part(message, body);

    /* the next message part is the calendar object */
    body = libbalsa_message_body_new(message);
    body->buffer = libbalsa_vevent_reply(event, ia, pstat);
    if (body->buffer == NULL) {
    	g_object_unref(message);
    	return;
    }
    body->charset = g_strdup("utf-8");
    body->content_type = g_strdup("text/calendar; method=reply");
    libbalsa_message_append_part(message, body);

    result = libbalsa_message_send(message, balsa_app.outbox, balsa_app.sentbox,
				   balsa_find_sentbox_by_url,
				   libbalsa_identity_get_smtp_server(ident),
				   balsa_app.send_progress_dialog,
                                   GTK_WINDOW(gtk_widget_get_toplevel
                                              ((GtkWidget *) button)),
				   FALSE, &error);
    if (result != LIBBALSA_MESSAGE_CREATE_OK)
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Sending the iTIP calendar reply failed: %s"),
			     error ? error->message : "?");
    if (error)
	g_error_free(error);
    g_object_unref(message);
}
