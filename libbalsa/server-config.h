/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2018 Stuart Parmenter and others,
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

#ifndef LIBBALSA_SERVER_CONFIG_H_
#define LIBBALSA_SERVER_CONFIG_H_


#include <gtk/gtk.h>
#include "server.h"


#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif


#define LIBBALSA_TYPE_SERVER_CFG (libbalsa_server_cfg_get_type())

G_DECLARE_FINAL_TYPE(LibBalsaServerCfg,
                     libbalsa_server_cfg,
                     LIBBALSA,
                     SERVER_CFG,
                     GtkNotebook)


/** @brief Create a new server configuration widget
 * @param server server data, must not be NULL
 * @param name name, may be NULL
 * @return a newly allocated and populated server configuration notebook widget
 */
LibBalsaServerCfg *libbalsa_server_cfg_new(LibBalsaServer *server,
										   const gchar    *name)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Check if the server configuration is valid
 * @param server_cfg server configuration widget
 * @return TRUE if the configuration is valid, i.e. all mandatory fields are filled in
 */
gboolean libbalsa_server_cfg_valid(LibBalsaServerCfg *server_cfg);

/** @brief Add a custom widget to the server configuration
 * @param server_cfg server configuration widget
 * @param basic TRUE to append the widget to the @em Basic or FALSE to the @em Advanced page
 * @param label widget label, must not be NULL
 * @param widget widget to append, must not be NULL
 */
void libbalsa_server_cfg_add_item(LibBalsaServerCfg *server_cfg,
								  gboolean           basic,
								  const gchar       *label,
								  GtkWidget         *widget);

/** @brief Add custom widgets to the server configuration
 * @param server_cfg server configuration widget
 * @param basic TRUE to append the widget to the @em Basic or FALSE to the @em Advanced page
 * @param left widget in the left grid column, must not be NULL
 * @param right widget in the right grid column, may be NULL if the left widget shall span over both grid columns
 */
void libbalsa_server_cfg_add_row(LibBalsaServerCfg *server_cfg,
								 gboolean           basic,
								 GtkWidget         *left,
								 GtkWidget         *right);

/** @brief Add a custom check box to the server configuration
 * @param server_cfg server configuration widget
 * @param basic TRUE to append the widget to the @em Basic or FALSE to the @em Advanced page
 * @param label widget label, must not be NULL
 * @param initval initial check box value
 * @param callback callback function for the @em toggled signal, may be NULL
 * @param cb_data data to pass to the callback
 * @return a new GtkCheckButton
 */
GtkWidget *libbalsa_server_cfg_add_check(LibBalsaServerCfg *server_cfg,
							      	     gboolean           basic,
										 const gchar       *label,
										 gboolean 			initval,
										 GCallback			callback,
										 gpointer			cb_data)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Add a custom entry to the server configuration
 * @param server_cfg server configuration widget
 * @param basic TRUE to append the widget to the @em Basic or FALSE to the @em Advanced page
 * @param label widget label, must not be NULL
 * @param initval initial entry value, may be NULL
 * @param callback callback function for the @em changed signal, may be NULL
 * @param cb_data data to pass to the callback
 * @return a new GtkEntry
 */
GtkWidget *libbalsa_server_cfg_add_entry(LibBalsaServerCfg *server_cfg,
							      	     gboolean           basic,
										 const gchar       *label,
										 const gchar       *initval,
										 GCallback			callback,
										 gpointer			cb_data)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Get the name of the server configuration
 * @param server_cfg server configuration widget
 * @return the name entered in the <em>Descriptive Name</em> GtkEntry
 */
const gchar *libbalsa_server_cfg_get_name(LibBalsaServerCfg *server_cfg);

/** @brief Get the server configuration
 * @param server_cfg server configuration widget
 * @param server server data, must not be NULL
 */
void libbalsa_server_cfg_assign_server(LibBalsaServerCfg *server_cfg,
									   LibBalsaServer    *server);


#endif /* LIBBALSA_SERVER_CONFIG_H_ */
