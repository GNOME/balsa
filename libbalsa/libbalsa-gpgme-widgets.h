/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * Balsa E-Mail Client
 *
 * gpgme key related widgets and display functions
 * Copyright (C) 2017 Albrecht Dreß <albrecht.dress@arcor.de>
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

#ifndef LIBBALSA_GPGME_WIDGETS_H_
#define LIBBALSA_GPGME_WIDGETS_H_


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gpgme.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS


/* defines the capabilities a subkey can have */
typedef enum {
	GPG_SUBKEY_CAP_SIGN	= (1U << 0),
	GPG_SUBKEY_CAP_ENCRYPT = (1U << 1),
	GPG_SUBKEY_CAP_CERTIFY = (1U << 2),
	GPG_SUBKEY_CAP_AUTH = (1U << 3)
} lb_gpg_subkey_capa_t;

#define GPG_SUBKEY_CAP_ALL (GPG_SUBKEY_CAP_SIGN + GPG_SUBKEY_CAP_ENCRYPT + GPG_SUBKEY_CAP_CERTIFY + GPG_SUBKEY_CAP_AUTH)


/** \brief Create a key widget
 *
 * \param key GnuPG or S/MIME key
 * \param fingerprint fingerprint of the subkey which shall be displayed, NULL to display all subkeys with certain capabilities
 * \param subkey_capa mask of capabilities for which subkeys shall be included, used only if \em fingerprint is NULL
 * \param expanded whether the expanders shall be initially expanded
 * \return a new widget containing details about the key
 *
 * Create a widget containing most information about the key, including all UID's, all requested subkeys and the issuer (S/MIME
 * only).  Note that no information about the OpenPGP signatures of the UID's are included, as it is expensive to retrieve all
 * signatures of a key.
 *
 * If a S/MIME issuer certificate is available, the widget includes a button for displaying the certificate chain in a modal
 * dialogue.
 */
GtkWidget *libbalsa_gpgme_key(const gpgme_key_t     key,
							  const gchar          *fingerprint,
							  lb_gpg_subkey_capa_t  subkey_capa,
							  gboolean              expanded)
	G_GNUC_WARN_UNUSED_RESULT;


/** \brief Key details as human-readable string
 *
 * \param key GnuPG or S/MIME key
 * \param fingerprint fingerprint of the subkey which shall be printed, <i>must not</i> be NULL
 * \return a newly allocated string containing the key details
 *
 * Create a human-readable multiline string containing the key details, including the details of the subkey identified by the
 * passed fingerprint.  The string is basically a printable version of libbalsa_gpgme_key() for the same key and fingerprint, with
 * the expanders opened.
 */
gchar *libbalsa_gpgme_key_to_gchar(gpgme_key_t  key,
							  	   const gchar *fingerprint)
	G_GNUC_WARN_UNUSED_RESULT;


/** \brief Create a key message dialogue
 *
 * \param parent transient parent window, may be NULL
 * \param buttons set of buttons to use (currently only GTK_BUTTONS_CLOSE and GTK_BUTTONS_YES_NO are implemented)
 * \param key key data which shall be displayed
 * \param subkey_capa mask of capabilities for which subkeys shall be included
 * \param message1 primary message, printed centred in bold and a little larger, may be NULL to omit
 * \param message2 secondary message, printed start-aligned id normal font, may be NULL to omit
 * \return the new dialogue
 *
 * Create a new dialogue, similar to e.g. gtk_message_dialog_new().
 */
GtkWidget *libbalsa_key_dialog(GtkWindow            *parent,
							   GtkButtonsType        buttons,
							   gpgme_key_t           key,
							   lb_gpg_subkey_capa_t  subkey_capa,
							   const gchar          *message1,
							   const gchar          *message2)
	G_GNUC_WARN_UNUSED_RESULT;


/** \brief Create a key list message dialogue
 *
 * \param parent transient parent window, may be NULL
 * \param buttons set of buttons to use (currently only GTK_BUTTONS_CLOSE and GTK_BUTTONS_YES_NO are implemented)
 * \param key_list list of gpgme_key_t keys which shall be displayed
 * \param subkey_capa mask of capabilities for which subkeys shall be included
 * \param message1 primary message, printed centred in bold and a little larger, may be NULL to omit
 * \param message2 secondary message, printed start-aligned id normal font, may be NULL to omit
 * \return the new dialogue
 *
 * Create a new dialogue, similar to e.g. gtk_message_dialog_new().
 */
GtkWidget *libbalsa_key_list_dialog(GtkWindow            *parent,
									GtkButtonsType        buttons,
									GList                *key_list,
									lb_gpg_subkey_capa_t  subkey_capa,
									const gchar          *message1,
									const gchar          *message2)
	G_GNUC_WARN_UNUSED_RESULT;


G_END_DECLS


#endif /* LIBBALSA_GPGME_WIDGETS_H_ */
