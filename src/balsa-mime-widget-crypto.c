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
#include "balsa-mime-widget-crypto.h"

#ifdef HAVE_GPGME
#include "balsa-app.h"
#include "balsa-icons.h"
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include "libbalsa-gpgme.h"
#include "libbalsa-gpgme-widgets.h"
#include "libbalsa-gpgme-keys.h"
#include "balsa-mime-widget.h"


static void on_gpg_key_button(GtkWidget *button, const gchar *fingerprint);
static void on_key_import_button(GtkButton *button, gpointer user_data);
static gboolean create_import_keys_widget(GtkBox *box, const gchar *key_buf, GError **error);
static void show_public_key_data(GtkExpander *expander, gpointer user_data);


BalsaMimeWidget *
balsa_mime_widget_new_signature(BalsaMessage * bm,
				LibBalsaMessageBody * mime_body,
				const gchar * content_type, gpointer data)
{
    BalsaMimeWidget *mw;

    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);
    
    if (!mime_body->sig_info)
	return NULL;

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    balsa_mime_widget_set_widget(mw,
                                 balsa_mime_widget_signature_widget(mime_body,
                                                                    content_type));
    
    return mw;
}

BalsaMimeWidget *
balsa_mime_widget_new_pgpkey(BalsaMessage        *bm,
							 LibBalsaMessageBody *mime_body,
							 const gchar 		 *content_type,
							 gpointer			  data)
{
    gssize body_size;
    gchar *body_buf = NULL;
    GError *err = NULL;
	BalsaMimeWidget *mw = NULL;

    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);

    body_size = libbalsa_message_body_get_content(mime_body, &body_buf, &err);
    if (body_size < 0) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Could not save a text part: %s"),
                          err ? err->message : "Unknown error");
        g_clear_error(&err);
    } else {
        GtkWidget *box;

        mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
        box = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_VBOX_SPACE);
        balsa_mime_widget_set_widget(mw, box);
        if (!create_import_keys_widget(GTK_BOX(box), body_buf, &err)) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Could not process GnuPG keys: %s"),
                              err != NULL ? err->message : "Unknown error");
            g_clear_error(&err);
            g_object_ref_sink(box);
            g_object_unref(box);
            g_object_unref(mw);
            mw = NULL;
        }
    	g_free(body_buf);
    }

    return mw;
}

GtkWidget *
balsa_mime_widget_signature_widget(LibBalsaMessageBody * mime_body,
				   const gchar * content_type)
{
    gchar *infostr;
    GtkWidget *expander;
    GtkWidget *vbox, *label;
    GtkWidget *signature_widget;
    gchar **lines;

    if (!mime_body->sig_info ||
	mime_body->sig_info->status == GPG_ERR_NOT_SIGNED)
	return NULL;

    infostr = g_mime_gpgme_sigstat_to_gchar(mime_body->sig_info, FALSE, balsa_app.date_string);
    if (infostr == NULL) {
        return NULL;
    }
    lines = g_strsplit(infostr, "\n", 2);
    g_free(infostr);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_VBOX_SPACE);
    label = gtk_label_new((lines[1] != NULL) ? lines[1] : lines[0]);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), label);
    if (mime_body->sig_info->protocol == GPGME_PROTOCOL_OpenPGP) {
        GtkWidget *button;

        if (mime_body->sig_info->status == GPG_ERR_NO_PUBKEY) {
            button = gtk_button_new_with_mnemonic(_("_Search key server for this key"));
        } else {
            button = gtk_button_new_with_mnemonic(_("_Search key server for updates of this key"));
        }
        g_signal_connect(G_OBJECT(button), "clicked",
                         G_CALLBACK(on_gpg_key_button),
                         (gpointer)mime_body->sig_info->fingerprint);
        gtk_box_pack_start(GTK_BOX(vbox), button);
    }

    /* Hack alert: if we omit the box below and use the expander as signature widget
     * directly, setting the container border width of the container = the expander
     * causes its sensitive area to shrink to an almost unusable narrow line above
     * the label... */
    signature_widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    expander = gtk_expander_new(lines[0]);
    gtk_container_add(GTK_CONTAINER(signature_widget), expander);
    gtk_container_add(GTK_CONTAINER(expander), vbox);

    /* add a callback to load the key when the user wants to show the details
     * Note: do *not* pass mime_body->sig_info to the callback, as it will be replaced when the user re-checks the signature or
     * opens the message again in a separate window */
    if (((mime_body->sig_info->summary & GPGME_SIGSUM_KEY_MISSING) == 0) &&
    	(mime_body->sig_info->key == NULL)) {
    	g_signal_connect(expander, "activate", (GCallback) show_public_key_data, mime_body);
    	g_object_set_data(G_OBJECT(expander), "vbox", vbox);
    }
    g_object_set(G_OBJECT(signature_widget), "margin", BMW_CONTAINER_BORDER, NULL);

    g_strfreev(lines);

    return signature_widget;
}


GtkWidget *
balsa_mime_widget_crypto_frame(LibBalsaMessageBody * mime_body, GtkWidget * child,
			       gboolean was_encrypted, gboolean no_signature,
			       GtkWidget * signature)
{
    GtkWidget * frame;
    GtkWidget * vbox;
    GtkWidget * icon_box;

    frame = gtk_frame_new(NULL);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_VBOX_SPACE);
    gtk_container_add(GTK_CONTAINER(frame), vbox);
    icon_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BMW_VBOX_SPACE);
    if (was_encrypted)
        gtk_box_pack_start(GTK_BOX(icon_box),
                           gtk_image_new_from_icon_name
                           (balsa_icon_id(BALSA_PIXMAP_ENCR)));
    if (!no_signature) {
	const gchar * icon_name =
	    balsa_mime_widget_signature_icon_name(libbalsa_message_body_protect_state(mime_body));
	if (!icon_name)
	    icon_name = BALSA_PIXMAP_SIGN;
        gtk_box_pack_start(GTK_BOX(icon_box),
                           gtk_image_new_from_icon_name
                           (balsa_icon_id(icon_name)));
    }
    gtk_frame_set_label_widget(GTK_FRAME(frame), icon_box);
    g_object_set(G_OBJECT(vbox), "margin", BMW_MESSAGE_PADDING, NULL);

    gtk_box_pack_start(GTK_BOX(vbox), child);

    if (signature) {
	gtk_box_pack_end(GTK_BOX(vbox), signature);
    }

    return frame;
}


/*
 * get the proper icon name for a given protection state
 */
const gchar *
balsa_mime_widget_signature_icon_name(LibBalsaMsgProtectState protect_state)
{
    switch (protect_state) {
    case LIBBALSA_MSG_PROTECT_SIGN_GOOD:
        return BALSA_PIXMAP_SIGN_GOOD;
    case LIBBALSA_MSG_PROTECT_SIGN_NOTRUST:
        return BALSA_PIXMAP_SIGN_NOTRUST;
    case LIBBALSA_MSG_PROTECT_SIGN_BAD:
        return BALSA_PIXMAP_SIGN_BAD;
    default:
        return NULL;
    }
}


/* Callback: try to import a public key */
static void
on_gpg_key_button(GtkWidget   *button,
				  const gchar *fingerprint)
{
	GError *error = NULL;

    if (!libbalsa_gpgme_keyserver_op(fingerprint, balsa_get_parent_window(button), &error)) {
    	libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s", error->message);
    	g_error_free(error);
    } else {
		gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    }
}


/* Callback: import an attached key */
static void
on_key_import_button(GtkButton *button,
					 gpointer   user_data)
{
	gpgme_ctx_t ctx;
	gboolean success;
	GError *error = NULL;
	gchar *import_info = NULL;
	GtkWidget *dialog;

	ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, NULL, NULL, &error);
	if (ctx != NULL) {
		success = libbalsa_gpgme_import_ascii_key(ctx, g_object_get_data(G_OBJECT(button), "keydata"), &import_info, &error);
		gpgme_release(ctx);
	} else {
		success = FALSE;
	}

	if (success) {
		dialog = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
			GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_CLOSE,
			_("Import GnuPG key:\n%s"), import_info);
		gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
	} else {
		dialog = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
			GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			_("Error importing key data: %s"), error->message);
		g_clear_error(&error);
	}
	g_free(import_info);
	(void) gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}


static gboolean
create_import_keys_widget(GtkBox *box, const gchar *key_buf, GError **error)
{
	gboolean success = FALSE;
	gpgme_ctx_t ctx;

	ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, NULL, NULL, error);
	if (ctx != NULL) {
		gchar *temp_dir = NULL;

		if (!libbalsa_mktempdir(&temp_dir)) {
			g_warning("Failed to create a temporary folder");
		} else {
			GList *keys = NULL;

			success = libbalsa_gpgme_ctx_set_home(ctx, temp_dir, error) &&
				libbalsa_gpgme_import_ascii_key(ctx, key_buf, NULL, error) &&
				libbalsa_gpgme_list_keys(ctx, &keys, NULL, NULL, FALSE, FALSE, error);

			if (success && (keys != NULL)) {
				GList *item;

				for (item = keys; success && (item != NULL); item = item->next) {
					gpgme_key_t this_key = (gpgme_key_t) item->data;
					gchar *key_ascii;
					GtkWidget *key_widget;
					GtkWidget *import_btn;

					key_ascii = libbalsa_gpgme_export_key(ctx, this_key, _("(imported)"), error);

					if (key_ascii == NULL) {
						success = FALSE;
					} else {
						key_widget = libbalsa_gpgme_key(this_key, NULL, GPG_SUBKEY_CAP_ALL, FALSE);
						gtk_box_pack_start(box, key_widget);

						import_btn = gtk_button_new_with_label(_("Import key into the local key ring"));
						g_object_set_data_full(G_OBJECT(import_btn), "keydata", key_ascii, (GDestroyNotify) g_free);
						g_signal_connect(G_OBJECT(import_btn), "clicked", (GCallback) on_key_import_button, NULL);
						gtk_box_pack_start(box, import_btn);

						if (item->next != NULL) {
                                                    GtkWidget *separator;

							separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                                                        gtk_widget_set_margin_top(separator, BMW_VBOX_SPACE);
							gtk_box_pack_start(box, separator);
						}
					}
				}

				g_list_free_full(keys, (GDestroyNotify) gpgme_key_release);
			}

			libbalsa_delete_directory_contents(temp_dir);
			g_rmdir(temp_dir);
		}

		gpgme_release(ctx);
	}

	return success;
}

static void
show_public_key_data(GtkExpander *expander,
                     gpointer     user_data)
{
	LibBalsaMessageBody *body = (LibBalsaMessageBody *) user_data;

	g_message("%s: %p %p %p", __func__, expander, body, body->sig_info);
	if (body->sig_info != NULL) {
		if (body->sig_info->key == NULL) {
			g_mime_gpgme_sigstat_load_key(body->sig_info);
		}

		if ((g_object_get_data(G_OBJECT(expander), "vbox") != NULL) && (body->sig_info->key != NULL)) {
			GtkWidget *key_widget;
			GtkBox *vbox;

			vbox = GTK_BOX(g_object_steal_data(G_OBJECT(expander), "vbox"));
			key_widget = libbalsa_gpgme_key(body->sig_info->key, body->sig_info->fingerprint, 0U, FALSE);
			gtk_box_pack_start(vbox, key_widget, FALSE, FALSE, 0);
			gtk_box_reorder_child(vbox, key_widget, 1U);
			gtk_widget_show_all(key_widget);
		}
	}
}

#endif  /* HAVE_GPGME */
