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

#include <gnome.h>
#include <string.h>


#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "misc.h"
#include "mime.h"
#include "sendmsg-window.h"
#include "address-book.h"
#include "address-entry.h"
#include "expand-alias.h"

#define CASE_INSENSITIVE_NAME

/*
 * expand_alias_find_match()
 *
 * Takes an emailData structure, and scans the relevent Balsa
 * addressbooks for matches.  It takes the most relevant match,
 * and enters it into the data structure.
 *
 * INPUT: emailData *data
 *        fastp - look only in non-expensive address books.
 * OUTPUT: void
 * MODIFIES: data
 * 
 * FIXME: This function is too long and evil.
 *
 * CAVEAT: Its here because it depends on balsa_app.
 */
void
expand_alias_find_match(emailData *addy, gboolean fastp)
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
    LibBalsaAddressBook* ab;

    input = addy->user;
    tab = addy->tabs;
    g_free(addy->match);
    addy->match = NULL;

    if(*input == '\0') {
	addy->match = g_strdup("");
	return;
    }
    
#ifdef CASE_INSENSITIVE_NAME
    str = g_ascii_strup(input, -1);
#else
    str = g_strdup(input);
#endif
    
    /*
     * Look at all addressbooks for a match.
     */
    for(ab_list = balsa_app.address_book_list; ab_list; 
	ab_list = ab_list->next) {
	ab = LIBBALSA_ADDRESS_BOOK(ab_list->data);
	if ( !ab->expand_aliases || (fastp && ab->is_expensive) )
	    continue;
	
	partial_res = 
	    libbalsa_address_book_alias_complete(ab, str, &partial_prefix);
	
	if ( partial_res != NULL ) {
	    match = match ? g_list_concat(match, partial_res) : partial_res;
	    
	    if ( prefix == NULL ) {
		prefix = partial_prefix;
	    } else {
		gchar *new_pfix;
		gint len = 0;
		
		/* 
		 * We have to find the longest common prefix of all options
		 * Tedious.
		 */
		new_pfix = g_strdup(strlen(partial_prefix) < strlen(prefix) 
				    ? prefix : partial_prefix);
		
		while( TRUE ) {
		    if (prefix[len] == 0 || partial_prefix[len] == 0) {
			new_pfix[len] = '\0';
			break;
		    } else if ( prefix[len] != partial_prefix[len] ) {
			new_pfix[len] = '\0';
			break;
		    } else {
			new_pfix[len] = prefix[len];
			len++;
		    }
		}
		g_free(prefix); g_free(partial_prefix);
		prefix = new_pfix;
	    }
	}
    }
    g_free(str);

    /*
     * Now look through all the matches the above code generated, and
     * find the one we want.
     */
    if(addy->address) {
        g_object_unref(addy->address);
        addy->address = NULL;
    }
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
        /* FIXME: in principle, we should pass -1 instead of 0 but the
         * group support is incomplete and these things has to be done
         * in one shot otherwise they do more harm than good. */
	output=libbalsa_address_to_gchar(addr, 0);
        addy->address = addr;
        g_object_ref(addy->address);

	if(balsa_app.debug)
            g_message("expand_alias_find_match(): Found [%s]", 
                      addr->full_name);
	g_list_foreach(match, (GFunc)g_object_unref, NULL);

	
	/*
	 * And now we handle the case of "No matches found."
	 */
    } 
    if (prefix) g_free(prefix);

    addy->match = output;
}

