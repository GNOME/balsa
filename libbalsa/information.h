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

#ifndef __LIBBALSA_INFORMATION_H__
#define __LIBBALSA_INFORMATION_H__


#include <gio/gio.h>


enum _LibBalsaInformationType {
    LIBBALSA_INFORMATION_MESSAGE,
    LIBBALSA_INFORMATION_WARNING,
    LIBBALSA_INFORMATION_ERROR,
    LIBBALSA_INFORMATION_DEBUG,
    LIBBALSA_INFORMATION_FATAL
};

typedef enum _LibBalsaInformationType LibBalsaInformationType;


/** \brief Initialise Balsa's desktop notifications
 *
 * \param[in] application the Balsa GApplication
 * \param[in] title the notification title
 * \param[in] notification_id the notification ID prefix (a unique serial number is added to each notification)
 *
 * Note that this function also loads the list of Balsa's notification identifiers which shall be hidden.  No other function from
 * this module shall be called before calling this function.
 */
void libbalsa_information_init(GApplication *application,
							   const gchar  *title,
							   const gchar  *notification_id);

/** \brief Display a desktop notification
 *
 * \param[in] type notification type, select the icon
 * \param[in] fmt printf()-like format string
 * \param[in] ... additional parameters according to the format string
 */
void libbalsa_information(LibBalsaInformationType  type,
						  const gchar             *fmt,
						  ...)
	G_GNUC_PRINTF(2, 3);

/** \brief Display a desktop notification which the user may hide
 *
 * \param[in] type notification type, select the icon
 * \param[in] hide_id Balsa's notification identifier, shall not contain the ";" character
 * \param[in] fmt printf()-like format string
 * \param[in] ... additional parameters according to the format string
 *
 * If the passed notification identifier is in the list of identifiers which shall be hidden the function is a no-op.  Otherwise,
 * it behaves as libbalsa_information(), but adds a button "do not show again" to the notification.  If the user clicks the button,
 * the passed notification identifier is added to an internal exclusion list which is saved in the config when
 * libbalsa_information_save_cfg() is called.
 */
void libbalsa_information_may_hide(LibBalsaInformationType  type,
								   const gchar             *hide_id,
								   const gchar             *fmt,
								   ...)
	G_GNUC_PRINTF(3, 4);

/** \brief Save the list of Balsa's notification identifiers which shall be hidden
 *
 * The list is stored in the main config file in the section "Notifications".
 */
void libbalsa_information_save_cfg(void);

/** \brief Shut down Balsa's desktop notifications
 *
 * If any notifications have been transmitted, the very last one (i.e. Balsa's shutdown message) is withdrawn.
 *
 * \note When using org.freedesktop.Notifications as backend for desktop notifications, a notification can be withdrawn only if the
 *		 backend reported its internal id back to Balsa.  If the time between sending the notification by calling
 *		 libbalsa_information() or libbalsa_information_may_hide() and the call to this function is too short, the notification is
 *		 hidden by the backend after a time-out or when the user clicks on it.
 */
void libbalsa_information_shutdown(void);


#endif
