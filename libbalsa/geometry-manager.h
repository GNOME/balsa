/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
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

#ifndef _GEOMETRY_MANAGER_H_
#define _GEOMETRY_MANAGER_H_

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif


#include <glib.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS


typedef struct {
	gint width;
	gint height;
	gboolean maximized;
} geometry_t;


/** \brief Add a dialog to the geometry manager
 *
 * \param key dialog identifier, must not be NULL or empty
 * \param width default width
 * \param height default height
 * \param maximized default maximized state
 *
 * This function initialises the geometry management for a dialog with the passed key.  It shall be called in the "Geometry" group
 * when the configuration is read.  The config values are <key>Width, <key>Height and <key>Maximized, using the passed default
 * values.
 *
 * It is an error to call the function more than once for the same key.
 */
void geometry_manager_init(const gchar *key,
						   gint         width,
						   gint         height,
						   gboolean     maximized);

/** \brief Get the geometry of a dialog
 *
 * \param key dialog identifier
 * \return the currently active dialog extents, or NULL on error
 */
const geometry_t *geometry_manager_get(const gchar *key);

/** \brief Activate geometry management for a window instance
 *
 * \param window window for which geometry management shall be used
 * \param key dialog identifier
 *
 * If the passed key is known, make the passed window resizable, set the default extents to the stored values, and connect signals
 * to internally track size changes.
 */
void geometry_manager_attach(GtkWindow   *window,
							 const gchar *key);

/** \brief Save the geometry manager state
 *
 * This function shall be called when the configuration file is saved, and the "Geometry" group is selected.  It simply writes all
 * collected values with appropriate keys.
 */
void geometry_manager_save(void);


G_END_DECLS


#endif /* GEOMETRY_MANAGER_H_ */
