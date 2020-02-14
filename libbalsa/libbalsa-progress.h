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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LIBBALSA_PROGRESS_H_
#define LIBBALSA_PROGRESS_H_


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>


/** \brief Progress dialogue
 *
 * Structure containing the actual progress dialogue widget and a mutex for protecting concurrent accesses.  As to initialise a
 * variable of this type, put it either in static memory, or set the \ref ProgressDialog::dialog to NULL and initialise \ref
 * ProgressDialog::mutex.
 */
typedef struct {
	GtkWidget *dialog;
	GMutex     mutex;
} ProgressDialog;


/** \brief Ensure that a progress dialogue and progress section exists
 *
 * \param progress_dialog properly initialised progress dialogue
 * \param dialog_title dialogue title, used only if a new dialogue is created
 * \param parent parent window
 * \param progress_id progress identifier, also used as section title, \em must be unique
 *
 * If the passed progress dialogue \ref ProgressDialog::dialog is NULL, a new dialogue is created.
 *
 * If no progress section with the passed id exists, it is appended.  An already existing section is revealed if necessary.
 *
 * \note This function may be called from a thread.  In this case, the function will block in the thread until the "real" work has
 *       been done in the main thread.
 */
void libbalsa_progress_dialog_ensure(ProgressDialog *progress_dialog,
									 const gchar    *dialog_title,
									 GtkWindow      *parent,
									 const gchar    *progress_id);


/** \brief Progress dialogue update
 *
 * \param progress_dialog progress dialogue, initialised by libbalsa_progress_dialog_ensure()
 * \param progress_id progress identifier passed to libbalsa_progress_dialog_ensure()
 * \param finished indicates whether the progress element shall be removed from the dialogue.  When no progress elements are left,
 *                 the dialogue is destroyed
 * \param fraction progress bar value between 0.0 and 1.0, or INF to switch to activity mode, or NAN to keep the current value
 * \param message printf-like format string which shall be printed above the progress bar, or NULL to keep the current message
 * \param ... additional arguments for the message format string
 *
 * Update the information of the passed progress dialogue.
 *
 * \note This function may be called from a thread.  In this case, the "real" work is done in an idle callback and may therefore
 *       be performed only after this function returns.
 */
void libbalsa_progress_dialog_update(ProgressDialog *progress_dialog,
									 const gchar    *progress_id,
									 gboolean        finished,
									 gdouble         fraction,
									 const gchar    *message,
									 ...)
	G_GNUC_PRINTF(5, 6);


#endif /* LIBBALSA_PROGRESS_H_ */
