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
				       GtkWidget * const * buttons);
#ifdef HAVE_GPGME
static void add_header_sigstate(GtkGrid * grid,
				GMimeGpgmeSigstat * siginfo);
#endif

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
	GtkWidget *widget;
	GtkWidget *container;
	GtkWidget *header_widget;

	mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

	widget = gtk_frame_new(NULL);
        balsa_mime_widget_set_widget(mw, widget);

	container = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_MESSAGE_PADDING);
	g_object_set(G_OBJECT(container), "margin", BMW_MESSAGE_PADDING, NULL);
	gtk_container_add(GTK_CONTAINER(widget), container);
        balsa_mime_widget_set_container(mw, container);

        header_widget = bm_header_widget_new(bm, NULL);
	gtk_box_pack_start(GTK_BOX(container), header_widget);
        balsa_mime_widget_set_header_widget(mw, header_widget);

        bmw_message_set_headers(bm, mw, mime_body,
                                bm->shown_headers == HEADERS_ALL);
    } else if (!g_ascii_strcasecmp("text/rfc822-headers", content_type)) {
	GtkWidget *widget;
	GtkWidget *header_widget;

	mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

	widget = gtk_frame_new(_("message headers"));
        balsa_mime_widget_set_widget(mw, widget);

	header_widget = bm_header_widget_new(bm, NULL);
        gtk_widget_set_valign(header_widget, GTK_ALIGN_START);
        gtk_widget_set_vexpand(header_widget, FALSE);
        g_object_set(G_OBJECT(header_widget), "margin", 5, NULL);
	gtk_container_add(GTK_CONTAINER(widget), header_widget);
        balsa_mime_widget_set_header_widget(mw, header_widget);

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
    GtkWidget *widget;

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

    widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_VBOX_SPACE);
    g_object_set(G_OBJECT(widget), "margin", BMW_CONTAINER_BORDER, NULL);

    gtk_box_pack_start(GTK_BOX(widget), gtk_label_new(msg->str));
    g_string_free(msg, TRUE);

    button = gtk_button_new_with_label(url);
    gtk_widget_set_margin_top(button, BMW_BUTTON_PACK_SPACE);
    gtk_box_pack_start(GTK_BOX(widget), button);
    g_object_set_data_full(G_OBJECT(button), "call_url", url,
			   (GDestroyNotify) g_free);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(extbody_call_url), NULL);

    balsa_mime_widget_set_widget(mw, widget);

    return mw;
}


static BalsaMimeWidget *
bmw_message_extbody_mail(LibBalsaMessageBody * mime_body)
{
    GtkWidget *button;
    GString *msg = NULL;
    gchar *mail_subject, *mail_site;
    BalsaMimeWidget *mw;
    GtkWidget *widget;

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
    
    widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_VBOX_SPACE);
    g_object_set(G_OBJECT(widget), "margin", BMW_CONTAINER_BORDER, NULL);

    gtk_box_pack_start(GTK_BOX(widget), gtk_label_new(msg->str));
    g_string_free(msg, TRUE);

    button =
	gtk_button_new_with_mnemonic(_
				     ("Se_nd message to obtain this part"));
    gtk_widget_set_margin_top(button, BMW_BUTTON_PACK_SPACE);
    gtk_box_pack_start(GTK_BOX(widget), button);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(extbody_send_mail), (gpointer) mime_body);

    balsa_mime_widget_set_widget(mw, widget);

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
    result = libbalsa_message_send(message, balsa_app.outbox, NULL,
				   balsa_find_sentbox_by_url,
				   balsa_app.current_ident->smtp_server,
				   balsa_app.send_progress_dialog,
                                   GTK_WINDOW(gtk_widget_get_toplevel
                                              (button)),
				   FALSE, &err);
    if (result != LIBBALSA_MESSAGE_CREATE_OK)
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Sending the external body request failed: %s"),
			     err ? err->message : "?");
    g_error_free(err);
    g_object_unref(G_OBJECT(message));
}


/* ----- message/rfc822 related stuff ----- */

BalsaMimeWidget *
balsa_mime_widget_new_message_tl(BalsaMessage * bm,
                                 GtkWidget * const * tl_buttons)
{
    GtkWidget *headers;
    BalsaMimeWidget *mw;
    GtkWidget *box;
    GtkWidget *container;

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_MESSAGE_PADDING);
    balsa_mime_widget_set_widget(mw, box);
    g_object_set(G_OBJECT(box), "margin", BMW_MESSAGE_PADDING, NULL);

    headers = bm_header_widget_new(bm, tl_buttons);
    balsa_mime_widget_set_header_widget(mw, headers);
    gtk_box_pack_start(GTK_BOX(box), headers);

    container = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_MESSAGE_PADDING);
    balsa_mime_widget_set_container(mw, container);
    gtk_widget_set_vexpand(container, TRUE);
    gtk_widget_set_margin_top(container,
                              BMW_CONTAINER_BORDER - BMW_MESSAGE_PADDING);
    gtk_box_pack_start(GTK_BOX(box), container);

    return mw;
}


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
    g_signal_connect(G_OBJECT(menu_item), "activate",
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
                              G_CALLBACK(balsa_message_copy_part), arg);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item),
                              submenu);
    gtk_widget_show(submenu);
}

static GtkWidget *
bm_header_widget_new(BalsaMessage * bm, GtkWidget * const * buttons)
{
    GtkWidget *grid;
#ifdef GTK_INFO_BAR_WRAPPING_IS_BROKEN
    GtkWidget *hbox;
#else                           /* GTK_INFO_BAR_WRAPPING_IS_BROKEN */
    GtkWidget *info_bar_widget;
    GtkInfoBar *info_bar;
    GtkWidget *content_area;
#endif                          /* GTK_INFO_BAR_WRAPPING_IS_BROKEN */
    GtkWidget *action_area;
    GtkWidget *widget;

    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_show(grid);

    g_signal_connect(grid, "notify::has-focus",
		     G_CALLBACK(balsa_mime_widget_check_focus), bm);
    g_signal_connect(grid, "key_press_event",
		     G_CALLBACK(balsa_mime_widget_key_press_event), bm);

#ifdef GTK_INFO_BAR_WRAPPING_IS_BROKEN
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(hbox), grid);
    g_object_set(G_OBJECT(hbox), "margin", 6, NULL);

    action_area = gtk_button_box_new(GTK_ORIENTATION_VERTICAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(action_area),
                              GTK_BUTTONBOX_START);
    gtk_box_pack_end(GTK_BOX(hbox), action_area);
#else                           /* GTK_INFO_BAR_WRAPPING_IS_BROKEN */
    info_bar_widget = gtk_info_bar_new();
    info_bar = GTK_INFO_BAR(info_bar_widget);

    content_area = gtk_info_bar_get_content_area(info_bar);
    gtk_container_add(GTK_CONTAINER(content_area), grid);

    action_area = gtk_info_bar_get_action_area(info_bar);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(action_area),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(action_area),
                              GTK_BUTTONBOX_START);
#endif                          /* GTK_INFO_BAR_WRAPPING_IS_BROKEN */
    if (!bm->face_box) {
        bm->face_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(action_area), bm->face_box);
        gtk_button_box_set_child_non_homogeneous(GTK_BUTTON_BOX(action_area),
                                                 bm->face_box, TRUE);
    }

    if (buttons) {
        while (*buttons) {
            gtk_container_add(GTK_CONTAINER(action_area), *buttons++);
        }
    }

    widget = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
#ifdef GTK_INFO_BAR_WRAPPING_IS_BROKEN
    gtk_container_add(GTK_CONTAINER(widget), hbox);
#else                           /* GTK_INFO_BAR_WRAPPING_IS_BROKEN */
    gtk_container_add(GTK_CONTAINER(widget), info_bar_widget);
#endif                          /* GTK_INFO_BAR_WRAPPING_IS_BROKEN */

    g_object_set_data(G_OBJECT(widget), BALSA_MESSAGE_GRID, grid);

    return widget;
}

static gboolean
label_size_allocate_cb(GtkLabel * label, GdkRectangle * rectangle,
                       gint baseline, GdkRectangle * clip,
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
    gtk_css_provider_load_from_data(css_provider, css, -1);
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
        value_label = gtk_label_new(sanitized);
        g_free(sanitized);

        gtk_widget_set_name(value_label, BALSA_MESSAGE_HEADER);
        gtk_style_context_add_provider(gtk_widget_get_style_context(value_label) ,
                                       GTK_STYLE_PROVIDER(css_provider),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        gtk_label_set_line_wrap(GTK_LABEL(value_label), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(value_label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_selectable(GTK_LABEL(value_label), TRUE);
        gtk_widget_set_halign(value_label, GTK_ALIGN_START);
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
        gtk_widget_show(hbox);
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

    if (!(bm->shown_headers == HEADERS_ALL ||
	  libbalsa_find_word(header, balsa_app.selected_headers)))
	return;

    value = internet_address_list_to_string(list, FALSE);

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

    if (bm->shown_headers == HEADERS_NONE) {
        g_signal_connect(G_OBJECT(widget), "realize",
                         G_CALLBACK(gtk_widget_hide), NULL);
	return;
    }

    bm->tab_position = 0;

    add_header_gchar(grid, "subject", _("Subject:"), subject,
                     show_all_headers);

    date = libbalsa_message_headers_date_to_utf8(headers,
						 balsa_app.date_string);
    add_header_gchar(grid, "date", _("Date:"), date, show_all_headers);
    g_free(date);

    if (headers->from) {
	gchar *from =
	    internet_address_list_to_string(headers->from, FALSE);
	add_header_gchar(grid, "from", _("From:"), from, show_all_headers);
	g_free(from);
    }

    if (headers->reply_to) {
	gchar *reply_to =
	    internet_address_list_to_string(headers->reply_to, FALSE);
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

#if BALSA_SHOW_FCC_AS_WELL_AS_X_BALSA_FCC
    if (headers->fcc_url)
	add_header_gchar(grid, "fcc", _("FCC:"), headers->fcc_url,
                         show_all_headers);
#endif

    if (headers->dispnotify_to) {
	gchar *mdn_to =
	    internet_address_list_to_string(headers->dispnotify_to, FALSE);
	add_header_gchar(grid, "disposition-notification-to",
			 _("Disposition-Notification-To:"), mdn_to,
                         show_all_headers);
	g_free(mdn_to);
    }

    /* remaining headers */
    for (p = headers->user_hdrs; p != NULL; p = p->next) {
	gchar **pair = p->data;
	gchar *hdr;

	hdr = g_strconcat(pair[0], ":", NULL);
	add_header_gchar(grid, pair[0], hdr, pair[1], show_all_headers);
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
	    add_header_sigstate(grid, part->parts->next->sig_info);
	else if (part->sig_info
		 && part->sig_info->status != GPG_ERR_NOT_SIGNED)
	    /* top-level is OpenPGP (RFC 2440) signed */
	    add_header_sigstate(grid, part->sig_info);
    }
#endif
}

static void
bmw_message_set_headers(BalsaMessage        * bm,
                        BalsaMimeWidget     * mw,
                        LibBalsaMessageBody * part,
                        gboolean              show_all_headers)
{
    GtkWidget *widget;
    GtkGrid *grid;

    bmw_message_set_headers_d(bm, mw, part->embhdrs, part->parts,
                              part->embhdrs ? part->embhdrs->subject : NULL,
                              show_all_headers);

    if ((widget = balsa_mime_widget_get_header_widget(mw)) == NULL)
	return;

    grid = bm_header_widget_get_grid(widget);
    gtk_container_foreach(GTK_CONTAINER(grid), (GtkCallback) foreach_label,
                          part);
}

void
balsa_mime_widget_message_set_headers(BalsaMessage        * bm,
                                      BalsaMimeWidget     * mw,
                                      LibBalsaMessageBody * part)
{
    bmw_message_set_headers(bm, mw, part,
                            bm->shown_headers == HEADERS_ALL);
}

void
balsa_mime_widget_message_set_headers_d(BalsaMessage           * bm,
                                        BalsaMimeWidget        * mw,
                                        LibBalsaMessageHeaders * headers,
                                        LibBalsaMessageBody    * part,
                                        const gchar            * subject)
{
    bmw_message_set_headers_d(bm, mw, headers, part, subject,
                              bm->shown_headers == HEADERS_ALL);
}

#ifdef HAVE_GPGME
/*
 * Add the short status of a signature info siginfo to the message headers in
 * view
 */
static void
add_header_sigstate(GtkGrid * grid, GMimeGpgmeSigstat * siginfo)
{
    gchar *format;
    gchar *msg;
    GtkWidget *label;

    format = siginfo->status ==
        GPG_ERR_NO_ERROR ? "<i>%s%s</i>" : "<b><i>%s%s</i></b>";
    msg = g_markup_printf_escaped
        (format,
         libbalsa_gpgme_sig_protocol_name(siginfo->protocol),
         libbalsa_gpgme_sig_stat_to_gchar(siginfo->status));

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), msg);
    g_free(msg);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_show(label);

    gtk_grid_attach_next_to(grid, label, NULL, GTK_POS_BOTTOM, 2, 1);
}
#endif
