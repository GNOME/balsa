/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Stuart Parmenter and Jay Painter
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

#include <gnome.h>
#include <glib.h>

#include "libbalsa.h"

LibBalsaContact *
libbalsa_contact_new (void)
{
  LibBalsaContact *contact;

  contact = g_new (LibBalsaContact, 1);

  contact->card_name = NULL;
  contact->first_name = NULL;
  contact->last_name = NULL;
  contact->organization = NULL;
  contact->email_address = NULL;
  
  return contact;
}


void
libbalsa_contact_free (LibBalsaContact * contact)
{

  if (!contact)
    return;

  g_free(contact->card_name);
  g_free(contact->first_name);
  g_free(contact->last_name);  
  g_free(contact->organization);
  g_free(contact->email_address);
  
  g_free(contact);
}

void libbalsa_contact_list_free(GList * contact_list)
{
  GList *list;
  for (list = g_list_first (contact_list); list; list = g_list_next (list))
    if(list->data) libbalsa_contact_free (list->data);
  g_list_free (contact_list);
}

gint
libbalsa_contact_store(LibBalsaContact *contact, const gchar *fname)
{
    FILE *gc; 
    gchar string[256];
    gint in_vcard = FALSE;

    g_return_val_if_fail(fname, LIBBALSA_CONTACT_UNABLE_TO_OPEN_GNOMECARD_FILE);
    if(strlen(contact->card_name) == 0)
        return LIBBALSA_CONTACT_CARD_NAME_FIELD_EMPTY;

    gc = fopen(fname, "r+");
    
    if (!gc) 
        return LIBBALSA_CONTACT_UNABLE_TO_OPEN_GNOMECARD_FILE; 
            
    while (fgets(string, sizeof(string), gc)) 
    { 
        if ( g_strncasecmp(string, "BEGIN:VCARD", 11) == 0 ) {
            in_vcard = TRUE;
            continue;
        }
                
        if ( g_strncasecmp(string, "END:VCARD", 9) == 0 ) {
            in_vcard = FALSE;
            continue;
        }
        
        if (!in_vcard) continue;
        
        g_strchomp(string);
                
        if ( g_strncasecmp(string, "FN:", 3) == 0 )
        {
            gchar *id = g_strdup(string+3);
            if(g_strcasecmp(id, contact->card_name) == 0)
            {
                g_free(id);
                fclose(gc);
                return LIBBALSA_CONTACT_CARD_NAME_EXISTS;
            }
            g_free(id);
            continue;
        }
    }

    fprintf(gc, "\nBEGIN:VCARD\n");
    fprintf(gc, g_strdup_printf( "FN:%s\n", contact->card_name));

    if(strlen(contact->first_name) || strlen(contact->last_name))
        fprintf(gc, g_strdup_printf( "N:%s;%s\n", contact->last_name, contact->first_name));

    if(strlen(contact->organization))
        fprintf(gc, g_strdup_printf( "ORG:%s\n", contact->organization));
            
    if(strlen(contact->email_address))
        fprintf(gc, g_strdup_printf( "EMAIL;INTERNET:%s\n", contact->email_address));
            
    fprintf(gc, "END:VCARD\n");
    
    fclose(gc);
    return LIBBALSA_CONTACT_CARD_STORED_SUCCESSFULLY;
}
