/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-mime-widget-message.h"

#include <string.h>
#include <gtk/gtk.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "send.h"
#include "rfc3156.h"
#include <glib/gi18n.h>
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include "sendmsg-window.h"

typedef enum _rfc_extbody_t {
    RFC2046_EXTBODY_FTP,
    RFC2046_EXTBODY_ANONFTP,
    RFC2046_EXTBODY_TFTP,
    RFC2046_EXTBODY_LOCALFILE,
    RFC2046_EXTBODY_MAILSERVER,
    RFC2017_EXTBODY_URL,
    RFC2046_EXTBODY_UNKNOWN
} rfc_extbody_t;


typedef struct _rfc_extbody_id {
    gchar *id_string;
    rfc_extbody_t action;
} rfc_extbody_id;


static rfc_extbody_id rfc_extbodys[] = {
    {"ftp", RFC2046_EXTBODY_FTP},
    {"anon-ftp", RFC2046_EXTBODY_ANONFTP},
    {"tftp", RFC2046_EXTBODY_TFTP},
    {"local-file", RFC2046_EXTBODY_LOCALFILE},
    {"mail-server", RFC2046_EXTBODY_MAILSERVER},
    {"URL", RFC2017_EXTBODY_URL},
    {NULL, RFC2046_EXTBODY_UNKNOWN}
};


/* message/external-body related stuff */
static BalsaMimeWidget *bmw_message_extbody_url(LibBalsaMessageBody *
						mime_body,
						rfc_extbody_t url_type);
static BalsaMimeWidget *bmw_message_extbody_mail(LibBalsaMessageBody *
						 mime_body);
static void extbody_call_url(GtkWidget * button, gpointer data);
static void extbody_send_mail(GtkWidget * button,
			      LibBalsaMessageBody * mime_body);

/* message/rfc822 related stuff */
static GtkWidget *bm_header_widget_new(BalsaMessage * bm,
				       GtkWidget * buttons);
#ifdef HAVE_GPGME
static void add_header_sigstate(GtkTextView * view,
				GMimeGpgmeSigstat * siginfo);
#endif

BalsaMimeWidget *
balsa_mime_widget_new_message(BalsaMessage * bm,
			      LibBalsaMessageBody * mime_body,
			      const gchar * content_type, gpointer data)
{
    BalsaMimeWidget *mw = NULL;

    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);

    if (!g_ascii_strcasecmp("message/external-body", content_type)) {
	gchar *access_type;
	rfc_extbody_id *extbody_type = rfc_extbodys;

	access_type =
	    libbalsa_message_body_get_parameter(mime_body, "access-type");
	while (extbody_type->id_string &&
	       g_ascii_strcasecmp(extbody_type->id_string, access_type))
	    extbody_type++;
	switch (extbody_type->action) {
	case RFC2046_EXTBODY_FTP:
	case RFC2046_EXTBODY_ANONFTP:
	case RFC2046_EXTBODY_TFTP:
	case RFC2046_EXTBODY_LOCALFILE:
	case RFC2017_EXTBODY_URL:
	    mw = bmw_message_extbody_url(mime_body, extbody_type->action);
	    break;
	case RFC2046_EXTBODY_MAILSERVER:
	    mw = bmw_message_extbody_mail(mime_body);
	    break;
	case RFC2046_EXTBODY_UNKNOWN:
	    break;
	default:
	    g_error("Undefined external body action %d!", extbody_type->action);
	    break;
	}
	g_free(access_type);
    } else if (!g_ascii_strcasecmp("message/rfc822", content_type)) {
	GtkWidget *emb_hdrs;

	mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

	mw->widget = gtk_frame_new(NULL);

	mw->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_MESSAGE_PADDING);
	gtk_container_set_border_width(GTK_CONTAINER(mw->container),
				       BMW_MESSAGE_PADDING);
	gtk_container_add(GTK_CONTAINER(mw->widget), mw->container);

        mw->header_widget = emb_hdrs = bm_header_widget_new(bm, NULL);
	gtk_box_pack_start(GTK_BOX(mw->container), emb_hdrs, FALSE, FALSE, 0);

	balsa_mime_widget_message_set_headers(bm, mw, mime_body);
    }

    /* return the created widget (may be NULL) */
    return mw;
}


/* ----- message/external-body related stuff ----- */
static BalsaMimeWidget *
bmw_message_extbody_url(LibBalsaMessageBody * mime_body,
			rfc_extbody_t url_type)
{
    GtkWidget *button;
    GString *msg = NULL;
    gchar *url;
    BalsaMimeWidget *mw;

    if (url_type == RFC2046_EXTBODY_LOCALFILE) {
	url = libbalsa_message_body_get_parameter(mime_body, "name");

	if (!url)
	    return NULL;

	msg = g_string_new(_("Content Type: external-body\n"));
	g_string_append_printf(msg, _("Access type: local-file\n"));
	g_string_append_printf(msg, _("File name: %s"), url);
    } else if (url_type == RFC2017_EXTBODY_URL) {
	gchar *local_name;

	local_name = libbalsa_message_body_get_parameter(mime_body, "URL");

	if (!local_name)
	    return NULL;

	url = g_strdup(local_name);
	msg = g_string_new(_("Content Type: external-body\n"));
	g_string_append_printf(msg, _("Access type: URL\n"));
	g_string_append_printf(msg, _("URL: %s"), url);
	g_free(local_name);
    } else {			/* *FTP* */
	gchar *ftp_dir, *ftp_name, *ftp_site;

	ftp_dir =
	    libbalsa_message_body_get_parameter(mime_body, "directory");
	ftp_name = libbalsa_message_body_get_parameter(mime_body, "name");
	ftp_site = libbalsa_message_body_get_parameter(mime_body, "site");

	if (!ftp_name || !ftp_site) {
	    g_free(ftp_dir);
	    g_free(ftp_name);
	    g_free(ftp_site);
	    return NULL;
	}

	if (ftp_dir)
	    url = g_strdup_printf("%s://%s/%s/%s",
				  url_type == RFC2046_EXTBODY_TFTP
				  ? "tftp" : "ftp",
				  ftp_site, ftp_dir, ftp_name);
	else
	    url = g_strdup_printf("%s://%s/%s",
				  url_type == RFC2046_EXTBODY_TFTP
				  ? "tftp" : "ftp", ftp_site, ftp_name);
	msg = g_string_new(_("Content Type: external-body\n"));
	g_string_append_printf(msg, _("Access type: %s\n"),
			       url_type == RFC2046_EXTBODY_TFTP ? "tftp" :
			       url_type ==
			       RFC2046_EXTBODY_FTP ? "ftp" : "anon-ftp");
	g_string_append_printf(msg, _("FTP site: %s\n"), ftp_site);
	if (ftp_dir)
	    g_string_append_printf(msg, _("Directory: %s\n"), ftp_dir);
	g_string_append_printf(msg, _("File name: %s"), ftp_name);
	g_free(ftp_dir);
	g_free(ftp_name);
	g_free(ftp_site);
    }

    /* now create & return the widget... */
    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    
    mw->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_VBOX_SPACE);
    gtk_container_set_border_width(GTK_CONTAINER(mw->widget),
				   BMW_CONTAINER_BORDER);

    gtk_box_pack_start(GTK_BOX(mw->widget), gtk_label_new(msg->str), FALSE,
		       FALSE, 0);
    g_string_free(msg, TRUE);

    button = gtk_button_new_with_label(url);
    gtk_box_pack_start(GTK_BOX(mw->widget), button, FALSE, FALSE,
		       BMW_BUTTON_PACK_SPACE);
    g_object_set_data_full(G_OBJECT(button), "call_url", url,
			   (GDestroyNotify) g_free);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(extbody_call_url), NULL);

    return mw;
}


static BalsaMimeWidget *
bmw_message_extbody_mail(LibBalsaMessageBody * mime_body)
{
    GtkWidget *button;
    GString *msg = NULL;
    gchar *mail_subject, *mail_site;
    BalsaMimeWidget *mw;

    mail_site = libbalsa_message_body_get_parameter(mime_body, "server");

    if (!mail_site)
	return NULL;

    mail_subject =
	libbalsa_message_body_get_parameter(mime_body, "subject");

    msg = g_string_new(_("Content Type: external-body\n"));
    g_string_append(msg, _("Access type: mail-server\n"));
    g_string_append_printf(msg, _("Mail server: %s\n"), mail_site);
    if (mail_subject)
	g_string_append_printf(msg, _("Subject: %s\n"), mail_subject);
    g_free(mail_subject);
    g_free(mail_site);

    /* now create & return the widget... */
    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    
    mw->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_VBOX_SPACE);
    gtk_container_set_border_width(GTK_CONTAINER(mw->widget),
				   BMW_CONTAINER_BORDER);

    gtk_box_pack_start(GTK_BOX(mw->widget), gtk_label_new(msg->str), FALSE,
		       FALSE, 0);
    g_string_free(msg, TRUE);

    button =
	gtk_button_new_with_mnemonic(_
				     ("Se_nd message to obtain this part"));
    gtk_box_pack_start(GTK_BOX(mw->widget), button, FALSE, FALSE,
		       BMW_BUTTON_PACK_SPACE);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(extbody_send_mail), (gpointer) mime_body);


    return mw;
}


static void
extbody_call_url(GtkWidget * button, gpointer data)
{
    gchar *url = g_object_get_data(G_OBJECT(button), "call_url");
    GdkScreen *screen;
    GError *err = NULL;

    g_return_if_fail(url);
    screen = gtk_widget_get_screen(button);
    gtk_show_uri(screen, url, gtk_get_current_event_time(), &err);
    if (err) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Error showing %s: %s\n"), url, err->message);
	g_error_free(err);
    }
}

static void
extbody_send_mail(GtkWidget * button, LibBalsaMessageBody * mime_body)
{
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;
    gchar *data;
    GError *err = NULL;
    LibBalsaMsgCreateResult result;

    /* create a message */
    message = libbalsa_message_new();
    message->headers->from = internet_address_list_new();
    internet_address_list_add(message->headers->from,
                              balsa_app.current_ident->ia);

    data = libbalsa_message_body_get_parameter(mime_body, "subject");
    if (data) {
	libbalsa_message_set_subject(message, data);
        g_free(data);
    }

    data = libbalsa_message_body_get_parameter(mime_body, "server");
    message->headers->to_list = internet_address_list_parse_string(data);
    g_free(data);

    /* the original body my have some data to be returned as commands... */
    body = libbalsa_message_body_new(message);

    if(libbalsa_message_body_get_content(mime_body, &data, &err)<0) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Could not get a part: %s"),
                          err ? err->message : "Unknown error");
        g_clear_error(&err);
    }

    if (data) {
	gchar *p;

	/* ignore everything before the first two newlines */
	if ((p = strstr(data, "\n\n")))
	    body->buffer = g_strdup(p + 2);
	else
	    body->buffer = g_strdup(data);
	g_free(data);
    }
    if (mime_body->charset)
	body->charset = g_strdup(mime_body->charset);
    else
	body->charset = g_strdup("US-ASCII");
    libbalsa_message_append_part(message, body);
#if ENABLE_ESMTP
    result = libbalsa_message_send(message, balsa_app.outbox, NULL,
				   balsa_find_sentbox_by_url,
				   balsa_app.current_ident->smtp_server,
				   FALSE, balsa_app.debug, &err);
#else
    result = libbalsa_message_send(message, balsa_app.outbox, NULL,
				   balsa_find_sentbox_by_url,
				   FALSE, balsa_app.debug, &err);
#endif
    if (result != LIBBALSA_MESSAGE_CREATE_OK)
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Sending the external body request failed: %s"),
			     err ? err->message : "?");
    g_error_free(err);
    g_object_unref(G_OBJECT(message));
}


/* ----- message/rfc822 related stuff ----- */

BalsaMimeWidget *
balsa_mime_widget_new_message_tl(BalsaMessage * bm, GtkWidget * tl_buttons)
{
    GtkWidget *headers;
    BalsaMimeWidget *mw;

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

    mw->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_MESSAGE_PADDING);
    gtk_container_set_border_width(GTK_CONTAINER(mw->widget), BMW_MESSAGE_PADDING);

    mw->header_widget = headers = bm_header_widget_new(bm, tl_buttons);
    gtk_box_pack_start(GTK_BOX(mw->widget), headers, FALSE, FALSE, 0);

    mw->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_MESSAGE_PADDING);
    gtk_box_pack_start(GTK_BOX(mw->widget), mw->container, TRUE, TRUE,
		       BMW_CONTAINER_BORDER - BMW_MESSAGE_PADDING);

    return mw;
}


/* Callback for the "realized" signal; set header frame and text base
 * color when first realized. */
#define BALSA_MESSAGE_TEXT_VIEW "balsa-message-text-view"
#define bm_header_widget_get_text_view(header_widget) \
    g_object_get_data(G_OBJECT(header_widget), BALSA_MESSAGE_TEXT_VIEW)

static void
bm_header_widget_realized(GtkWidget * widget, BalsaMessage * bm)
{
    GtkStyleContext *context;
    GdkRGBA rgba;
    GtkWidget *tl_buttons;

    context = gtk_widget_get_style_context(bm->scroll);
    tl_buttons = g_object_get_data(G_OBJECT(widget), "tl-buttons");

    gtk_style_context_get_background_color(context, GTK_STATE_FLAG_NORMAL,
                                           &rgba);
    gtk_widget_override_background_color(bm_header_widget_get_text_view
                                         (widget), GTK_STATE_FLAG_NORMAL,
                                         &rgba);

    if (tl_buttons) {
        gtk_widget_override_background_color(tl_buttons,
                                             GTK_STATE_FLAG_NORMAL, &rgba);
    }

    gtk_style_context_get_color(context, GTK_STATE_FLAG_NORMAL, &rgba);
    gtk_widget_override_color(bm_header_widget_get_text_view(widget),
                              GTK_STATE_FLAG_NORMAL, &rgba);

    gtk_style_context_get_background_color(context, GTK_STATE_FLAG_SELECTED,
                                           &rgba);
    gtk_widget_override_background_color(bm_header_widget_get_text_view
                                         (widget), GTK_STATE_FLAG_SELECTED,
                                         &rgba);

    if (tl_buttons) {
        gtk_widget_override_background_color(tl_buttons,
                                             GTK_STATE_FLAG_SELECTED, &rgba);
    }

    gtk_style_context_get_color(context, GTK_STATE_FLAG_SELECTED, &rgba);
    gtk_widget_override_color(bm_header_widget_get_text_view(widget),
                              GTK_STATE_FLAG_SELECTED, &rgba);
}

/* Callback for the "style-set" signal; reset colors when theme is
 * changed. */
static void
bm_header_widget_set_style(GtkWidget * widget, BalsaMessage * bm)
{
    g_signal_handlers_block_by_func(widget, bm_header_widget_set_style,
				    bm);
    bm_header_widget_realized(widget, bm);
    g_signal_handlers_unblock_by_func(widget, bm_header_widget_set_style,
				      bm);
}

static void
bm_header_ctx_menu_reply(GtkWidget * menu_item,
                         LibBalsaMessageBody *part)
{
    sendmsg_window_reply_embedded(part, SEND_REPLY);
}

static void
bm_header_extend_popup(GtkTextView *textview, GtkMenu *menu, gpointer arg)
{
    GtkWidget *menu_item, *submenu;
    GtkWidget *separator = gtk_separator_menu_item_new();

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_widget_show(separator);
    menu_item = gtk_menu_item_new_with_label(_("Reply..."));
    g_signal_connect(G_OBJECT(menu_item), "activate",
                     G_CALLBACK(bm_header_ctx_menu_reply),
                     arg);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);


    menu_item = gtk_menu_item_new_with_mnemonic(_("_Copy to folder..."));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);

    submenu =
        balsa_mblist_mru_menu(GTK_WINDOW
                              (gtk_widget_get_toplevel(GTK_WIDGET(textview))),
                              &balsa_app.folder_mru,
                              G_CALLBACK(balsa_message_copy_part), arg);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item),
                              submenu);
    gtk_widget_show_all(submenu);
}

static GtkWidget *
bm_header_widget_new(BalsaMessage * bm, GtkWidget * buttons)
{
    GtkWidget *widget;
    GtkWidget *text_view;
    GtkTextView *view;
    GtkTextBuffer *buffer;
    GtkWidget *hbox;

    widget = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
    g_object_set_data(G_OBJECT(widget), "tl-buttons", buttons);
    g_signal_connect_after(widget, "realize",
			   G_CALLBACK(bm_header_widget_realized), bm);
    g_signal_connect_after(widget, "style-updated",
			   G_CALLBACK(bm_header_widget_set_style), bm);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(widget), hbox);

    text_view = gtk_text_view_new();
    g_signal_connect(G_OBJECT(text_view), "focus_in_event",
		     G_CALLBACK(balsa_mime_widget_limit_focus), (gpointer) bm);
    g_signal_connect(G_OBJECT(text_view), "focus_out_event",
		     G_CALLBACK(balsa_mime_widget_unlimit_focus),
		     (gpointer) bm);

    view = GTK_TEXT_VIEW(text_view);
    gtk_text_view_set_editable(view, FALSE);
    gtk_text_view_set_left_margin(view, BMW_HEADER_MARGIN_LEFT);
    gtk_text_view_set_right_margin(view, BMW_HEADER_MARGIN_RIGHT);
    gtk_text_view_set_wrap_mode(view, GTK_WRAP_WORD_CHAR);


    buffer = gtk_text_view_get_buffer(view);
    gtk_text_buffer_create_tag(buffer, "subject-font", NULL);
    gtk_text_buffer_create_tag(buffer, "message-font", NULL);
    gtk_text_buffer_create_tag(buffer, "hanging-indent", NULL);
    gtk_text_buffer_create_tag(buffer, "url", NULL);

    g_signal_connect(text_view, "key_press_event",
		     G_CALLBACK(balsa_mime_widget_key_press_event), bm);
    gtk_box_pack_start(GTK_BOX(hbox), text_view, TRUE, TRUE, 0);

    if (buttons)
	gtk_box_pack_start(GTK_BOX(hbox), buttons, FALSE, FALSE, 0);

    g_object_set_data(G_OBJECT(widget), BALSA_MESSAGE_TEXT_VIEW,
		      text_view);

    return widget;
}

static void
bmwm_set_tabs(BalsaMessage * bm, GtkTextView * view,
              GtkTextBuffer * buffer, gint tab_position)
{
    PangoTabArray *tabs;
    GtkTextTagTable *table;
    GtkTextTag *hanging_indent;

    if (tab_position <= bm->tab_position)
        return;

    bm->tab_position = tab_position;
    tabs = pango_tab_array_new_with_positions(1, TRUE, PANGO_TAB_LEFT,
                                              tab_position);
    gtk_text_view_set_tabs(view, tabs);
    pango_tab_array_free(tabs);

    table = gtk_text_buffer_get_tag_table(buffer);
    hanging_indent = gtk_text_tag_table_lookup(table, "hanging-indent");
    g_object_set(hanging_indent, "indent", -tab_position, NULL);
}

/* Indent in pixels: */
#define BALSA_MESSAGE_HEADER_SEP     6

static void
add_header_gchar(BalsaMessage * bm, GtkTextView * view,
		 const gchar * header, const gchar * label,
		 const gchar * value)
{
    static const gchar * all_tag_const = N_("... [truncated]");
    GtkTextBuffer *buffer;
    GtkTextIter insert;
    const gchar *font;
    GdkRectangle location;

    if (!(bm->shown_headers == HEADERS_ALL ||
	  libbalsa_find_word(header, balsa_app.selected_headers)))
	return;

    /* always display the label in the predefined font */
    buffer = gtk_text_view_get_buffer(view);

    gtk_text_buffer_get_iter_at_mark(buffer, &insert,
				     gtk_text_buffer_get_insert(buffer));
    if (gtk_text_buffer_get_char_count(buffer) > 0)
	gtk_text_buffer_insert(buffer, &insert, "\n", 1);
    font =
        strcmp(header, "subject") == 0 ? "subject-font" : "message-font";
    gtk_text_buffer_insert_with_tags_by_name(buffer, &insert, label, -1,
                                             "hanging-indent", font,
                                             NULL);

    gtk_text_view_get_iter_location(view, &insert, &location);
    bmwm_set_tabs(bm, view, buffer, location.x + BALSA_MESSAGE_HEADER_SEP);

    if (value && *value != '\0') {
        gchar *sanitized;
        const gchar *all_tag = _(all_tag_const);
        gboolean truncated = FALSE;

	gtk_text_buffer_insert(buffer, &insert, "\t", 1);

        sanitized = g_strdup(value);
        libbalsa_utf8_sanitize(&sanitized,
                               balsa_app.convert_unknown_8bit, NULL);

        if(bm->shown_headers != HEADERS_ALL) {
            static const gssize MAXLEN = 160;
            ssize_t all_tag_len = g_utf8_strlen(all_tag, -1);
            /* Look far enough into value to be sure that we can tell if
             * the length is more than MAXLEN+all_tag_len: */
            glong header_length = g_utf8_strlen(value, 4*(MAXLEN+all_tag_len+1));
            if(header_length > MAXLEN+all_tag_len) {
                gchar *p = g_utf8_offset_to_pointer(sanitized, MAXLEN);
                *p = '\0';
		truncated = TRUE;
            }
        }

        gtk_text_buffer_insert_with_tags_by_name(buffer, &insert,
                                                 sanitized, -1,
                                                 font, NULL);
        g_free(sanitized);

	if (truncated)
            gtk_text_buffer_insert_with_tags_by_name(buffer, &insert,
                                                     all_tag, -1,
                                                     "url", NULL);
    }
}

static void
add_header_address_list(BalsaMessage * bm, GtkTextView * view,
			gchar * header, gchar * label,
			InternetAddressList * list)
{
    gchar *value;

    if (list == NULL || internet_address_list_length(list) == 0)
	return;

    if (!(bm->shown_headers == HEADERS_ALL ||
	  libbalsa_find_word(header, balsa_app.selected_headers)))
	return;

    value = internet_address_list_to_string(list, FALSE);

    add_header_gchar(bm, view, header, label, value);

    g_free(value);
}

void
balsa_mime_widget_message_set_headers(BalsaMessage * bm, BalsaMimeWidget *mw,
				      LibBalsaMessageBody * part)
{
    GtkWidget *widget;
    GtkTextView *view;

    balsa_mime_widget_message_set_headers_d(bm, mw, part->embhdrs, part->parts,
                                            part->embhdrs
                                            ? part->embhdrs->subject
                                            : NULL );
    if (!(widget = mw->header_widget))
	return;
    view = bm_header_widget_get_text_view(widget);
    if( !g_object_get_data(G_OBJECT(view), "popup-extended") ) {
        g_signal_connect(G_OBJECT(view), "populate-popup",
                         G_CALLBACK(bm_header_extend_popup),
                         part);
        g_object_set_data(G_OBJECT(view), "popup-extended",
                          GINT_TO_POINTER(1));
    }
}

/*
 * Set up the message according to the user preferences.
 * Do it each time we display a message, in case the prefs have been
 * changed.
 */
static void
bmwm_buffer_set_prefs(GtkTextBuffer * buffer)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *tag;
    GdkColor color;

    tag = gtk_text_tag_table_lookup(table, "subject-font");
    g_object_set(tag, "font", balsa_app.subject_font, NULL);

    tag = gtk_text_tag_table_lookup(table, "message-font");
    g_object_set(tag, "font", balsa_app.message_font, NULL);

    color.red   = G_MAXUINT16 * balsa_app.url_color.red;
    color.green = G_MAXUINT16 * balsa_app.url_color.green;
    color.blue  = G_MAXUINT16 * balsa_app.url_color.blue;
    color.pixel = 0;
    tag = gtk_text_tag_table_lookup(table, "url");
    g_object_set(tag, "foreground-gdk", &color, NULL);
}

static gboolean
bmwm_set_headers_d_idle_cb(GtkWidget * view)
{
    gtk_widget_queue_resize(view);
    g_object_unref(view);
    return FALSE;
}

void
balsa_mime_widget_message_set_headers_d(BalsaMessage * bm,
                                        BalsaMimeWidget *mw,
                                        LibBalsaMessageHeaders *headers,
                                        LibBalsaMessageBody *part,
                                        const gchar *subject)
{
    GtkTextView *view;
    GtkTextBuffer *buffer;
    GList *p;
    gchar *date;
    GtkWidget * widget;

    if (!(widget = mw->header_widget))
	return;

    view = bm_header_widget_get_text_view(widget);
    buffer = gtk_text_view_get_buffer(view);
    bmwm_buffer_set_prefs(buffer);

    gtk_text_buffer_set_text(buffer, "", 0);
    if (!headers) {
        /* Gmail sometimes fails to do that. */
        add_header_gchar(bm, view, "subject", _("Error:"),
                         _("IMAP server did not report message structure"));
        return;
    }

    if (bm->shown_headers == HEADERS_NONE) {
        g_signal_connect(G_OBJECT(widget), "realize",
                         G_CALLBACK(gtk_widget_hide), NULL);
	return;
    }

    bm->tab_position = 0;

    add_header_gchar(bm, view, "subject", _("Subject:"), subject);

    date = libbalsa_message_headers_date_to_utf8(headers,
						 balsa_app.date_string);
    add_header_gchar(bm, view, "date", _("Date:"), date);
    g_free(date);

    if (headers->from) {
	gchar *from =
	    internet_address_list_to_string(headers->from, FALSE);
	add_header_gchar(bm, view, "from", _("From:"), from);
	g_free(from);
    }

    if (headers->reply_to) {
	gchar *reply_to =
	    internet_address_list_to_string(headers->reply_to, FALSE);
	add_header_gchar(bm, view, "reply-to", _("Reply-To:"), reply_to);
	g_free(reply_to);
    }
    add_header_address_list(bm, view, "to", _("To:"), headers->to_list);
    add_header_address_list(bm, view, "cc", _("Cc:"), headers->cc_list);
    add_header_address_list(bm, view, "bcc", _("Bcc:"), headers->bcc_list);

#if BALSA_SHOW_FCC_AS_WELL_AS_X_BALSA_FCC
    if (headers->fcc_url)
	add_header_gchar(bm, view, "fcc", _("Fcc:"), headers->fcc_url);
#endif

    if (headers->dispnotify_to) {
	gchar *mdn_to =
	    internet_address_list_to_string(headers->dispnotify_to, FALSE);
	add_header_gchar(bm, view, "disposition-notification-to",
			 _("Disposition-Notification-To:"), mdn_to);
	g_free(mdn_to);
    }

    /* remaining headers */
    for (p = g_list_first(headers->user_hdrs); p; p = g_list_next(p)) {
	gchar **pair = p->data;
	gchar *hdr;

	hdr = g_strconcat(pair[0], ":", NULL);
	add_header_gchar(bm, view, pair[0], hdr, pair[1]);
	g_free(hdr);
    }

#ifdef HAVE_GPGME
    if (part) {
	if (part->parts
	    && part->parts->next
	    && part->parts->next->sig_info
	    && part->parts->next->sig_info->status !=
	    GPG_ERR_NOT_SIGNED)
	    /* top-level part is RFC 3156 or RFC 2633 signed */
	    add_header_sigstate(view, part->parts->next->sig_info);
	else if (part->sig_info
		 && part->sig_info->status != GPG_ERR_NOT_SIGNED)
	    /* top-level is OpenPGP (RFC 2440) signed */
	    add_header_sigstate(view, part->sig_info);
    }
#endif
    g_idle_add((GSourceFunc) bmwm_set_headers_d_idle_cb,
               g_object_ref(view));
}


#ifdef HAVE_GPGME
/*
 * Add the short status of a signature info siginfo to the message headers in
 * view
 */
static void
add_header_sigstate(GtkTextView * view, GMimeGpgmeSigstat * siginfo)
{
    GtkTextBuffer *buffer;
    GtkTextIter insert;
    GtkTextTag *status_tag;
    gchar *msg;

    buffer = gtk_text_view_get_buffer(view);
    gtk_text_buffer_get_iter_at_mark(buffer, &insert,
				     gtk_text_buffer_get_insert(buffer));
    if (gtk_text_buffer_get_char_count(buffer))
	gtk_text_buffer_insert(buffer, &insert, "\n", 1);

    if (siginfo->status == GPG_ERR_NO_ERROR)
	status_tag = gtk_text_buffer_create_tag(buffer, NULL,
						"style",
						PANGO_STYLE_ITALIC, NULL);
    else
	status_tag = gtk_text_buffer_create_tag(buffer, NULL,
						"style",
						PANGO_STYLE_ITALIC,
						"weight",
						PANGO_WEIGHT_BOLD,
                                                NULL);
    msg =
	g_strdup_printf("%s%s",
			libbalsa_gpgme_sig_protocol_name(siginfo->
							 protocol),
			libbalsa_gpgme_sig_stat_to_gchar(siginfo->status));
    gtk_text_buffer_insert_with_tags(buffer, &insert, msg, -1, status_tag,
				     NULL);
    g_free(msg);
}
#endif
