/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */
/*

 * filter-file.c
 *
 * File functions of the mail filter portion of balsa
 *
 * filter_load()
 * filter_save()
 */

#include "config.h"

#include <sys/types.h>

#include "filter.h"
#include "filter-private.h"
#include "filter-funcs.h"
#include <gtk/gtk.h>
#include "misc.h"


/*
 * filter_load()
 *
 * Loads the filters from the definition file.
 *
 * Arguments:
 *    gchar *filter_file - the name of the filter config file
 *
 * Returns:
 *    int - 0 for success, -1 for error.  Sets filter_errno on error.
 */
gint 
filter_load (GList * filter_list, gchar * filter_file)
{
  FILE *fp;
  gchar *buf;
  size_t len;

  if ((!filter_file) || (filter_file[0] == '\0'))
    {
      filter_errno = FILTER_ENOFILE;
      return (-FILTER_ENOFILE);
    }

  /* here we'll delete an existing filter list, if there is one */

  if (!(fp = fopen (filter_file, "r")))
    {
      gchar filter_file_error[1024];

      g_snprintf (filter_file_error,
                  1024,
                  "Unable to load filter file %s",
                  filter_file);
      perror (filter_file_error);
      filter_errno = FILTER_ENOREAD;
      return (-FILTER_ENOREAD);
    }

  len = libbalsa_readfile (fp, &buf);
  fclose (fp);

  return (0);
}


/*
 * filter_save()
 *
 * Saves the filters into the defninition file
 *
 * Arguments:
 *    GList *filter_list - the list of filters to save
 *    gchar *filter_file - the file to save them to
 *
 * Returns:
 *    gint - 0 for success, -1 for error.  Sets filter_errno on error.
 */
gint 
filter_save (GList * filter_list, gchar * filter_file)
{
	return 0;
}
