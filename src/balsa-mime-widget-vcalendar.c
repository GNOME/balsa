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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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


static GtkWidget *balsa_vevent_widget(LibBalsaVEvent * event,
				      gboolean may_reply,
				      InternetAddress * sender);
static void vevent_reply(GObject * button, GtkWidget * box);


BalsaMimeWidget *
balsa_mime_widget_new_vcalendar(BalsaMessage * bm,
				LibBalsaMessageBody * mime_body,
				const gchar * content_type, gpointer data)
{
    LibBalsaVCal *vcal_obj;
    LibBalsaVCalMethod method;
    BalsaMimeWidget *mw;
    GtkWidget *widget;
    GtkWidget *label;
    gchar *text;
    GList *l;
    LibBalsaMessage *lbm = bm->message;
    gboolean may_reply = FALSE;
    InternetAddress *sender = NULL;;

    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);
    g_return_val_if_fail(lbm != NULL, NULL);

    vcal_obj = libbalsa_vcal_new_from_body(mime_body);
    if (!vcal_obj)
	return NULL;

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    balsa_mime_widget_set_widget(mw, widget);

    method = libbalsa_vcal_get_method(vcal_obj);
    text = g_strdup_printf(_("This is an iTIP calendar “%s” message."),
                           libbalsa_vcal_method_to_str(method));
    label = gtk_label_new(text);
    g_free(text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(widget), label);

    /* a reply may be created only for unread requests */
    if ((method == ITIP_REQUEST) &&
	LIBBALSA_MESSAGE_IS_UNREAD(lbm)) {
        LibBalsaMessageHeaders *headers;

        headers = libbalsa_message_get_headers(lbm);
	may_reply = TRUE;
	if (headers != NULL) {
	    if (headers->reply_to != NULL)
                sender = internet_address_list_get_address(headers-> reply_to, 0);
	    else if (headers->from != NULL)
                sender = internet_address_list_get_address(headers->from, 0);
	} else if (libbalsa_message_get_sender(lbm))
	    sender = internet_address_list_get_address(libbalsa_message_get_sender(lbm), 0);
    }

    /* add events */
    for (l = libbalsa_vcal_get_vevent(vcal_obj); l != NULL; l = l->next) {
	GtkWidget *event =
	    balsa_vevent_widget((LibBalsaVEvent *) l->data, may_reply,
				sender);
	gtk_box_pack_start(GTK_BOX(widget), event);
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
            lbl = gtk_label_new(str);                                   \
            gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);              \
            gtk_widget_set_halign(lbl, GTK_ALIGN_START);                \
            gtk_widget_set_valign(lbl, GTK_ALIGN_START);                \
            gtk_widget_set_hexpand(lbl, TRUE);                          \
            gtk_widget_set_vexpand(lbl, TRUE);                          \
            gtk_grid_attach(g, lbl, 1, row, 1, 1);                      \
            row++;                                                      \
        }                                                               \
    } while (0)

#define GRID_ATTACH_DATE(g,date,label)                                  \
    do {                                                                \
        if (date != (time_t) -1) {                                      \
            gchar * _dstr =                                             \
                libbalsa_date_to_utf8(date, balsa_app.date_string);     \
            GRID_ATTACH(g, _dstr, label);                               \
            g_free(_dstr);                                              \
        }                                                               \
    } while (0)

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
balsa_vevent_widget(LibBalsaVEvent * event, gboolean may_reply,
		    InternetAddress * sender)
{
    GtkGrid *grid;
    int row = 0;
    LibBalsaIdentity *vevent_ident = NULL;

    grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 12);
    GRID_ATTACH(grid, libbalsa_vevent_get_summary(event), _("Summary:"));
    GRID_ATTACH_ADDRESS(grid, libbalsa_vevent_get_organizer(event), _("Organizer:"));
    GRID_ATTACH_DATE(grid, libbalsa_vevent_get_start(event), _("Start:"));
    GRID_ATTACH_DATE(grid, libbalsa_vevent_get_end(event), _("End:"));
    GRID_ATTACH(grid, libbalsa_vevent_get_location(event), _("Location:"));
    if (libbalsa_vevent_get_attendee(event)) {
	GList *att;
	GString *all_atts = NULL;

	for (att = libbalsa_vevent_get_attendee(event); att; att = att->next) {
	    LibBalsaAddress *lba = LIBBALSA_ADDRESS(att->data);
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
                        break;
                    }
                }
		g_object_unref(ia);
	    }
	}
	GRID_ATTACH(grid, all_atts->str,
                    ngettext("Attendee:", "Attendees:",
                             g_list_length(libbalsa_vevent_get_attendee(event))));
	g_string_free(all_atts, TRUE);
    }
    GRID_ATTACH_TEXT(grid, libbalsa_vevent_get_description(event), _("Description:"));

    if (sender && vevent_ident) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *label;
	GtkWidget *bbox;
	GtkWidget *button;

	/* add the callback data to the event object */
	g_object_ref(event);
	g_object_ref(sender);
	g_object_set_data_full(G_OBJECT(event), "ev:sender",
			       internet_address_to_string(sender, FALSE),
			       (GDestroyNotify) g_free);
        g_object_set_data_full(G_OBJECT(event), "ev:ident",
                               g_object_ref(vevent_ident),
			       (GDestroyNotify) g_object_unref);

	/* pack everything into a box */
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(grid));
	label =
	    gtk_label_new(_("The sender asks you for a reply to this request:"));
	gtk_box_pack_start(GTK_BOX(box), label);
	bbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox),
				  GTK_BUTTONBOX_SPREAD);
	gtk_box_pack_start(GTK_BOX(box), bbox);

	button = gtk_button_new_with_label(_("Accept"));
	g_object_set_data_full(G_OBJECT(button), "event", event,
                               (GDestroyNotify) g_object_unref);
	g_object_set_data(G_OBJECT(button), "mode",
			  GINT_TO_POINTER(VCAL_PSTAT_ACCEPTED));
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(vevent_reply), bbox);
	gtk_box_pack_start(GTK_BOX(bbox), button);

	button = gtk_button_new_with_label(_("Accept tentatively"));
	g_object_set_data(G_OBJECT(button), "event", event);
	g_object_set_data(G_OBJECT(button), "mode",
			  GINT_TO_POINTER(VCAL_PSTAT_TENTATIVE));
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(vevent_reply), bbox);
	gtk_box_pack_start(GTK_BOX(bbox), button);

	button = gtk_button_new_with_label(_("Decline"));
	g_object_set_data(G_OBJECT(button), "event", event);
	g_object_set_data(G_OBJECT(button), "mode",
			  GINT_TO_POINTER(VCAL_PSTAT_DECLINED));
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(vevent_reply), bbox);
	gtk_box_pack_start(GTK_BOX(bbox), button);

	return box;
    } else
	return GTK_WIDGET(grid);
}

static void
vevent_reply(GObject * button, GtkWidget * box)
{
    LibBalsaVEvent *event =
	LIBBALSA_VEVENT(g_object_get_data(button, "event"));
    LibBalsaVCalPartStat pstat =
	GPOINTER_TO_INT(g_object_get_data(button, "mode"));
    gchar *rcpt;
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;
    gchar *dummy;
    gchar **params;
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
    libbalsa_message_get_headers(message)->from = internet_address_list_new();
    internet_address_list_add(libbalsa_message_get_headers(message)->from, ia);
    libbalsa_message_get_headers(message)->to_list = internet_address_list_parse_string(rcpt);
    libbalsa_message_get_headers(message)->date = time(NULL);

    /* create the message subject */
    dummy = g_strdup_printf("%s: %s",
			    libbalsa_vevent_get_summary(event) ? libbalsa_vevent_get_summary(event) : _("iTIP Calendar Request"),
			    libbalsa_vcal_part_stat_to_str(pstat));
    libbalsa_message_set_subject(message, dummy);
    g_free(dummy);

    /* the only message part is the calendar object */
    body = libbalsa_message_body_new(message);
    body->buffer =
	libbalsa_vevent_reply(event,
			      INTERNET_ADDRESS_MAILBOX(ia)->addr,
			      pstat);
    body->charset = g_strdup("utf-8");
    body->content_type = g_strdup("text/calendar");
    libbalsa_message_append_part(message, body);

    /* set the text/calendar parameters */
    params = g_new(gchar *, 3);
    params[0] = g_strdup("method");
    params[1] = g_strdup("reply");
    params[2] = NULL;
    libbalsa_message_set_parameters(message,
            g_list_prepend(libbalsa_message_get_parameters(message), params));

    result = libbalsa_message_send(message, balsa_app.outbox, NULL,
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
    g_object_unref(G_OBJECT(message));
}
