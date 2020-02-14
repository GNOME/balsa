/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2019 Stuart Parmenter and others
 * Written by (C) Albrecht Dreß <albrecht.dress@arcor.de> 2007
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

#ifndef __BALSA_PRINT_OBJECT_HEADER_H__
#define __BALSA_PRINT_OBJECT_HEADER_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "balsa-print-object.h"

G_BEGIN_DECLS

#define BALSA_TYPE_PRINT_OBJECT_HEADER	balsa_print_object_header_get_type()
G_DECLARE_FINAL_TYPE(BalsaPrintObjectHeader, balsa_print_object_header, BALSA_PRINT, OBJECT_HEADER, BalsaPrintObject)


GList *balsa_print_object_header_from_message(GList           *list,
											  GtkPrintContext *context,
											  LibBalsaMessage *message,
											  const gchar     *subject,
											  BalsaPrintSetup *psetup)
	G_GNUC_WARN_UNUSED_RESULT;
GList *balsa_print_object_header_from_body(GList               *list,
										   GtkPrintContext     *context,
										   LibBalsaMessageBody *body,
										   BalsaPrintSetup     *psetup)
	G_GNUC_WARN_UNUSED_RESULT;
GList *balsa_print_object_header_crypto(GList               *list,
										GtkPrintContext     *context,
										LibBalsaMessageBody *body,
										BalsaPrintSetup     *psetup)
	G_GNUC_WARN_UNUSED_RESULT;


G_END_DECLS

#endif				/* __BALSA_PRINT_OBJECT_HEADER_H__ */
