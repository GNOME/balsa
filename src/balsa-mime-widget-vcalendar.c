/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include "config.h"
#include "libbalsa.h"
#include "rfc2445.h"
#include "send.h"
#include "balsa-app.h"
#include <glib/gi18n.h>
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include "balsa-mime-widget-vcalendar.h"


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
    BalsaMimeWidget *mw;
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
    mw->widget = gtk_vbox_new(FALSE, 12);

    text = g_strdup_printf(_("This is an iTIP calendar \"%s\" message."),
			   libbalsa_vcal_method_to_str(vcal_obj->method));
    label = gtk_label_new(text);
    g_free(text);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(mw->widget), label);

    /* a reply may be created only for unread requests */
    if ((vcal_obj->method == ITIP_REQUEST) &&
	LIBBALSA_MESSAGE_IS_UNREAD(lbm)) {
	may_reply = TRUE;
	if (lbm->headers) {
	    if (lbm->headers->reply_to)
		sender = lbm->headers->reply_to->address;
	    else if (lbm->headers && lbm->headers->from)
		sender = lbm->headers->from->address;
	} else if (lbm->sender)
	    sender = lbm->sender->address;
    }

    /* add events */
    for (l = vcal_obj->vevent; l; l = g_list_next(l)) {
	GtkWidget *event =
	    balsa_vevent_widget((LibBalsaVEvent *) l->data, may_reply,
				sender);
	gtk_container_add(GTK_CONTAINER(mw->widget), event);
    }

    gtk_widget_show_all(mw->widget);
    g_object_unref(vcal_obj);

    return mw;
}

#define TABLE_ATTACH(t,str,label)                                       \
    do {                                                                \
        if (str) {                                                      \
            GtkWidget *lbl = gtk_label_new(label);                      \
            gtk_table_attach(t, lbl, 0, 1, row, row+1,                  \
                             GTK_FILL, GTK_FILL, 4, 2);                 \
            gtk_misc_set_alignment(GTK_MISC(lbl), 1.0, 0.0);            \
            gtk_table_attach(table, lbl = gtk_label_new(str),           \
                             1, 2, row, row + 1,                        \
                             GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND,  \
                             4, 2);                                     \
            gtk_misc_set_alignment(GTK_MISC(lbl), 0.0, 0.0);            \
            row++;                                                      \
        }                                                               \
    } while (0)

#define TABLE_ATTACH_DATE(t,date,label)                                 \
    do {                                                                \
        if (date != (time_t) -1) {                                      \
            gchar * _dstr =                                             \
                libbalsa_date_to_utf8(&date, balsa_app.date_string);    \
            TABLE_ATTACH(table, _dstr, label);                          \
            g_free(_dstr);                                              \
        }                                                               \
    } while (0)

#define TABLE_ATTACH_ADDRESS(t,addr,label)                              \
    do {                                                                \
        if (addr) {                                                     \
            gchar * _astr = libbalsa_vcal_attendee_to_str(addr);        \
            TABLE_ATTACH(table, _astr, label);                          \
            g_free(_astr);                                              \
        }                                                               \
    } while (0)

static GtkWidget *
balsa_vevent_widget(LibBalsaVEvent * event, gboolean may_reply,
		    InternetAddress * sender)
{
    GtkTable *table;
    int row = 0;
    LibBalsaIdentity *vevent_ident = NULL;

    table = GTK_TABLE(gtk_table_new(7, 2, FALSE));
    TABLE_ATTACH(table, event->summary, _("Summary"));
    TABLE_ATTACH_ADDRESS(table, event->organizer, _("Organizer"));
    TABLE_ATTACH_DATE(table, event->start, _("Start"));
    TABLE_ATTACH_DATE(table, event->end, _("End"));
    TABLE_ATTACH(table, event->location, _("Location"));
    if (event->attendee) {
	GList *att;
	GString *all_atts = NULL;

	for (att = event->attendee; att; att = g_list_next(att)) {
	    LibBalsaAddress *lba = LIBBALSA_ADDRESS(att->data);
	    gchar *this_att = libbalsa_vcal_attendee_to_str(lba);

	    if (all_atts)
		g_string_append_printf(all_atts, "\n%s", this_att);
	    else
		all_atts = g_string_new(this_att);
	    g_free(this_att);

	    if (may_reply && libbalsa_vcal_attendee_rsvp(lba)) {
		InternetAddress *ia = internet_address_new();
                GList *list;

                internet_address_set_addr(ia, lba->address_list->data);
                for (list = balsa_app.identities; list; list = list->next) {
                    LibBalsaIdentity *ident = list->data;
                    if (libbalsa_ia_rfc2821_equal(ident->ia, ia)) {
                        vevent_ident = ident;
                        break;
                    }
                }
		internet_address_unref(ia);
	    }
	}
	TABLE_ATTACH(table, all_atts->str,
		     event->attendee->next ? _("Attendees") : _("Attendee"));
	g_string_free(all_atts, TRUE);
    }
    TABLE_ATTACH(table, event->description, _("Description"));

    if (sender && vevent_ident) {
	GtkWidget *box = gtk_vbox_new(FALSE, 6);
	GtkWidget *label;
	GtkWidget *bbox;
	GtkWidget *button;

	/* add the callback data to the event object */
	g_object_ref(G_OBJECT(event));
	internet_address_ref(sender);
	g_object_set_data_full(G_OBJECT(event), "ev:sender",
			       internet_address_to_string(sender, FALSE),
			       (GDestroyNotify) g_free);
        g_object_set_data_full(G_OBJECT(event), "ev:ident",
                               g_object_ref(vevent_ident),
			       (GDestroyNotify) g_object_unref);

	/* pack everything into a box */
	gtk_container_add(GTK_CONTAINER(box), GTK_WIDGET(table));
	label =
	    gtk_label_new(_("The sender asks you for a reply to this request:"));
	gtk_container_add(GTK_CONTAINER(box), label);
	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox),
				  GTK_BUTTONBOX_SPREAD);
	gtk_container_add(GTK_CONTAINER(box), bbox);

	button = gtk_button_new_with_label(_("Accept"));
	g_object_set_data_full(G_OBJECT(button), "event", event,
                               (GDestroyNotify) g_object_unref);
	g_object_set_data(G_OBJECT(button), "mode",
			  (gpointer) VCAL_PSTAT_ACCEPTED);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(vevent_reply), bbox);
	gtk_container_add(GTK_CONTAINER(bbox), button);

	button = gtk_button_new_with_label(_("Accept tentatively"));
	g_object_set_data(G_OBJECT(button), "event", event);
	g_object_set_data(G_OBJECT(button), "mode",
			  (gpointer) VCAL_PSTAT_TENTATIVE);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(vevent_reply), bbox);
	gtk_container_add(GTK_CONTAINER(bbox), button);

	button = gtk_button_new_with_label(_("Decline"));
	g_object_set_data(G_OBJECT(button), "event", event);
	g_object_set_data(G_OBJECT(button), "mode",
			  (gpointer) VCAL_PSTAT_DECLINED);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(vevent_reply), bbox);
	gtk_container_add(GTK_CONTAINER(bbox), button);

	return box;
    } else
	return GTK_WIDGET(table);
}

static void
vevent_reply(GObject * button, GtkWidget * box)
{
    LibBalsaVEvent *event =
	LIBBALSA_VEVENT(g_object_get_data(button, "event"));
    LibBalsaVCalPartStat pstat =
	(LibBalsaVCalPartStat) g_object_get_data(button, "mode");
    gchar *rcpt;
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;
    gchar *dummy;
    gchar **params;
    GError *error = NULL;
    LibBalsaMsgCreateResult result;
    LibBalsaIdentity *ident;

    g_return_if_fail(event != NULL);
    rcpt = (gchar *) g_object_get_data(G_OBJECT(event), "ev:sender");
    g_return_if_fail(rcpt != NULL);
    ident = g_object_get_data(G_OBJECT(event), "ev:ident");
    g_return_if_fail(ident != NULL);

    /* make the button box insensitive... */
    gtk_widget_set_sensitive(box, FALSE);

    /* create a message with the header set from the incoming message */
    message = libbalsa_message_new();
    dummy = internet_address_to_string(ident->ia, FALSE);
    message->headers->from = internet_address_parse_string(dummy);
    g_free(dummy);
    message->headers->to_list = internet_address_parse_string(rcpt);
    message->headers->date = time(NULL);

    /* create the message subject */
    dummy = g_strdup_printf("%s: %s",
			    event->summary ? event->summary : _("iTip Calendar Request"),
			    libbalsa_vcal_part_stat_to_str(pstat));
    libbalsa_message_set_subject(message, dummy);
    g_free(dummy);

    /* the only message part is the calendar object */
    body = libbalsa_message_body_new(message);
    body->buffer =
	libbalsa_vevent_reply(event,
			      internet_address_get_addr(ident->ia),
			      pstat);
    body->charset = g_strdup("utf-8");
    body->content_type = g_strdup("text/calendar");
    libbalsa_message_append_part(message, body);

    /* set the text/calendar parameters */
    params = g_new(gchar *, 3);
    params[0] = g_strdup("method");
    params[1] = g_strdup("reply");
    params[2] = NULL;
    message->parameters = g_list_prepend(message->parameters, params);

#if ENABLE_ESMTP
    result = libbalsa_message_send(message, balsa_app.outbox, NULL,
				   balsa_find_sentbox_by_url,
				   ident->smtp_server,
				   FALSE, balsa_app.debug, &error);
#else
    result = libbalsa_message_send(message, balsa_app.outbox, NULL,
				   balsa_find_sentbox_by_url,
				   FALSE, balsa_app.debug, &error);
#endif
    if (result != LIBBALSA_MESSAGE_CREATE_OK)
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Sending the iTip calendar reply failed: %s"),
			     error ? error->message : "?");
    if (error)
	g_error_free(error);
    g_object_unref(G_OBJECT(message));
}
