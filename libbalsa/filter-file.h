/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
/*

 * filter-file.h
 *
 * File functions to save filters with the gnome_config scheme
 */

#ifndef __FILTER_FILE_H__
#define __FILTER_FILE_H__

#include "filter.h"

#define FILTER_SECTION_PREFIX "filter-"
#define MAILBOX_FILTERS_KEY "MailboxFilters"
#define MAILBOX_FILTERS_WHEN_KEY "MailboxFilters-When"

/* Load conditions list using filter_section_name as prefix to find sections */

void libbalsa_conditions_new_from_config(gchar* prefix,
                                         gchar * filter_section_name,
                                         LibBalsaFilter* fil);

/* Save conditions list using filter_section_name as prefix to create
 * sections. */

void libbalsa_clean_condition_sections(const gchar * prefix,
				       const gchar * filter_section_name);

void libbalsa_conditions_save_config(GSList * conds,const gchar * prefix,
                                     const gchar * filter_section_name);

void libbalsa_filter_save_config(LibBalsaFilter * f);

/* libbalsa_filter_new_from_config can position filter_errno on error */
LibBalsaFilter* libbalsa_filter_new_from_config(void);

/* Loads the filters associated to the mailbox */
void libbalsa_mailbox_filters_load_config(LibBalsaMailbox * mbox);

/* Saves the filters associated to the mailbox */
void libbalsa_mailbox_filters_save_config(LibBalsaMailbox * mbox);

#endif  /* __FILTER_FILE_H__ */
