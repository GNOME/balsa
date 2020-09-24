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
#include "balsa-mime-widget-crypto.h"

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


BalsaMimeWidget *
balsa_mime_widget_new_signature(BalsaMessage * bm,
				LibBalsaMessageBody * mime_body,
				const gchar * content_type, gpointer data)
{
    BalsaMimeWidget *mw;

    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);

    if (mime_body->sig_info == NULL)
	return NULL;

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    gtk_container_add(GTK_CONTAINER(mw),
                      balsa_mime_widget_signature_widget(mime_body, content_type));

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
        balsa_information(LIBBALSA_INFORMATION_ERROR, _("Could not save a text part: %s"),
                          err ? err->message : "Unknown error");
        g_clear_error(&err);
    } else {
        mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
        if (!create_import_keys_widget(GTK_BOX(mw), body_buf, &err)) {
            balsa_information(LIBBALSA_INFORMATION_ERROR, _("Could not process GnuPG keys: %s"),
                              err ? err->message : "Unknown error");
            g_clear_error(&err);
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
	gpgme_key_t key;
	gchar *infostr;
    GtkWidget *expander;
    GtkWidget *vbox, *label;
    GtkWidget *signature_widget;
    gchar **lines;

    if (!mime_body->sig_info ||
    	g_mime_gpgme_sigstat_status(mime_body->sig_info) == GPG_ERR_NOT_SIGNED)
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
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    key = g_mime_gpgme_sigstat_key(mime_body->sig_info);
    if (key != NULL) {
		GtkWidget *key_widget;

		key_widget = libbalsa_gpgme_key(key, g_mime_gpgme_sigstat_fingerprint(mime_body->sig_info), 0U, FALSE);
		gtk_box_pack_start(GTK_BOX(vbox), key_widget, FALSE, FALSE, 0);
    }
    if (g_mime_gpgme_sigstat_protocol(mime_body->sig_info) == GPGME_PROTOCOL_OpenPGP) {
    	GtkWidget *hbox;
        GtkWidget *button;

        hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_EXPAND);
        gtk_box_set_spacing(GTK_BOX(hbox), BMW_HBOX_SPACE);

        gtk_widget_set_vexpand(hbox, TRUE);
        gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
        gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
        if (g_mime_gpgme_sigstat_status(mime_body->sig_info) == GPG_ERR_NO_PUBKEY) {
#ifdef ENABLE_AUTOCRYPT
        	GBytes *autocrypt_key;

        	autocrypt_key = autocrypt_get_key(g_mime_gpgme_sigstat_fingerprint(mime_body->sig_info), NULL);
        	if (autocrypt_key != NULL) {
        		button = gtk_button_new_with_mnemonic(_("_Import Autocrypt key"));
        		g_object_set_data_full(G_OBJECT(button), "autocrypt_key", autocrypt_key, (GDestroyNotify) g_bytes_unref);
        		g_signal_connect(button, "clicked", G_CALLBACK(on_key_import_button), NULL);
                        gtk_widget_set_hexpand(button, TRUE);
                        gtk_widget_set_halign(button, GTK_ALIGN_FILL);
        		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
        	}
#endif
            button = gtk_button_new_with_mnemonic(_("_Search key server for this key"));
        } else {
            button = gtk_button_new_with_mnemonic(_("_Search key server for updates of this key"));
        }
        g_signal_connect(button, "clicked",
                         G_CALLBACK(on_gpg_key_button),
                         (gpointer) g_mime_gpgme_sigstat_fingerprint(mime_body->sig_info));

        gtk_widget_set_hexpand(button, TRUE);
        gtk_widget_set_halign(button, GTK_ALIGN_FILL);
        gtk_container_add(GTK_CONTAINER(hbox), button);
    }

    /* Hack alert: if we omit the box below and use the expander as signature widget
     * directly, setting the container border width of the container = the expander
     * causes its sensitive area to shrink to an almost unusable narrow line above
     * the label... */
    signature_widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    expander = gtk_expander_new(lines[0]);
    label = gtk_expander_get_label_widget(GTK_EXPANDER(expander));
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_container_add(GTK_CONTAINER(signature_widget), expander);
    gtk_container_add(GTK_CONTAINER(expander), vbox);

    gtk_container_set_border_width(GTK_CONTAINER(signature_widget), BMW_CONTAINER_BORDER);

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
    GtkWidget *icon;

    frame = gtk_frame_new(NULL);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_VBOX_SPACE);
    gtk_container_add(GTK_CONTAINER(frame), vbox);
    icon_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BMW_VBOX_SPACE);
    if (was_encrypted) {
    	icon = gtk_image_new_from_icon_name(balsa_icon_id(BALSA_PIXMAP_ENCR), GTK_ICON_SIZE_MENU);
    	gtk_widget_set_tooltip_text(icon, _("decrypted"));
        gtk_box_pack_start(GTK_BOX(icon_box), icon, FALSE, FALSE, 0);
    }
    if (!no_signature) {
    	LibBalsaMsgProtectState sig_state = libbalsa_message_body_protect_state(mime_body);
    	const gchar *icon_name = balsa_mime_widget_signature_icon_name(sig_state);

    	if (icon_name == NULL) {
    		icon_name = BALSA_PIXMAP_SIGN;
    	}
		icon = gtk_image_new_from_icon_name(balsa_icon_id(icon_name), GTK_ICON_SIZE_MENU);
    	switch (sig_state) {
    	case LIBBALSA_MSG_PROTECT_SIGN_GOOD:
    		gtk_widget_set_tooltip_text(icon, _("trusted signature"));
    		break;
    	case LIBBALSA_MSG_PROTECT_SIGN_NOTRUST:
    		gtk_widget_set_tooltip_text(icon, _("low trust signature"));
    		break;
    	case LIBBALSA_MSG_PROTECT_SIGN_BAD:
    		gtk_widget_set_tooltip_text(icon, _("bad signature"));
    		break;
    	default:
    		gtk_widget_set_tooltip_text(icon, _("unknown signature status"));
    	}
        gtk_box_pack_start(GTK_BOX(icon_box), icon, FALSE, FALSE, 0);
    }
    gtk_frame_set_label_widget(GTK_FRAME(frame), icon_box);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), BMW_MESSAGE_PADDING);

    gtk_box_pack_start(GTK_BOX(vbox), child, FALSE, FALSE, 0);

    if (signature) {
	gtk_box_pack_end(GTK_BOX(vbox), signature, FALSE, FALSE, 0);
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
	gboolean success = FALSE;
	GError *error = NULL;
	gchar *import_info = NULL;
	GtkWidget *dialog;

	ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, NULL, NULL, &error);
	if (ctx != NULL) {
		const gchar *keydata;

		keydata = g_object_get_data(G_OBJECT(button), "keydata");
		if (keydata != NULL) {
			success = libbalsa_gpgme_import_ascii_key(ctx, keydata, &import_info, &error);
		} else {
			GBytes *key_buf;

			key_buf = (GBytes *) g_object_get_data(G_OBJECT(button), "autocrypt_key");
			if (key_buf != NULL) {
				success = libbalsa_gpgme_import_bin_key(ctx, key_buf, &import_info, &error);
			}
		}
		gpgme_release(ctx);
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
			_("Error importing key data: %s"), (error != NULL) ? error->message : _("unknown error"));
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
				libbalsa_gpgme_list_keys(ctx, &keys, NULL, NULL, FALSE, FALSE, TRUE, error);

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
						gtk_box_pack_start(box, key_widget, FALSE, FALSE, 0);

						import_btn = gtk_button_new_with_label(_("Import key into the local key ring"));
						g_object_set_data_full(G_OBJECT(import_btn), "keydata", key_ascii, (GDestroyNotify) g_free);
						g_signal_connect(import_btn, "clicked", (GCallback) on_key_import_button, NULL);
						gtk_box_pack_start(box, import_btn, FALSE, FALSE, 0);

						if (item->next != NULL) {
                                                    GtkWidget *separator =
                                                        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                                                    gtk_widget_set_margin_top(separator,
                                                                              BMW_VBOX_SPACE);
                                                    gtk_widget_set_margin_bottom(separator,
                                                                                 BMW_VBOX_SPACE);
                                                    gtk_box_pack_start(box, separator, FALSE, FALSE, 0);
						}
					}
				}

				g_list_free_full(keys, (GDestroyNotify) gpgme_key_release);
			}

			libbalsa_delete_directory_contents(temp_dir);
			g_rmdir(temp_dir);
			g_free(temp_dir);
		}

		gpgme_release(ctx);
	}

	return success;
}
