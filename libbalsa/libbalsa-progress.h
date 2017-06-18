/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * Flexible progress dialogue for Balsa
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBBALSA_PROGRESS_H_
#define LIBBALSA_PROGRESS_H_


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>


typedef struct _LibbalsaProgressData LibbalsaProgressData;

/** \brief Progress update data
 *
 * The data which shall be passed to libbalsa_progress_dialog_update() in order to update a progress dialogue.
 */
struct _LibbalsaProgressData {
	GtkWidget *progress_dialog;		/**< Progress dialogue as created by libbalsa_progress_dialog_ensure(). */
	gchar     *progress_id;			/**< Progress identifier. */
	gboolean   finished;			/**< Indicates whether the progress element shall be removed from the dialogue.  When no
									 * progress elements are left, the dialogue is destroyed. */
	gchar     *message;				/**< Message which shall be printed above the progress bar, or NULL to keep the current
									 * message. */
	gdouble    fraction;			/**< Progress bar value, between 0.0 and 1.0, or NAN to keep the current value. */
};


/** \brief Ensure that a progress dialogue and progress section exists
 *
 * \param progress_dialog address of an existing progress dialogue, shall be filled with NULL to create a new one
 * \param dialog_title dialogue title, used only if a new dialogue is created
 * \param parent parent window
 * \param progress_id progress identifier, also used as section title, \em must be unique
 *
 * If the passed progress dialogue address is NULL, a new dialogue is created.
 *
 * If no progress section with the passed id exists, it is appended.  An already existing section is revealed if necessary.
 */
void libbalsa_progress_dialog_ensure(GtkWidget   **progress_dialog,
									 const gchar  *dialog_title,
									 GtkWindow    *parent,
									 const gchar  *progress_id);


/** \brief Progress dialogue update callback
 *
 * \param user_data progress update information, cast'ed to LibbalsaProgressData *
 * \return always FALSE
 *
 * This function shall be called with a pointer to an update information structure, typically from an idle callback.
 *
 * \note The function will free the passed data.
 */
gboolean libbalsa_progress_dialog_update(gpointer user_data);


#endif /* LIBBALSA_PROGRESS_H_ */
