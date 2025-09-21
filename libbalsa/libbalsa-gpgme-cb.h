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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LIBBALSA_GPGME_CB_H_
#define LIBBALSA_GPGME_CB_H_


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gpgme.h>
#include <glib.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS


typedef enum {
    LB_SELECT_PRIVATE_KEY = 1,
    LB_SELECT_PUBLIC_KEY_USER,
    LB_SELECT_PUBLIC_KEY_ANY
} lb_key_sel_md_t;


gpgme_key_t lb_gpgme_select_key(const gchar * user_name, lb_key_sel_md_t mode,
				GList * keys, gpgme_protocol_t protocol,
				GtkWindow * parent);
gboolean lb_gpgme_accept_low_trust_key(const gchar *user_name,
				       	   	   	   	   gpgme_key_t  key,
									   GtkWindow   *parent);


G_END_DECLS


#endif				/* LIBBALSA_GPGME_CB_H_ */
