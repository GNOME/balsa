/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others
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
#ifdef ENABLE_LDAP
#include <lber.h>
#include <ldap.h>
#include "balsa-app.h"
#include "ldap-addressbook.h"
#include "address-book.h"


#define LDAP_CACHE_TIMEOUT 300 /* Seconds */

static LDAP * ldap_initialise (const gchar *host);
static gchar * create_name (gchar *, gchar *);
static GList * ldap_add_from_server (GList *, LDAPMessage *);


/*
 * Static LDAP directory - kept for cache.
 */
static LDAP * directory = NULL;


/*
 * ldap_initialise(host)
 * 
 * Opens an ldap connection, and binds to the server.
 * Also enables LDAP caching.
 *
 * Returns:
 *   LDAP * on success
 *   NULL on failure
 */
static LDAP *
ldap_initialise (const gchar* host)
{
  LDAP *ld = NULL;
  int result;

  if (!host ||!*host) return NULL;
	
  ld = ldap_init ((gchar*)host, LDAP_PORT);
  if (!ld) {
    balsa_information (LIBBALSA_INFORMATION_WARNING,
		       _("Failed to initialise LDAP server.\n"
			 "Check that the servername is valid.") );
    perror ("ldap_init");
    return NULL;
  }
  result = ldap_simple_bind_s (ld, NULL, NULL);
  if (result != LDAP_SUCCESS) {
    balsa_information (LIBBALSA_INFORMATION_WARNING,
		       _("Failed to bind to server: %s\n"
			 "Check that the servername is valid."),
		       ldap_err2string (result));
    ldap_unbind_s (ld);
    return NULL;
  }
  ldap_enable_cache (ld, LDAP_CACHE_TIMEOUT, 0);
  return ld;
}


/*
 * ldap_quit()
 *
 * Call this before quitting balsa - it cleans up LDAP memory
 * structures.
 */
void
ldap_quit (void)
{
  if (directory) {
    ldap_destroy_cache (directory);
    ldap_unbind (directory);
    directory = NULL;
  }
}


/*
 * create_name()
 *
 * Creates a full name from a given first name and surname.
 * 
 * Returns:
 *   gchar * a full name
 *   NULL on failure (both first and last names invalid.
 */
static gchar *
create_name (gchar * first, gchar * last)
{
  if ((first == NULL) && (last == NULL))
    return NULL;
  else if (first == NULL)
    return g_strdup (last);
  else if (last == NULL)
    return g_strdup (first);
  else
    return g_strdup_printf ("%s %s", first, last);
}


/*
 * ldap_test(host, dn)
 *
 * Call this from a Preferences window - it will test whether the
 * current settings might work.
 *
 * Spits out balsa_information() messages to give feedback.
 *
 * Do NOT use ldap_initialise() because the user might change his
 * settings, and ldap_initialise() keeps a cache around.
 */
void
ldap_test (const gchar* host, const gchar *dn)
{
  LDAPMessage *result, *e;
  int rc, num_entries = 0;
  char *a;
  BerElement *ber = NULL;

  /*
   * Attempt to initialise LDAP.  Does not necessarily connect.
   */
  if (!directory) {
    directory = ldap_initialise (host);
    if (!directory) return;
  }
	
  /*
   * Attempt to search for e-mail addresses.  It returns success
   * or failure, but not all the matches.
   */
  if(balsa_app.debug) g_message ("Doing a search:");
  rc = ldap_search_s (directory, (gchar*)dn,
		    LDAP_SCOPE_SUBTREE, "(mail=*)", NULL, 0, &result);
  if (rc != LDAP_SUCCESS) {
    balsa_information (LIBBALSA_INFORMATION_WARNING,
		       _("Failed to do a search: %s\n"
			 "Check that the base name is valid."),
		       ldap_err2string(rc));
    return;
  }
  
  /*
   * Now loop over all the results, and spit out the output.
   */
  num_entries = 0;
  for (e = ldap_first_entry (directory, result);
       e != NULL;
       e = ldap_next_entry (directory, e)) {
    for (a = ldap_first_attribute(directory, e, &ber);
	 a != NULL;
	 a = ldap_next_attribute(directory, e, ber)) 
      /* NOP */;
    
    /*
     * Man page says: please free this when done.
     * If I do, I get segfault.
     * gdb session shows that ldap_unbind attempts to free
     * this later anyway (documentation for older version?)
     *
     * It seems like OpenLDAP hates this, and Netscape
     * Directory Server loves this...
     if (ber != NULL) ber_free (ber, 0);
    */
    num_entries++;
  }
  ldap_msgfree (result);
  
  /*
   * Check for e-mail addresses.
   */
  if (num_entries == 0)
    balsa_information (LIBBALSA_INFORMATION_WARNING,
		       _("Connection to server established.\n"
		       "No e-mail addresses were found.\n"
			 "Check the base domain name."));
  else
    balsa_information (LIBBALSA_INFORMATION_WARNING, _("LDAP tested OK.") );
  
/*
 * STEP 4: Disconnect from the server.
 */
  ldap_quit ();
  return;
}


/*
 * ldap_add_from_server ()
 *
 * Load addresses from the server.  It loads a single address in an
 * LDAPMessage.
 *
 * Returns
 *   GList * list with the address (if any) appended.
 */
static GList *
ldap_add_from_server (GList *list, LDAPMessage *e)
{
  gchar *name = NULL, *email = NULL, *id = NULL;
  gchar *first = NULL, *last = NULL;
  AddressData *data = NULL;
  char *a;
  char **vals;
  BerElement *ber = NULL;
  int i;

  g_free (first);
  first = NULL;
  g_free (last);
  last = NULL;
  for (a = ldap_first_attribute (directory, e, &ber);
       a != NULL;
       a = ldap_next_attribute (directory, e, ber))
  {
    /*
     * For each attribute, print the attribute name
     * and values.
     */
    if ((vals = ldap_get_values (directory, e, a)) != NULL)
    {
      for (i = 0; vals[i] != NULL; i++)
      {
	if ((g_strcasecmp (a, "sn") == 0) && (!last))
	  last = g_strdup (vals[i]);
	if ((g_strcasecmp (a, "cn") == 0) && (!id))
	  id = g_strdup (vals[i]);
	if ((g_strcasecmp (a, "givenname") == 0) &&
	    (!first))
	  first = g_strdup (vals[i]);
	if ((g_strcasecmp (a, "mail") == 0) && (!email))
	  email = g_strdup (vals[i]);
      }
      ldap_value_free (vals);
    }
  }
/*
 * Record will have e-mail (searched)
 */
  name = create_name (first, last);
  data = g_malloc (sizeof (AddressData));
  data->addy = email;
  if (id)
    data->id = id;
  else
    data->id = g_strdup ( _("No-Id") );
  if (name)
    data->name = name;
  else if (id) 
    data->name = g_strdup (id);
  else
    data->name = g_strdup( _("No-Name") );
  data->upper = g_strdup (data->name);
  g_strup (data->upper);
  list = g_list_append (list, data);
  name = NULL;
  id = NULL;
  email = NULL;
  /*
   * Man page says: please free this when done.
   * If I do, I get segfault.
   * gdb session shows that ldap_unbind attempts to free
   * this later anyway (documentation for older version?)
   if (ber != NULL) ber_free (ber, 0);
  */
  return list;
}


/*
 * ldap_load_addresses ()
 *
 * Load addresses in the LDAP server into a GList.  It accepts a GList
 * as input so that we can read GnomeCard addressbook first, if need
 * be.
 *
 * Returns:
 *   GList * AddressData
 *   NULL on failure or no addresses.
 *
 * Side effects:
 *   Spits out balsa_information() when it deems necessary.
 */
GList*
ldap_load_addresses (GList *current, gboolean multiples)
{
  LDAPMessage *result, *e;
  int rc, num_entries = 0;
  GList *list;
  
  list = current;
  
  /*
   * Connect to the server.
   */
  if (!directory)  {
    directory = ldap_initialise (balsa_app.ldap_host);
    if (!directory) return list;
  }
  
  /*
   * Attempt to search for e-mail addresses.  It returns success
   * or failure, but not all the matches.
   */
  if(balsa_app.debug) g_message ("Doing a search:");
  rc = ldap_search_s (directory, balsa_app.ldap_base_dn,
		      LDAP_SCOPE_SUBTREE, "(mail=*)", NULL, 0, &result);
  if (rc != LDAP_SUCCESS) {
    balsa_information (LIBBALSA_INFORMATION_WARNING,
		       _("Failed to do a search: %s"
			 "Check that the base name is valid."),
		       ldap_err2string(rc));
    return list;
  }
  
  /*
   * Now loop over all the results, and spit out the output.
   */
  num_entries = 0;
  for (e = ldap_first_entry (directory, result);
       e != NULL;
       e = ldap_next_entry (directory, e))
    list = ldap_add_from_server (list, e);
  ldap_msgfree (result);
  
  return list;
}
#endif /* ENABLE_LDAP */
