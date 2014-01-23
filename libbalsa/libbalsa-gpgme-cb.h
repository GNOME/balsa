/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gpgme standard callback functions for balsa
 * Copyright (C) 2011 Albrecht Dreß <albrecht.dress@arcor.de>
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

#ifndef LIBBALSA_GPGME_CB_H_
#define LIBBALSA_GPGME_CB_H_


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gpgme.h>
#include <glib.h>
#include <gtk/gtk.h>


#ifdef __cplusplus
extern "C" {
#ifdef MAKE_EMACS_HAPPY
}
#endif
#endif				/* __cplusplus */


gpgme_error_t lb_gpgme_passphrase(void *hook, const gchar * uid_hint,
				  const gchar * passphrase_info,
				  int prev_was_bad, int fd);
gpgme_key_t lb_gpgme_select_key(const gchar * user_name, gboolean secret,
				GList * keys, gpgme_protocol_t protocol,
				GtkWindow * parent);
gboolean lb_gpgme_accept_low_trust_key(const gchar * user_name,
				       const gpgme_user_id_t user_id,
				       GtkWindow * parent);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* LIBBALSA_GPGME_CB_H_ */
