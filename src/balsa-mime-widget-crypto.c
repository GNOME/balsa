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
#include "balsa-mime-widget-crypto.h"

#ifdef HAVE_GPGME
#include "balsa-app.h"
#include "balsa-icons.h"
#include <glib/gi18n.h>
#include "balsa-mime-widget.h"


#ifdef HAVE_GPG
static void on_gpg_key_button(GtkButton * button, const gchar * fingerprint);
#endif


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
    mw->widget = balsa_mime_widget_signature_widget(mime_body, content_type);
    
    return mw;
}

GtkWidget *
balsa_mime_widget_signature_widget(LibBalsaMessageBody * mime_body,
				   const gchar * content_type)
{
    gchar *infostr;
    GtkWidget *vbox, *label;

    if (!mime_body->sig_info ||
	mime_body->sig_info->status == GPG_ERR_NOT_SIGNED)
	return NULL;
				   
    infostr =
        libbalsa_signature_info_to_gchar(mime_body->sig_info,
                                         balsa_app.date_string);
    if (g_ascii_strcasecmp(content_type, "application/pgp-signature") &&
	g_ascii_strcasecmp(content_type, "application/pkcs7-signature") &&
	g_ascii_strcasecmp(content_type, "application/x-pkcs7-signature")) {
	gchar * labelstr = 
	    g_strdup_printf(_("This is an inline %s signed %s message part:\n%s"),
			    mime_body->sig_info->protocol == GPGME_PROTOCOL_OpenPGP ?
			    _("OpenPGP") : _("S/MIME"),
			    content_type, infostr);
	g_free(infostr);
	infostr = labelstr;
    }
    
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_VBOX_SPACE);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), BMW_CONTAINER_BORDER);
    label = gtk_label_new(infostr);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    g_free(infostr);
#ifdef HAVE_GPG
    if (mime_body->sig_info->protocol == GPGME_PROTOCOL_OpenPGP) {
        GtkWidget *button;

        if (mime_body->sig_info->status == GPG_ERR_NO_PUBKEY) {
            button = gtk_button_new_with_mnemonic(_("_Run GnuPG to import this key"));
            g_object_set_data(G_OBJECT(button), "gpg-keyserver-op",
                              GINT_TO_POINTER(GPG_KEYSERVER_IMPORT));
        } else {
            button = gtk_button_new_with_mnemonic(_("_Run GnuPG to check for an update of this key"));
            g_object_set_data(G_OBJECT(button), "gpg-keyserver-op",
                              GINT_TO_POINTER(GPG_KEYSERVER_UPDATE));
        }
        g_signal_connect(G_OBJECT(button), "clicked",
                         G_CALLBACK(on_gpg_key_button),
                         (gpointer)mime_body->sig_info->fingerprint);
        gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
    }
#endif /* HAVE_GPG */

    return vbox;
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
                           gtk_image_new_from_icon_name(BALSA_PIXMAP_ENCR,
                                                        GTK_ICON_SIZE_MENU),
                           FALSE, FALSE, 0);
    if (!no_signature) {
	const gchar * icon_name =
	    balsa_mime_widget_signature_icon_name(libbalsa_message_body_protect_state(mime_body));
	if (!icon_name)
	    icon_name = BALSA_PIXMAP_SIGN;
        gtk_box_pack_start(GTK_BOX(icon_box),
                           gtk_image_new_from_icon_name(icon_name,
                                                        GTK_ICON_SIZE_MENU),
                           FALSE, FALSE, 0);
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


#ifdef HAVE_GPG
/*
 * We need gnupg to retreive a key from a key server...
 */

/* Callback: run gpg to import a public key */
static void
on_gpg_key_button(GtkButton * button, const gchar * fingerprint)
{
    gpg_keyserver_action_t action =
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "gpg-keyserver-op"));

    gpg_keyserver_op(fingerprint, action,
                     balsa_get_parent_window(GTK_WIDGET(button)));
}
#endif /* HAVE_GPG */

#endif  /* HAVE_GPGME */
