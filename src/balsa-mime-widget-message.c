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
#include "balsa-mime-widget-message.h"

#include <string.h>
#include <gtk/gtk.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "send.h"
#include <glib/gi18n.h>
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include "sendmsg-window.h"
#include "libbalsa-gpgme.h"
#include "dkim.h"

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
				       GtkWidget * const * buttons);
static void add_header_sigstate(GtkGrid * grid,
				GMimeGpgmeSigstat * siginfo);
static void add_header_dmarc_dkim(GtkGrid      *grid,
								  LibBalsaDkim *dkim);

static void bmw_message_set_headers(BalsaMessage        * bm,
                                    BalsaMimeWidget     * mw,
                                    LibBalsaMessageBody * part,
                                    gboolean              show_all_headers);

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
	GtkWidget *widget;
	GtkWidget *container;

	mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

	widget = gtk_frame_new(NULL);
        gtk_container_add(GTK_CONTAINER(mw), widget);

	container = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_MESSAGE_PADDING);
        balsa_mime_widget_set_container(mw, container);

	gtk_container_set_border_width(GTK_CONTAINER(container),
				       BMW_MESSAGE_PADDING);
	gtk_container_add(GTK_CONTAINER(widget), container);

        emb_hdrs = bm_header_widget_new(bm, NULL);
        balsa_mime_widget_set_header_widget(mw, emb_hdrs);

	gtk_container_add(GTK_CONTAINER(container), emb_hdrs);

        bmw_message_set_headers(bm, mw, mime_body,
                                balsa_message_get_shown_headers(bm) == HEADERS_ALL);
    } else if (!g_ascii_strcasecmp("text/rfc822-headers", content_type)) {
	GtkWidget *widget;
	GtkWidget *header_widget;

	mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

	widget = gtk_frame_new(_("message headers"));
        gtk_container_add(GTK_CONTAINER(mw), widget);

	header_widget = bm_header_widget_new(bm, NULL);
        balsa_mime_widget_set_header_widget(mw, header_widget);

        gtk_widget_set_valign(header_widget, GTK_ALIGN_START);
        gtk_widget_set_vexpand(header_widget, FALSE);
        libbalsa_set_margins(header_widget, 5);
	gtk_container_add(GTK_CONTAINER(widget), header_widget);
	bmw_message_set_headers(bm, mw, mime_body, TRUE);
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

    gtk_container_set_border_width(GTK_CONTAINER(mw),
				   BMW_CONTAINER_BORDER);

    gtk_container_add(GTK_CONTAINER(mw), gtk_label_new(msg->str));
    g_string_free(msg, TRUE);

    button = gtk_button_new_with_label(url);
    libbalsa_set_vmargins(button, BMW_BUTTON_PACK_SPACE);
    gtk_container_add(GTK_CONTAINER(mw), button);

    g_object_set_data_full(G_OBJECT(button), "call_url", url,
			   (GDestroyNotify) g_free);
    g_signal_connect(button, "clicked",
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

    gtk_container_set_border_width(GTK_CONTAINER(mw),
				   BMW_CONTAINER_BORDER);

    gtk_container_add(GTK_CONTAINER(mw), gtk_label_new(msg->str));
    g_string_free(msg, TRUE);

    button =
	gtk_button_new_with_mnemonic(_
				     ("Se_nd message to obtain this part"));
    libbalsa_set_vmargins(button, BMW_BUTTON_PACK_SPACE);
    gtk_container_add(GTK_CONTAINER(mw), button);
    g_signal_connect(button, "clicked",
		     G_CALLBACK(extbody_send_mail), (gpointer) mime_body);


    return mw;
}


static void
extbody_call_url(GtkWidget * button, gpointer data)
{
    gchar *url = g_object_get_data(G_OBJECT(button), "call_url");
    GtkWidget *toplevel;
    GError *err = NULL;

    g_return_if_fail(url);
    toplevel = gtk_widget_get_toplevel(GTK_WIDGET(button));
    if (gtk_widget_is_toplevel(toplevel)) {
        gtk_show_uri_on_window(GTK_WINDOW(toplevel), url,
                               gtk_get_current_event_time(), &err);
    }
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
    LibBalsaMessageHeaders *headers;
    LibBalsaMessageBody *body;
    gchar *data;
    GError *err = NULL;
    LibBalsaMsgCreateResult result;

    /* create a message */
    message = libbalsa_message_new();
    headers = libbalsa_message_get_headers(message);
    headers->from = internet_address_list_new();
    internet_address_list_add(headers->from,
                              libbalsa_identity_get_address(balsa_app.current_ident));

    data = libbalsa_message_body_get_parameter(mime_body, "subject");
    if (data) {
	libbalsa_message_set_subject(message, data);
        g_free(data);
    }

    data = libbalsa_message_body_get_parameter(mime_body, "server");
    headers->to_list = internet_address_list_parse(libbalsa_parser_options(), data);
    g_free(data);

    /* the original body my have some data to be returned as commands... */
    body = libbalsa_message_body_new(message);

    if(libbalsa_message_body_get_content(mime_body, &data, &err)<0) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Could not get a part: %s"),
                          err ? err->message : _("Unknown error"));
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
    result = libbalsa_message_send(message, balsa_app.outbox, NULL,
				   balsa_find_sentbox_by_url,
				   libbalsa_identity_get_smtp_server
                                   (balsa_app.current_ident),
				   balsa_app.send_progress_dialog,
                                   GTK_WINDOW(gtk_widget_get_toplevel
                                              (button)),
				   FALSE, &err);
    if (result != LIBBALSA_MESSAGE_CREATE_OK)
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Sending the external body request failed: %s"),
			     err ? err->message : "?");
    g_error_free(err);
    g_object_unref(message);
}


/* ----- message/rfc822 related stuff ----- */

BalsaMimeWidget *
balsa_mime_widget_new_message_tl(BalsaMessage * bm,
                                 GtkWidget * const * tl_buttons)
{
    BalsaMimeWidget *mw;
    GtkWidget *headers;
    GtkWidget *container;

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

    gtk_container_set_border_width(GTK_CONTAINER(mw), BMW_MESSAGE_PADDING);

    headers = bm_header_widget_new(bm, tl_buttons);
    balsa_mime_widget_set_header_widget(mw, headers);

    gtk_container_add(GTK_CONTAINER(mw), headers);

    container = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_MESSAGE_PADDING);
    balsa_mime_widget_set_container(mw, container);

    gtk_widget_set_vexpand(container, TRUE);
    gtk_widget_set_valign(container, GTK_ALIGN_FILL);
    libbalsa_set_vmargins(container, BMW_CONTAINER_BORDER - BMW_MESSAGE_PADDING);
    gtk_container_add(GTK_CONTAINER(mw), container);

    return mw;
}


/* Callback for the "realized" signal; set header frame and text base
 * color when first realized. */
#define BALSA_MESSAGE_GRID "balsa-message-grid"
#define bm_header_widget_get_grid(header_widget) \
    g_object_get_data(G_OBJECT(header_widget), BALSA_MESSAGE_GRID)

static void
bm_header_ctx_menu_reply(GtkWidget * menu_item,
                         LibBalsaMessageBody *part)
{
    sendmsg_window_reply_embedded(part, SEND_REPLY);
}

static void
bm_header_extend_popup(GtkWidget * widget, GtkMenu * menu, gpointer arg)
{
    GtkWidget *menu_item, *submenu;
    GtkWidget *separator = gtk_separator_menu_item_new();

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_widget_show(separator);
    menu_item = gtk_menu_item_new_with_label(_("Reply…"));
    g_signal_connect(menu_item, "activate",
                     G_CALLBACK(bm_header_ctx_menu_reply),
                     arg);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);


    menu_item = gtk_menu_item_new_with_mnemonic(_("_Copy to folder…"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);

    submenu =
        balsa_mblist_mru_menu(GTK_WINDOW
                              (gtk_widget_get_toplevel(widget)),
                              &balsa_app.folder_mru,
                              balsa_message_copy_part, arg);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item),
                              submenu);
    gtk_widget_show_all(submenu);
}

static GtkWidget *
bm_header_widget_new(BalsaMessage * bm, GtkWidget * const * buttons)
{
    GtkWidget *grid;
    GtkWidget *info_bar_widget;
    GtkInfoBar *info_bar;
    GtkWidget *content_area;
    GtkWidget *vbox;
    GtkWidget *action_area;
    GtkWidget *widget;
    GtkEventController *key_controller;

    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2 * HIG_PADDING);
    gtk_widget_show(grid);

    key_controller = gtk_event_controller_key_new(grid);
    g_signal_connect(key_controller, "focus-in",
		     G_CALLBACK(balsa_mime_widget_limit_focus), bm);
    g_signal_connect(key_controller, "focus-out",
		     G_CALLBACK(balsa_mime_widget_unlimit_focus), bm);
    g_signal_connect(key_controller, "key-pressed",
		     G_CALLBACK(balsa_mime_widget_key_pressed), bm);

    info_bar_widget = gtk_info_bar_new();
    info_bar = GTK_INFO_BAR(info_bar_widget);

    content_area = gtk_info_bar_get_content_area(info_bar);
    gtk_container_add(GTK_CONTAINER(content_area), grid);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, HIG_PADDING);
    action_area = gtk_info_bar_get_action_area(info_bar);
    gtk_widget_set_valign(action_area, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(action_area), vbox);

    if (balsa_message_get_face_box(bm) == NULL) {
        GtkWidget *face_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

        balsa_message_set_face_box(bm, face_box);
        gtk_container_add(GTK_CONTAINER(vbox), face_box);
    }

    if (buttons) {
        while (*buttons) {
            gtk_container_add(GTK_CONTAINER(vbox), *buttons++);
        }
    }

    widget = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(widget), info_bar_widget);

    g_object_set_data(G_OBJECT(widget), BALSA_MESSAGE_GRID, grid);

    return widget;
}

static gboolean
label_size_allocate_cb(GtkLabel * label, GdkRectangle * rectangle,
                       GtkWidget * expander)
{
    PangoLayout *layout;

    layout = gtk_label_get_layout(label);

    if (pango_layout_is_wrapped(layout)
        || pango_layout_is_ellipsized(layout))
        gtk_widget_show(expander);
    else
        gtk_widget_hide(expander);

    return FALSE;
}

static void
expanded_cb(GtkExpander * expander, GParamSpec * arg1, GtkLabel * label)
{
    gtk_label_set_ellipsize(label,
                            gtk_expander_get_expanded(expander) ?
                            PANGO_ELLIPSIZE_NONE : PANGO_ELLIPSIZE_END);
}

#define BALSA_MESSAGE_HEADER "balsa-message-header"

static void
add_header_gchar(GtkGrid * grid, const gchar * header, const gchar * label,
		 const gchar * value, gboolean show_all_headers)
{
    gchar *css;
    GtkCssProvider *css_provider;
    GtkWidget *lab;

    if (!(show_all_headers ||
	  libbalsa_find_word(header, balsa_app.selected_headers)))
	return;

    if (balsa_app.use_system_fonts) {
        if (strcmp(header, "subject") == 0)
            /* Use bold for the subject line */
            css = g_strdup("#" BALSA_MESSAGE_HEADER " {font-weight:bold}");
        else
            css = g_strdup("");
    } else {
        css = libbalsa_font_string_to_css(strcmp(header, "subject")
                                          ? balsa_app.message_font
                                          : balsa_app.subject_font,
                                          BALSA_MESSAGE_HEADER);
    }

    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, css, -1, NULL);
    g_free(css);

    lab = gtk_label_new(label);
    gtk_widget_set_name(lab, BALSA_MESSAGE_HEADER);
    gtk_style_context_add_provider(gtk_widget_get_style_context(lab) ,
                                   GTK_STYLE_PROVIDER(css_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    gtk_grid_attach_next_to(grid, lab, NULL, GTK_POS_BOTTOM, 1, 1);
    gtk_label_set_selectable(GTK_LABEL(lab), TRUE);
    gtk_widget_set_halign(lab, GTK_ALIGN_START);
    gtk_widget_set_valign(lab, GTK_ALIGN_START);
    gtk_widget_show(lab);

    if (value && *value != '\0') {
        gchar *sanitized;
        GtkWidget *value_label;
        GtkWidget *expander;
        GtkWidget *hbox;

        sanitized = g_strdup(value);
        libbalsa_utf8_sanitize(&sanitized,
                               balsa_app.convert_unknown_8bit, NULL);
        g_strdelimit(sanitized, "\r\n", ' ');
        value_label = libbalsa_create_wrap_label(sanitized, FALSE);
        g_free(sanitized);

        gtk_widget_set_name(value_label, BALSA_MESSAGE_HEADER);
        gtk_style_context_add_provider(gtk_widget_get_style_context(value_label) ,
                                       GTK_STYLE_PROVIDER(css_provider),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        gtk_label_set_line_wrap_mode(GTK_LABEL(value_label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_selectable(GTK_LABEL(value_label), TRUE);
        gtk_widget_set_hexpand(value_label, TRUE);

        expander = gtk_expander_new(NULL);

        /*
         * If we are showing all headers, we initially expand the
         * header, otherwise collapse it.
         */
        if (show_all_headers) {
            gtk_label_set_ellipsize(GTK_LABEL(value_label), PANGO_ELLIPSIZE_NONE);
            gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);
        } else {
            gtk_label_set_ellipsize(GTK_LABEL(value_label), PANGO_ELLIPSIZE_END);
            gtk_expander_set_expanded(GTK_EXPANDER(expander), FALSE);
        }
        g_signal_connect(expander, "notify::expanded",
                         G_CALLBACK(expanded_cb), value_label);
        g_signal_connect(value_label, "size-allocate",
                         G_CALLBACK(label_size_allocate_cb), expander);

        hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_container_add(GTK_CONTAINER(hbox), value_label);
        gtk_container_add(GTK_CONTAINER(hbox), expander);
        gtk_widget_show_all(hbox);
        gtk_grid_attach_next_to(grid, hbox, lab, GTK_POS_RIGHT, 1, 1);
    }

    g_object_unref(css_provider);
}

static void
add_header_address_list(BalsaMessage * bm, GtkGrid * grid,
			gchar * header, gchar * label,
			InternetAddressList * list,
                        gboolean show_all_headers)
{
    gchar *value;

    if (list == NULL || internet_address_list_length(list) == 0)
	return;

    if (!(balsa_message_get_shown_headers(bm) == HEADERS_ALL ||
	  libbalsa_find_word(header, balsa_app.selected_headers)))
	return;

    value = internet_address_list_to_string(list, NULL, FALSE);

    add_header_gchar(grid, header, label, value, show_all_headers);

    g_free(value);
}

static void
foreach_label(GtkWidget * widget, LibBalsaMessageBody * part)
{
    g_assert(widget != NULL);

    if (GTK_IS_CONTAINER(widget))
        gtk_container_foreach((GtkContainer *) widget,
                              (GtkCallback) foreach_label, part);
    else if (GTK_IS_LABEL(widget))
        g_signal_connect(widget, "populate-popup",
                         G_CALLBACK(bm_header_extend_popup), part);
}

static void
bmw_message_set_headers_d(BalsaMessage           * bm,
                          BalsaMimeWidget        * mw,
                          LibBalsaMessageHeaders * headers,
                          LibBalsaMessageBody    * part,
                          LibBalsaDkim           * dkim,
                          const gchar            * subject,
                          gboolean                 show_all_headers)
{
    GtkGrid *grid;
    GList *p;
    gchar *date;
    GtkWidget * widget;

    if ((widget = balsa_mime_widget_get_header_widget(mw)) == NULL)
	return;

    grid = bm_header_widget_get_grid(widget);
    gtk_container_foreach(GTK_CONTAINER(grid),
                          (GtkCallback) gtk_widget_destroy, NULL);

    if (!headers) {
        /* Gmail sometimes fails to do that. */
        add_header_gchar(grid, "subject", _("Error:"),
                         _("IMAP server did not report message structure"),
                         show_all_headers);
        return;
    }

    if (balsa_message_get_shown_headers(bm) == HEADERS_NONE) {
        g_signal_connect(widget, "realize",
                         G_CALLBACK(gtk_widget_hide), NULL);
	return;
    }

    add_header_gchar(grid, "subject", _("Subject:"), subject,
                     show_all_headers);

    date = libbalsa_message_headers_date_to_utf8(headers,
						 balsa_app.date_string);
    add_header_gchar(grid, "date", _("Date:"), date, show_all_headers);
    g_free(date);

    if (headers->from) {
	gchar *from =
	    internet_address_list_to_string(headers->from, NULL, FALSE);
	add_header_gchar(grid, "from", _("From:"), from, show_all_headers);
	g_free(from);
    }

    if (headers->reply_to) {
	gchar *reply_to =
	    internet_address_list_to_string(headers->reply_to, NULL, FALSE);
	add_header_gchar(grid, "reply-to", _("Reply-To:"), reply_to,
                         show_all_headers);
	g_free(reply_to);
    }
    add_header_address_list(bm, grid, "to", _("To:"), headers->to_list,
                            show_all_headers);
    add_header_address_list(bm, grid, "cc", _("CC:"), headers->cc_list,
                            show_all_headers);
    add_header_address_list(bm, grid, "bcc", _("BCC:"), headers->bcc_list,
                            show_all_headers);
    add_header_address_list(bm, grid, "sender", _("Sender:"), headers->sender,
                            show_all_headers);

#if BALSA_SHOW_FCC_AS_WELL_AS_X_BALSA_FCC
    if (headers->fcc_url)
	add_header_gchar(grid, "fcc", _("FCC:"), headers->fcc_url,
                         show_all_headers);
#endif

    if (headers->dispnotify_to) {
	gchar *mdn_to =
	    internet_address_list_to_string(headers->dispnotify_to, NULL, FALSE);
	add_header_gchar(grid, "disposition-notification-to",
			 _("Disposition-Notification-To:"), mdn_to,
                         show_all_headers);
	g_free(mdn_to);
    }

    /* remaining headers */
    for (p = g_list_first(headers->user_hdrs); p; p = g_list_next(p)) {
	gchar **pair = p->data;
	gchar *hdr;

	hdr = g_strconcat(pair[0], ":", NULL);
	add_header_gchar(grid, pair[0], hdr, pair[1], show_all_headers);
	g_free(hdr);
    }

    if (part) {
	if (libbalsa_message_body_multipart_signed(part)) {
	    /* top-level part is RFC 3156 or RFC 8551 multipart/signed */
	    add_header_sigstate(grid, part->parts->next->sig_info);
	} else if (libbalsa_message_body_inline_signed(part)) {
	    /* top-level is OpenPGP (RFC 2440) signed */
	    add_header_sigstate(grid, part->sig_info);
	}
    }
    if ((balsa_app.enable_dkim_checks != 0) && (libbalsa_dkim_status_code(dkim) >= DKIM_SUCCESS)) {
	    add_header_dmarc_dkim(grid, dkim);
    }
}

static void
bmw_message_set_headers(BalsaMessage        * bm,
                        BalsaMimeWidget     * mw,
                        LibBalsaMessageBody * part,
                        gboolean              show_all_headers)
{
    GtkWidget *widget;
    GtkGrid *grid;

    bmw_message_set_headers_d(bm, mw, part->embhdrs, part->parts, part->dkim,
                              part->embhdrs ? part->embhdrs->subject : NULL,
                              show_all_headers);

    if ((widget = balsa_mime_widget_get_header_widget(mw)) == NULL)
	return;

    grid = bm_header_widget_get_grid(widget);
    gtk_container_foreach(GTK_CONTAINER(grid), (GtkCallback) foreach_label,
                          part);
}

void
balsa_mime_widget_message_set_headers_d(BalsaMessage           * bm,
                                        BalsaMimeWidget        * mw,
                                        LibBalsaMessageHeaders * headers,
                                        LibBalsaMessageBody    * part,
                                        const gchar            * subject)
{
    bmw_message_set_headers_d(bm, mw, headers, part, part->dkim, subject,
                              balsa_message_get_shown_headers(bm) == HEADERS_ALL);
}

/*
 * Add the short status of a signature info siginfo to the message headers in
 * view
 */
static void
add_header_sigstate(GtkGrid * grid, GMimeGpgmeSigstat * siginfo)
{
    gchar *format;
    gchar *msg;
    gchar *info_str;
    GtkWidget *label;

    info_str = g_mime_gpgme_sigstat_info(siginfo, TRUE);
    format = (g_mime_gpgme_sigstat_status(siginfo) == GPG_ERR_NO_ERROR) ? "<i>%s</i>" : "<b><i>%s</i></b>";
    msg = g_markup_printf_escaped(format, info_str);
    g_free(info_str);

    label = libbalsa_create_wrap_label(msg, TRUE);
    g_free(msg);
    gtk_widget_show(label);

    gtk_grid_attach_next_to(grid, label, NULL, GTK_POS_BOTTOM, 2, 1);
}


static void
show_dkim_details(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	if (event->type == GDK_BUTTON_PRESS) {
		GdkEventButton *btn_ev = (GdkEventButton *) event;
		GdkRectangle rect;

		rect.x = (int) btn_ev->x;
		rect.y = (int) btn_ev->y;
		rect.width = 1;
		rect.height = 1;
		gtk_popover_set_pointing_to(GTK_POPOVER(user_data), &rect);
	}
	gtk_popover_popup(GTK_POPOVER(user_data));
}


static void
add_header_dmarc_dkim(GtkGrid *grid, LibBalsaDkim *dkim)
{
	gint dkim_status;
	gchar *msg;
	GtkWidget *evbox;
	GtkWidget *box;
	GtkWidget *details;
	GtkWidget *label;

	dkim_status = libbalsa_dkim_status_code(dkim);
	evbox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(evbox), TRUE);
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_add(GTK_CONTAINER(evbox), box);
	if (dkim_status == DKIM_WARNING) {
		gtk_container_add(GTK_CONTAINER(box), gtk_image_new_from_icon_name("dialog-warning", GTK_ICON_SIZE_MENU));
	} else if (dkim_status == DKIM_FAILED) {
		gtk_container_add(GTK_CONTAINER(box), gtk_image_new_from_icon_name("dialog-error", GTK_ICON_SIZE_MENU));
	} else {
		/* valid: no icon */
	}
	msg = g_markup_printf_escaped("<i>%s</i>", libbalsa_dkim_status_str_short(dkim));
	label = libbalsa_create_wrap_label(msg, TRUE);
	g_free(msg);
	gtk_container_add(GTK_CONTAINER(box), label);
	details = gtk_popover_new(evbox);
	gtk_popover_set_modal(GTK_POPOVER(details), TRUE);
	label = libbalsa_create_wrap_label(libbalsa_dkim_status_str_long(dkim), FALSE);
	gtk_widget_show(label);
	gtk_container_add(GTK_CONTAINER(details), label);
	gtk_container_set_border_width(GTK_CONTAINER(details), HIG_PADDING);
	g_signal_connect(evbox, "button-press-event", G_CALLBACK(show_dkim_details), details);
	gtk_widget_show_all(evbox);
	gtk_grid_attach_next_to(grid, evbox, NULL, GTK_POS_BOTTOM, 2, 1);
}
