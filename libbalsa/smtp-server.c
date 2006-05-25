/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2005 Stuart Parmenter and others,
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
#if ENABLE_ESMTP
/*
 * LibBalsaSmtpServer is a subclass of LibBalsaServer.
 */

#include <string.h>
#include <libesmtp.h>
#ifdef HAVE_GNOME
#include <gnome.h>
#endif                          /* HAVE_GNOME */


#include "smtp-server.h"
#include "libbalsa-conf.h"
#include "misc.h"
#include "i18n.h"

static LibBalsaServerClass *parent_class = NULL;

struct _LibBalsaSmtpServer {
    LibBalsaServer server;

    gchar *name;
    auth_context_t authctx;
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    gchar *cert_passphrase;
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */
    guint big_message; /* size of partial messages; in kB */
};

typedef struct _LibBalsaSmtpServerClass {
    LibBalsaServerClass parent_class;
} LibBalsaSmtpServerClass;

/* Server class methods */

/* Object class method */

static void
libbalsa_smtp_server_finalize(GObject * object)
{
    LibBalsaServer *server;
    LibBalsaSmtpServer *smtp_server;

    g_return_if_fail(LIBBALSA_IS_SMTP_SERVER(object));

    server = LIBBALSA_SERVER(object);
    smtp_server = LIBBALSA_SMTP_SERVER(object);

    auth_destroy_context(smtp_server->authctx);
    g_free(smtp_server->name);
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    g_free(smtp_server->cert_passphrase);
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
libbalsa_smtp_server_class_init(LibBalsaSmtpServerClass * klass)
{
    GObjectClass *object_class;
    LibBalsaServerClass *server_class;

    object_class = G_OBJECT_CLASS(klass);
    server_class = LIBBALSA_SERVER_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    object_class->finalize = libbalsa_smtp_server_finalize;
}

/* Callback to get user/password info from SMTP server preferences.
   This is adequate for simple username / password requests but does
   not adequately cope with all SASL mechanisms.  */
static int
authinteract(auth_client_request_t request, char **result, int fields,
             void *arg)
{
    LibBalsaServer *server = LIBBALSA_SERVER(arg);
    int i;

    for (i = 0; i < fields; i++) {
        if (request[i].flags & AUTH_PASS)
            result[i] = server->passwd;
        else if (request[i].flags & AUTH_USER)
            result[i] = (server->user
                         && *server->user) ? server->user : NULL;

        /* Fail the AUTH exchange if something was requested
           but not supplied. */
        if (result[i] == NULL)
            return 0;
    }

    return 1;
}

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
static int
tlsinteract(char *buf, int buflen, int rwflag, void *arg)
{
    LibBalsaSmtpServer *smtp_server = LIBBALSA_SMTP_SERVER(arg);
    char *pw;
    int len;

    pw = smtp_server->cert_passphrase;
    len = strlen(pw);
    if (len + 1 > buflen)
        return 0;
    strcpy(buf, pw);
    return len;
}
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */

static void
libbalsa_smtp_server_init(LibBalsaSmtpServer * smtp_server)
{
    smtp_server->authctx = auth_create_context();
    auth_set_mechanism_flags(smtp_server->authctx, AUTH_PLUGIN_PLAIN, 0);
    auth_set_interact_cb(smtp_server->authctx, authinteract, smtp_server);

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    /* Use our callback for X.509 certificate passwords.  If STARTTLS is
       not in use or disabled in configure, the following is harmless. */
    smtp_server->cert_passphrase = NULL;
    smtp_starttls_set_password_cb(tlsinteract, smtp_server);
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */
}

static void libbalsa_smtp_server_finalize(GObject * object);

/* Class boilerplate */

GType
libbalsa_smtp_server_get_type(void)
{
    static GType server_type = 0;

    if (!server_type) {
        static const GTypeInfo server_info = {
            sizeof(LibBalsaSmtpServerClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) libbalsa_smtp_server_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(LibBalsaSmtpServer),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) libbalsa_smtp_server_init
        };

        server_type =
            g_type_register_static(LIBBALSA_TYPE_SERVER,
                                   "LibBalsaSmtpServer", &server_info, 0);
    }

    return server_type;
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

    return smtp_server;
}

LibBalsaSmtpServer *
libbalsa_smtp_server_new_from_config(const gchar * name)
{
    LibBalsaSmtpServer *smtp_server;

    smtp_server = libbalsa_smtp_server_new();
    smtp_server->name = g_strdup(name);

    libbalsa_server_load_config(LIBBALSA_SERVER(smtp_server));

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    smtp_server->cert_passphrase =
        libbalsa_conf_private_get_string("CertificatePassphrase");
    if (smtp_server->cert_passphrase) {
        gchar *tmp = libbalsa_rot(smtp_server->cert_passphrase);
        g_free(smtp_server->cert_passphrase);
        smtp_server->cert_passphrase = tmp;
    }
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */

    smtp_server->big_message = libbalsa_conf_get_int("BigMessage=0");

    return smtp_server;
}

void
libbalsa_smtp_server_save_config(LibBalsaSmtpServer * smtp_server)
{
    /* FIXME: isn't it a bit of a brute force? */
    LIBBALSA_SERVER(smtp_server)->remember_passwd = TRUE;
    libbalsa_server_save_config(LIBBALSA_SERVER(smtp_server));

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    if (smtp_server->cert_passphrase) {
        gchar *tmp = libbalsa_rot(smtp_server->cert_passphrase);
        libbalsa_conf_private_set_string("CertificatePassphrase", tmp);
        g_free(tmp);
    }
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */
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

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
void
libbalsa_smtp_server_set_cert_passphrase(LibBalsaSmtpServer * smtp_server,
                                         const gchar * passphrase)
{
    g_free(smtp_server->cert_passphrase);
    smtp_server->cert_passphrase = g_strdup(passphrase);
}

const gchar *
libbalsa_smtp_server_get_cert_passphrase(LibBalsaSmtpServer * smtp_server)
{
    return smtp_server->cert_passphrase;
}
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */

auth_context_t
libbalsa_smtp_server_get_authctx(LibBalsaSmtpServer * smtp_server)
{
    return smtp_server->authctx;
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

    if (smtp_server_a->name && smtp_server_b->name)
        return strcmp(smtp_server_a->name, smtp_server_b->name);

    return smtp_server_a->name - smtp_server_b->name;
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

/* SMTP server dialog */

#define LIBBALSA_SMTP_SERVER_DIALOG_KEY "libbalsa-smtp-server-dialog"

struct smtp_server_dialog_info {
    LibBalsaSmtpServer *smtp_server;
    gchar *old_name;
    LibBalsaSmtpServerUpdate update;
    GtkWidget *dialog;
    GtkWidget *name;
    GtkWidget *host;
    GtkWidget *user;
    GtkWidget *pass;
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    GtkWidget *tlsm;
    GtkWidget *cert;
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */
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
smtp_server_add_widget(GtkWidget * table, gint row, const gchar * text,
                       GtkWidget * widget)
{
    GtkWidget *label = libbalsa_create_label(text, table, row);
    gtk_table_attach_defaults(GTK_TABLE(table), widget,
                              1, 2, row, row + 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), widget);
}

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
static
GtkWidget *
smtp_server_tls_widget(LibBalsaSmtpServer * smtp_server)
{
    LibBalsaServer *server = LIBBALSA_SERVER(smtp_server);
    GtkWidget *combo_box = gtk_combo_box_new_text();

    gtk_combo_box_append_text(GTK_COMBO_BOX(combo_box), _("Never"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo_box), _("If Possible"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(combo_box), _("Required"));

    switch (server->tls_mode) {
    case Starttls_DISABLED:
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), 0);
        break;
    case Starttls_ENABLED:
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), 1);
        break;
    case Starttls_REQUIRED:
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), 2);
        break;
    default:
        break;
    }

    return combo_box;
}
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */

static const gchar smtp_server_section[] = "smtp-server-config";

static void
smtp_server_response(GtkDialog * dialog, gint response,
                     struct smtp_server_dialog_info *sdi)
{
    LibBalsaServer *server = LIBBALSA_SERVER(sdi->smtp_server);
#ifdef HAVE_GNOME
    GError *error = NULL;
#endif                          /* HAVE_GNOME */

    switch (response) {
    case GTK_RESPONSE_HELP:
#ifdef HAVE_GNOME
        gnome_help_display("balsa", smtp_server_section, &error);
        if (error) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Error displaying %s: %s\n"),
                                 smtp_server_section, error->message);
            g_error_free(error);
        }
#endif                          /* HAVE_GNOME */
        return;
    case GTK_RESPONSE_OK:
        libbalsa_smtp_server_set_name(sdi->smtp_server,
                                      gtk_entry_get_text(GTK_ENTRY
                                                         (sdi->name)));
        libbalsa_server_set_host(server,
                                 gtk_entry_get_text(GTK_ENTRY(sdi->host)),
                                 FALSE);
        libbalsa_server_set_username(server,
                                     gtk_entry_get_text(GTK_ENTRY
                                                        (sdi->user)));
        libbalsa_server_set_password(server,
                                     gtk_entry_get_text(GTK_ENTRY
                                                        (sdi->pass)));
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
        switch (gtk_combo_box_get_active(GTK_COMBO_BOX(sdi->tlsm))) {
        case 0:
            server->tls_mode = Starttls_DISABLED;
            break;
        case 1:
            server->tls_mode = Starttls_ENABLED;
            break;
        case 2:
            server->tls_mode = Starttls_REQUIRED;
            break;
        default:
            break;
        }
        libbalsa_smtp_server_set_cert_passphrase(sdi->smtp_server,
                                                 gtk_entry_get_text
                                                 (GTK_ENTRY(sdi->cert)));
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */
        if (gtk_toggle_button_get_active
            (GTK_TOGGLE_BUTTON(sdi->split_button)))
            /* big_message is stored in kB, but the widget is in MB. */
            LIBBALSA_SMTP_SERVER(server)->big_message =
                gtk_spin_button_get_value(GTK_SPIN_BUTTON
                                          (sdi->big_message)) * 1024;
        else
            LIBBALSA_SMTP_SERVER(server)->big_message = 0;
        break;
    default:
        break;
    }

    /* The update may unref the server, so we temporarily ref it;
     * we use server instead of sdi->smtp_server, as sdi is deallocated
     * when the object data is cleared. */
    g_object_ref(server);
    sdi->update(sdi->smtp_server, response, sdi->old_name);
    g_object_unref(server);

    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
smtp_server_changed(GtkWidget * widget,
                    struct smtp_server_dialog_info *sdi)
{
    gboolean ok;

    /* Minimal sanity check: Name and Host fields both non-blank. */
    ok = *gtk_entry_get_text(GTK_ENTRY(sdi->name))
        && *gtk_entry_get_text(GTK_ENTRY(sdi->host));

    gtk_dialog_set_response_sensitive(GTK_DIALOG(sdi->dialog),
                                      GTK_RESPONSE_OK, ok);
    gtk_dialog_set_default_response(GTK_DIALOG(sdi->dialog),
                                    ok ? GTK_RESPONSE_OK :
                                    GTK_RESPONSE_CANCEL);
}

static void
smtp_server_split_button_changed(GtkWidget * button,
                                 struct smtp_server_dialog_info *sdi)
{
    gtk_widget_set_sensitive(sdi->big_message,
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                                          (button)));
    smtp_server_changed(button, sdi);
}

void
libbalsa_smtp_server_dialog(LibBalsaSmtpServer * smtp_server,
                            GtkWindow * parent,
                            LibBalsaSmtpServerUpdate update)
{
    LibBalsaServer *server = LIBBALSA_SERVER(smtp_server);
    struct smtp_server_dialog_info *sdi;
    GtkWidget *dialog;
    GtkWidget *table;
    gint row;
    GtkWidget *label, *hbox;

    /* Show only one dialog at a time. */
    sdi = g_object_get_data(G_OBJECT(smtp_server),
                            LIBBALSA_SMTP_SERVER_DIALOG_KEY);
    if (sdi) {
        gdk_window_raise(sdi->dialog->window);
        return;
    }

    sdi = g_new(struct smtp_server_dialog_info, 1);
    g_object_set_data_full(G_OBJECT(smtp_server),
                           LIBBALSA_SMTP_SERVER_DIALOG_KEY, sdi,
                           (GDestroyNotify) smtp_server_destroy_notify);

    sdi->smtp_server = smtp_server;
    sdi->old_name = g_strdup(libbalsa_smtp_server_get_name(smtp_server));
    sdi->update = update;
    sdi->dialog = dialog =
        gtk_dialog_new_with_buttons(_("SMTP Server"),
                                    parent,
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                                    NULL);
    g_object_weak_ref(G_OBJECT(dialog),
		    (GWeakNotify) smtp_server_weak_notify, sdi);
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(smtp_server_response), sdi);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    GTK_RESPONSE_CANCEL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK,
                                      FALSE);

#define HIG_PADDING 12
    table = libbalsa_create_table(6, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);

    row = 0;
    smtp_server_add_widget(table, row, _("_Descriptive Name:"),
                           sdi->name = gtk_entry_new());
    if (smtp_server->name)
        gtk_entry_set_text(GTK_ENTRY(sdi->name), smtp_server->name);
    g_signal_connect(sdi->name, "changed", G_CALLBACK(smtp_server_changed),
                     sdi);

    smtp_server_add_widget(table, ++row, _("_Server:"),
                           sdi->host = gtk_entry_new());
    if (server->host)
        gtk_entry_set_text(GTK_ENTRY(sdi->host), server->host);
    g_signal_connect(sdi->host, "changed", G_CALLBACK(smtp_server_changed),
                     sdi);

    smtp_server_add_widget(table, ++row, _("_User Name:"),
                           sdi->user = gtk_entry_new());
    if (server->user)
        gtk_entry_set_text(GTK_ENTRY(sdi->user), server->user);
    g_signal_connect(sdi->user, "changed", G_CALLBACK(smtp_server_changed),
                     sdi);

    smtp_server_add_widget(table, ++row, _("_Pass Phrase:"),
                           sdi->pass = gtk_entry_new());
    gtk_entry_set_visibility(GTK_ENTRY(sdi->pass), FALSE);
    if (server->passwd)
        gtk_entry_set_text(GTK_ENTRY(sdi->pass), server->passwd);
    g_signal_connect(sdi->pass, "changed", G_CALLBACK(smtp_server_changed),
                     sdi);

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    smtp_server_add_widget(table, ++row, _("Use _TLS:"), sdi->tlsm =
                           smtp_server_tls_widget(smtp_server));
    g_signal_connect(sdi->tlsm, "changed", G_CALLBACK(smtp_server_changed),
                     sdi);

    smtp_server_add_widget(table, ++row, _("C_ertificate Pass Phrase:"),
                           sdi->cert = gtk_entry_new());
    gtk_entry_set_visibility(GTK_ENTRY(sdi->cert), FALSE);
    if (smtp_server->cert_passphrase)
        gtk_entry_set_text(GTK_ENTRY(sdi->cert),
                           smtp_server->cert_passphrase);
    g_signal_connect(sdi->cert, "changed", G_CALLBACK(smtp_server_changed),
                     sdi);
#endif                          /* HAVE_SMTP_TLS_CLIENT_CERTIFICATE */

    ++row;
    sdi->split_button =
        gtk_check_button_new_with_mnemonic(_("Sp_lit message larger than"));
    gtk_table_attach_defaults(GTK_TABLE(table), sdi->split_button,
                              0, 1, row, row + 1);
    hbox = gtk_hbox_new(FALSE, 6);
    sdi->big_message = gtk_spin_button_new_with_range(0.1, 100, 0.1);
    gtk_box_pack_start(GTK_BOX(hbox), sdi->big_message, TRUE, TRUE, 0);
    label = gtk_label_new(_("MB"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    if (smtp_server->big_message > 0) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdi->split_button),
                                     TRUE);
        /* The widget is in MB, but big_message is stored in kB. */
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sdi->big_message),
                                  ((float) smtp_server->big_message) /
                                  1024);
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdi->split_button),
                                     FALSE);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sdi->big_message), 1);
        gtk_widget_set_sensitive(sdi->big_message, FALSE);
    }
    g_signal_connect(sdi->split_button, "toggled",
                     G_CALLBACK(smtp_server_split_button_changed), sdi);
    g_signal_connect(sdi->big_message, "changed",
                     G_CALLBACK(smtp_server_changed), sdi);
    gtk_table_attach_defaults(GTK_TABLE(table), hbox, 1, 2, row, row + 1);

    gtk_widget_show_all(dialog);
}

#endif                          /* ENABLE_ESMTP */
