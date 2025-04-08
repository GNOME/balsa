/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2025 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 * Author: Copyright (C) Albrecht Dreß <albrecht.dress@posteo.de> 2025
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
 *
 * This is a helper module which checks if all configuration and state
 * files are stored in the XDG-compliant folders $XDG_CONFIG_HOME and
 * $XDG_STATE_HOME, or still in $HOME/.balsa.  In the latter case,
 * migrate everything to the XDG folders.
 *
 * See https://gitlab.gnome.org/GNOME/balsa/-/issues/95 and
 * https://specifications.freedesktop.org/basedir-spec/latest/ for further
 * details.
 */

#ifndef XDG_FOLDERS_H_
#define XDG_FOLDERS_H_


#include <glib.h>


/** \brief Check if Balsa's files are stored in XDG-compliant folders
 *
 * \return TRUE iff Balsa's files are stored in XDG-compliant folders, if the migration was successful, or if Balsa is launched for
 *         the first time, FALSE if manual user intervention is required.
 *
 * The function can return FALSE iff a migration was attempted, but failed because either copying any files from the old to the new
 * location did not succeed, or if adjusting paths in the main config file failed.  In this very unlikely case, the newly created
 * XDG folders are erased, and manual user intervention is required.  The application shall be terminated in this case.
 *
 * A dialog is shown if a migration was attempted, both if it was successful or failed.
 */
gboolean xdg_config_check(void);


#endif /* SRC_XDG_FOLDERS_H_ */
