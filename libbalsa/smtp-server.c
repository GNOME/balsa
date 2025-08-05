/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

/*
 * LibBalsaSmtpServer is a subclass of LibBalsaServer.
 */
#include "smtp-server.h"

#include <glib/gi18n.h>

#include "libbalsa-conf.h"
#include "server-config.h"

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "libbalsa-server"


struct _LibBalsaSmtpServer {
    LibBalsaServer server;

    gchar *name;
    guint big_message; /* size of partial messages; in kB; 0 disables splitting */
    gint lock_state;	/* 0 means unlocked; access via atomic operations */
};

/* Class boilerplate */

G_DEFINE_TYPE(LibBalsaSmtpServer, libbalsa_smtp_server, LIBBALSA_TYPE_SERVER)

/* Object class method */

static void
libbalsa_smtp_server_finalize(GObject * object)
{
    LibBalsaSmtpServer *smtp_server;

    g_return_if_fail(LIBBALSA_IS_SMTP_SERVER(object));

    smtp_server = LIBBALSA_SMTP_SERVER(object);

    g_free(smtp_server->name);

    G_OBJECT_CLASS(libbalsa_smtp_server_parent_class)->finalize(object);
}

static void
libbalsa_smtp_server_class_init(LibBalsaSmtpServerClass * klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = libbalsa_smtp_server_finalize;
}

static void
libbalsa_smtp_server_init(LibBalsaSmtpServer * smtp_server)
{
    libbalsa_server_set_protocol(LIBBALSA_SERVER(smtp_server), "smtp");
}

/* Public methods */

/**
 * libbalsa_smtp_server_new:
 * @username: username to use to login
 * @host: hostname of server
 *
 * Creates or recycles a #LibBalsaSmtpServer matching the host+username pair.
 *
 * Return value: A #LibBalsaSmtpServer
 */
LibBalsaSmtpServer *
libbalsa_smtp_server_new(void)
{
    LibBalsaSmtpServer *smtp_server;

    smtp_server = g_object_new(LIBBALSA_TYPE_SMTP_SERVER, NULL);

    /* Change the default. */
    libbalsa_server_set_remember_password(LIBBALSA_SERVER(smtp_server), TRUE);

    return smtp_server;
}

LibBalsaSmtpServer *
libbalsa_smtp_server_new_from_config(const gchar * name)
{
    LibBalsaSmtpServer *smtp_server;

    smtp_server = libbalsa_smtp_server_new();
    smtp_server->name = g_strdup(name);

    libbalsa_server_load_config(LIBBALSA_SERVER(smtp_server));

    smtp_server->big_message = libbalsa_conf_get_int("BigMessage=0");

    return smtp_server;
}

void
libbalsa_smtp_server_save_config(LibBalsaSmtpServer * smtp_server)
{
    libbalsa_server_save_config(LIBBALSA_SERVER(smtp_server));

    libbalsa_conf_set_int("BigMessage", smtp_server->big_message);
}

void
libbalsa_smtp_server_set_name(LibBalsaSmtpServer * smtp_server,
                              const gchar * name)
{
    g_free(smtp_server->name);
    smtp_server->name = g_strdup(name);
}

const gchar *
libbalsa_smtp_server_get_name(LibBalsaSmtpServer * smtp_server)
{
    return smtp_server ? smtp_server->name : _("Default");
}

guint
libbalsa_smtp_server_get_big_message(LibBalsaSmtpServer * smtp_server)
{
    /* big_message is stored in kB, but we want the value in bytes. */
    return smtp_server->big_message * 1024;
}

static gint
smtp_server_compare(gconstpointer a, gconstpointer b)
{
    const LibBalsaSmtpServer *smtp_server_a = a;
    const LibBalsaSmtpServer *smtp_server_b = b;

    return g_strcmp0(smtp_server_a->name, smtp_server_b->name);
}

void
libbalsa_smtp_server_add_to_list(LibBalsaSmtpServer * smtp_server,
                                 GSList ** server_list)
{
    GSList *list;

    if ((list =
         g_slist_find_custom(*server_list, smtp_server,
                             smtp_server_compare)) != NULL) {
        g_object_unref(list->data);
        *server_list = g_slist_delete_link(*server_list, list);
    }

    *server_list = g_slist_prepend(*server_list, smtp_server);
}

gboolean
libbalsa_smtp_server_trylock(LibBalsaSmtpServer *smtp_server)
{
	gint prev_state;
	gboolean result;

	prev_state = g_atomic_int_add(&smtp_server->lock_state, 1);
	if (prev_state == 0) {
		result = TRUE;
	} else {
		result = FALSE;
		(void) g_atomic_int_dec_and_test(&smtp_server->lock_state);
	}
	g_debug("%s: lock %s: %d", __func__, libbalsa_smtp_server_get_name(smtp_server), result);
	return result;
}

void
libbalsa_smtp_server_unlock(LibBalsaSmtpServer *smtp_server)
{
	(void) g_atomic_int_dec_and_test(&smtp_server->lock_state);
	g_debug("%s: unlock %s", __func__, libbalsa_smtp_server_get_name(smtp_server));
}

/* SMTP server dialog */

#define LIBBALSA_SMTP_SERVER_DIALOG_KEY "libbalsa-smtp-server-dialog"

struct smtp_server_dialog_info {
    LibBalsaSmtpServer *smtp_server;
    gchar *old_name;
    LibBalsaSmtpServerUpdate update;
    GtkWidget *dialog;
    LibBalsaServerCfg *notebook;
    GtkWidget *split_button;
    GtkWidget *big_message;
};

/* GDestroyNotify for smtp_server_dialog_info. */
static void
smtp_server_destroy_notify(struct smtp_server_dialog_info *sdi)
{
    g_free(sdi->old_name);
    if (sdi->dialog)
        gtk_widget_destroy(sdi->dialog);
    g_free(sdi);
}

/* GWeakNotify for dialog. */
static void 
smtp_server_weak_notify(struct smtp_server_dialog_info *sdi, GObject *dialog)
{
    sdi->dialog = NULL;
    g_object_set_data(G_OBJECT(sdi->smtp_server),
                      LIBBALSA_SMTP_SERVER_DIALOG_KEY, NULL);
}

static void
smtp_server_response(GtkDialog * dialog, gint response,
                     struct smtp_server_dialog_info *sdi)
{
    LibBalsaServer *server = LIBBALSA_SERVER(sdi->smtp_server);
    GError *error = NULL;

    switch (response) {
    case GTK_RESPONSE_HELP:
        gtk_show_uri_on_window(GTK_WINDOW(dialog),
                               "help:balsa/preferences-mail-options#smtp-server-config",
                               gtk_get_current_event_time(), &error);
        if (error) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Error displaying server help: %s\n"),
                                 error->message);
            g_error_free(error);
        }
        return;
    case GTK_RESPONSE_OK:
        libbalsa_smtp_server_set_name(sdi->smtp_server, libbalsa_server_cfg_get_name(sdi->notebook));
        libbalsa_server_cfg_assign_server(sdi->notebook, server);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sdi->split_button))) {
            /* big_message is stored in kB, but the widget is in MB. */
        	sdi->smtp_server->big_message =
                gtk_spin_button_get_value(GTK_SPIN_BUTTON(sdi->big_message)) * 1024.0;
        } else {
        	sdi->smtp_server->big_message = 0U;
        }
        /* The update may unref the server, so we temporarily ref it;
         * we use server instead of sdi->smtp_server, as sdi is deallocated
         * when the object data is cleared. */
        g_object_ref(server);
        sdi->update(sdi->smtp_server, sdi->old_name);
        g_object_unref(server);
        break;
    default:
        /* user cancelled */
        if (sdi->old_name == NULL) {
            /* new server: destroy it */
            g_object_unref(server);
        }
        break;
    }

    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
smtp_server_changed(GtkWidget G_GNUC_UNUSED *widget,
                    struct smtp_server_dialog_info *sdi)
{
	gboolean sensitive;
	gboolean enable_ok = libbalsa_server_cfg_valid(sdi->notebook);

	/* split big messages */
	if ((sdi->big_message != NULL) && (sdi->split_button != NULL)) {
		sensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sdi->split_button));
	    gtk_widget_set_sensitive(sdi->big_message, sensitive);
	}

    gtk_dialog_set_response_sensitive(GTK_DIALOG(sdi->dialog), GTK_RESPONSE_OK, enable_ok);
    gtk_dialog_set_default_response(GTK_DIALOG(sdi->dialog),
    	enable_ok ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
}

void
libbalsa_smtp_server_dialog(LibBalsaSmtpServer * smtp_server,
                            GtkWindow * parent,
                            LibBalsaSmtpServerUpdate update)
{
    LibBalsaServer *server = LIBBALSA_SERVER(smtp_server);
    struct smtp_server_dialog_info *sdi;
    GtkWidget *dialog;
    const gchar *action;
    GtkWidget *content_area;
    GtkWidget *label, *hbox;

    /* Show only one dialog at a time. */
    sdi = g_object_get_data(G_OBJECT(smtp_server),
                            LIBBALSA_SMTP_SERVER_DIALOG_KEY);
    if (sdi != NULL) {
        gtk_window_present_with_time(GTK_WINDOW(sdi->dialog),
                                     gtk_get_current_event_time());
        return;
    }

    sdi = g_new0(struct smtp_server_dialog_info, 1U);
    g_object_set_data_full(G_OBJECT(smtp_server),
                           LIBBALSA_SMTP_SERVER_DIALOG_KEY, sdi,
                           (GDestroyNotify) smtp_server_destroy_notify);

    sdi->smtp_server = smtp_server;
    sdi->old_name = g_strdup(libbalsa_smtp_server_get_name(smtp_server));
    action = (sdi->old_name == NULL) ? _("_Add") : _("_Apply");
    sdi->update = update;
    sdi->dialog = dialog =
        gtk_dialog_new_with_buttons(_("SMTP Server"),
                                    parent,
                                    GTK_DIALOG_DESTROY_WITH_PARENT |
                                    libbalsa_dialog_flags(),
                                    action,       GTK_RESPONSE_OK,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_Help"),   GTK_RESPONSE_HELP,
                                    NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    g_object_weak_ref(G_OBJECT(dialog),
		    (GWeakNotify) smtp_server_weak_notify, sdi);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(smtp_server_response), sdi);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    GTK_RESPONSE_CANCEL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK,
                                      FALSE);

    sdi->notebook = libbalsa_server_cfg_new(server, smtp_server->name);
    gtk_container_add(GTK_CONTAINER(content_area), GTK_WIDGET(sdi->notebook));

    /* split large messages */
    sdi->split_button = gtk_check_button_new_with_mnemonic(_("Sp_lit message larger than"));
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    sdi->big_message = gtk_spin_button_new_with_range(0.1, 100, 0.1);
    gtk_widget_set_hexpand(sdi->big_message, TRUE);
    gtk_widget_set_halign(sdi->big_message, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(hbox), sdi->big_message);
    label = gtk_label_new(_("MB"));
    gtk_container_add(GTK_CONTAINER(hbox), label);
    if (smtp_server->big_message > 0) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdi->split_button), TRUE);
        /* The widget is in MB, but big_message is stored in kB. */
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sdi->big_message),
                                  ((gdouble) smtp_server->big_message) / 1024.0);
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdi->split_button), FALSE);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sdi->big_message), 1);
    }
    libbalsa_server_cfg_add_row(sdi->notebook, FALSE, sdi->split_button, hbox);
    g_signal_connect(sdi->notebook, "changed", G_CALLBACK(smtp_server_changed), sdi);
    g_signal_connect(sdi->split_button, "toggled", G_CALLBACK(smtp_server_changed), sdi);
    g_signal_connect(sdi->big_message, "changed", G_CALLBACK(smtp_server_changed), sdi);

    gtk_widget_show_all(dialog);
}
