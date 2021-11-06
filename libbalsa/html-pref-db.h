/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2021 Stuart Parmenter and others,
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

#ifndef HTML_PREF_DB_H_
#define HTML_PREF_DB_H_

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "libbalsa.h"

#ifdef HAVE_HTML_WIDGET

/* SQLite3 database to manage HTML preferences */


/** \brief Check if HTML is preferred for messages from a sender
 *
 * \param from From: address list, may be NULL or empty
 * \return TRUE if HTML is preferred
 */
gboolean libbalsa_html_get_prefer_html(InternetAddressList *from);

/** \brief Check if external content in HTML messages from a sender shall be loaded automatically
 *
 * \param from From: address list, may be NULL or empty
 * \return TRUE if external content in HTML messages shall be loaded without confirmation
 */
gboolean libbalsa_html_get_load_content(InternetAddressList *from);

/** \brief Remember if HTML is preferred for messages from a sender
 *
 * \param from From: address list, must not be NULL
 * \param state TRUE if HTML is preferred
 * \note The function is a no-op if the InternetAddressList does not contain an InternetAddressMailbox as 1st element.
 */
void libbalsa_html_prefer_set_prefer_html(InternetAddressList *from,
                                          gboolean             state);

/** \brief Remember if external content in HTML messages from a sender shall be loaded automatically
 *
 * \param from From: address list, must not be NULL
 * \param state TRUE if external content in HTML messages shall be loaded without confirmation
 * \note The function is a no-op if the InternetAddressList does not contain an InternetAddressMailbox as 1st element.
 */
void libbalsa_html_prefer_set_load_content(InternetAddressList *from,
                                          gboolean             state);

/** \brief Show the dialogue for managing the HTML preferences database
 *
 * \param parent transient parent window
 */
void libbalsa_html_pref_dialog_run(GtkWindow *parent);


#endif		/* HAVE_HTML_WIDGET */

#endif		/* HTML_PREF_DB_H_ */
