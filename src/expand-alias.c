/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#include "config.h"

#include "libbalsa.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <ctype.h>

#include <sys/stat.h>		/* for check_if_regular_file() */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "misc.h"
#include "mime.h"
#include "sendmsg-window.h"
#include "address-book.h"
#include "address-entry.h"
#include "main.h"
#include "expand-alias.h"

#define CASE_INSENSITIVE_NAME

gchar *make_rfc822(gchar *, gchar *);


/*
 * FIXME:  Should this come here?
 * 
 *         By putting it here, we slow it down, because we need to scan
 *         the match for ',' every time.
 *
 *         By putting it in the address books, its faster, but the user
 *         must type '"' to match relevant addresses...
 */
gchar *
make_rfc822(gchar *full_name, gchar *address)
{
    gboolean found_comma;
    gchar *new_str;
    gint i;

    found_comma = FALSE;
    for (i=0; full_name[i]; i++)
	if (full_name[i] == ',') found_comma = TRUE;
    if (found_comma) {
	new_str = g_strdup_printf("\042%s\042 <%s>", full_name, address);
	g_message("make_rfc822(): New str [%s]", new_str);
    } else
	new_str = g_strdup_printf("%s <%s>", full_name, address);
    return new_str;
}
	
/*
 * expand_alias_find_match()
 *
 * Takes an emailData structure, and scans the relevent Balsa
 * addressbooks for matches.  It takes the most relevant match,
 * and enters it into the data structure.
 *
 * INPUT: emailData *data
 * OUTPUT: void
 * MODIFIES: data
 * 
 * FIXME: This function is too long and evil.
 *
 * CAVEAT: Its here because it depends on balsa_app.
 */
void
expand_alias_find_match(emailData *addy)
{
    gchar *prefix = NULL;	/* the longest common string. */
    GList *match = NULL;	/* A list of matches.         */
    GList *search = NULL;	/* Used to search the list.   */
    gchar *output = NULL;	/* We return this.            */
    LibBalsaAddress *addr = NULL;	/* Process the list data.     */
    gint i;			/* A counter for the tabs.    */
    GList *ab_list;             /* To iterate address books   */
    GList *partial_res = NULL;  /* The result froma single address book */
    gchar *partial_prefix;
    gchar *str;
    gchar *input;
    gint tab;

    input = addy->user;
    tab = addy->tabs;
    g_free(addy->match);
    addy->match = NULL;

    if (strlen(input) > 0) {

	str = g_strdup(input);
#ifdef CASE_INSENSITIVE_NAME
	g_strup(str);
#endif

	/*
	 * Look at all addressbooks for a match.
	 */
	ab_list = balsa_app.address_book_list;
	while(ab_list) {
	    if ( !LIBBALSA_ADDRESS_BOOK(ab_list->data)->expand_aliases ) {
		ab_list = g_list_next(ab_list);
		continue;
	    }

	    partial_res = libbalsa_address_book_alias_complete
	        (LIBBALSA_ADDRESS_BOOK(ab_list->data), str, &partial_prefix);
	    
	    if ( partial_res != NULL ) {
		if ( match != NULL )
		    match = g_list_concat(match, partial_res);
		else 
		    match = partial_res;
		
		if ( prefix == NULL ) {
		    prefix = partial_prefix;
		} else {
		    gchar *new_pfix;
		    gint len = 0;

		    /* 
		     * We have to find the longest common prefix of all options
		     * Tedious.
		     */
		    if ( strlen(partial_prefix) < strlen(prefix) )
			new_pfix = g_strdup(prefix);
		    else
			new_pfix = g_strdup(partial_prefix);

		    while( TRUE ) {
			if (*(prefix+len) == 0 || *(partial_prefix+len) == 0) {
			    *(new_pfix+len) = '\0';
			    break;
			} else if ( *(prefix+len) != *(partial_prefix+len) ) {
			    *(new_pfix+len) = '\0';
			    break;
			} else {
			    *(new_pfix+len) = *(prefix+len);
			    len++;
			}
		    }
		    g_free(prefix); g_free(partial_prefix);
		    prefix = new_pfix;
		}
	    }
	    
	    ab_list = g_list_next(ab_list);
	}
	g_free(str);

	/*
	 * Now look through all the matches the above code generated, and
	 * find the one we want.
	 */
	if (match) {
	    i = tab;
	    if ((i == 0) && (strlen(prefix) > strlen(input))) {
		addr = LIBBALSA_ADDRESS(match->data);

	    } else {
		for (search = match; i > 0; i--) {
		    search = g_list_next(search);
		    if (!search) {
			addy->tabs = i = 0;
			search = match;
		    }
		}
		addr = LIBBALSA_ADDRESS(search->data);
	    }
	    output = make_rfc822(addr->full_name,
				 (gchar *) addr->address_list->data);
	    g_message("expand_alias_find_match(): Found [%s]", addr->full_name);
	    g_list_foreach(match, (GFunc)gtk_object_unref, NULL);

	/*
	 * And now we handle the case of "No matches found."
	 */
	} else {
	    output = NULL;
	}

	if (prefix) g_free(prefix);
	prefix = NULL;
    } else {
	output = g_strdup("");
    }
    addy->match = output;
    if (addy->match)
	g_message("expand_alias_find_match(): Setting to [%s]", addy->match);
}


