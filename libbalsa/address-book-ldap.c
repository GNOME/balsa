/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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
 * LDAP address book
 */
#include "config.h"

#if ENABLE_LDAP

#include <glib.h>
#include <gnome.h>
#include <sys/time.h>
#include <string.h>
#include <lber.h>
#include <ldap.h>
#include <iconv.h>

#include "address-book.h"
#include "address-book-ldap.h"
#include "information.h"

static const int DEBUG_LDAP = 0;
/* FIXME: Configurable... */
static const int LDAP_CACHE_TIMEOUT=300;	/* Seconds */
/* don't search when prefix has length shorter than LDAP_MIN_LEN */
static const unsigned LDAP_MIN_LEN=2;
/* Which parameters do we want back? */
char* attrs[] = {
    "cn",
    "mail",
    "sn",
    "givenname",
    NULL
};
/* End of FIXME */
#define LDAP_CODESET "UTF-8"
#define BALSA_CODESET "ISO-8859-1"


static LibBalsaAddressBookClass *parent_class = NULL;

static void
libbalsa_address_book_ldap_class_init(LibBalsaAddressBookLdapClass *
				      klass);
static void libbalsa_address_book_ldap_init(LibBalsaAddressBookLdap * ab);
static void libbalsa_address_book_ldap_finalize(GObject * object);

static LibBalsaABErr libbalsa_address_book_ldap_load(LibBalsaAddressBook * ab, 
                                                     LibBalsaAddressBookLoadFunc callback, 
                                                     gpointer closure);
static gboolean
libbalsa_address_book_ldap_open_connection(LibBalsaAddressBookLdap * ab);
static void
libbalsa_address_book_ldap_close_connection(LibBalsaAddressBookLdap * ab);

static void libbalsa_address_book_ldap_save_config(LibBalsaAddressBook *ab,
						   const gchar * prefix);
static void libbalsa_address_book_ldap_load_config(LibBalsaAddressBook *ab,
						   const gchar * prefix);

static GList *libbalsa_address_book_ldap_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix, 
							 gchar ** new_prefix);

static LibBalsaAddress *libbalsa_address_book_ldap_get_address(LibBalsaAddressBook * ab,
							       LDAPMessage * e);


static gchar *create_name(gchar *, gchar *);

GType libbalsa_address_book_ldap_get_type(void)
{
    static GType address_book_ldap_type = 0;

    if (!address_book_ldap_type) {
	static const GTypeInfo address_book_ldap_info = {
	    sizeof(LibBalsaAddressBookLdapClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_book_ldap_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddressBookLdap),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_address_book_ldap_init
	};

	address_book_ldap_type =
            g_type_register_static(LIBBALSA_TYPE_ADDRESS_BOOK,
	                           "LibBalsaAddressBookLdap",
			           &address_book_ldap_info, 0);
    }

    return address_book_ldap_type;
}

static void
libbalsa_address_book_ldap_class_init(LibBalsaAddressBookLdapClass * klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GObjectClass *object_class;

    parent_class = g_type_class_peek_parent(klass);

    object_class = G_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->finalize = libbalsa_address_book_ldap_finalize;

    address_book_class->load = libbalsa_address_book_ldap_load;

    address_book_class->save_config =
	libbalsa_address_book_ldap_save_config;
    address_book_class->load_config =
	libbalsa_address_book_ldap_load_config;

    address_book_class->alias_complete = 
	libbalsa_address_book_ldap_alias_complete;
}

static void
libbalsa_address_book_ldap_init(LibBalsaAddressBookLdap * ab)
{
    ab->host = NULL;
    ab->base_dn = NULL;
    ab->directory = NULL;
    LIBBALSA_ADDRESS_BOOK(ab)->is_expensive = FALSE;
}

static void
libbalsa_address_book_ldap_finalize(GObject * object)
{
    LibBalsaAddressBookLdap *addr_ldap;

    addr_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(object);

    libbalsa_address_book_ldap_close_connection(addr_ldap);

    g_free(addr_ldap->host);
    addr_ldap->host = NULL;
    g_free(addr_ldap->base_dn);
    addr_ldap->base_dn = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_ldap_new(const gchar * name, const gchar * host,
                               const gchar * base_dn)
{
    LibBalsaAddressBookLdap *ldap;
    LibBalsaAddressBook *ab;

    ldap =
        LIBBALSA_ADDRESS_BOOK_LDAP(g_object_new
                                   (LIBBALSA_TYPE_ADDRESS_BOOK_LDAP,
                                    NULL));
    ab = LIBBALSA_ADDRESS_BOOK(ldap);

    ab->name = g_strdup(name);
    ldap->host = g_strdup(host);
    ldap->base_dn = g_strdup(base_dn);
    /* We open on demand... */
    ldap->directory = NULL;
    return ab;
}

/*
 * Close the ldap connection....
 */
static void
libbalsa_address_book_ldap_close_connection(LibBalsaAddressBookLdap * ab)
{
    if (ab->directory) {
	ldap_destroy_cache(ab->directory);
	ldap_unbind(ab->directory);
	ab->directory = NULL;
    }
}

/*
 * Opens the ldap connection, and binds to the server.
 * Also enables LDAP caching.
 */
static gboolean
libbalsa_address_book_ldap_open_connection(LibBalsaAddressBookLdap * ab)
{
    int result;

    g_return_val_if_fail(ab->host != NULL, FALSE);

    ab->directory = ldap_init(ab->host, LDAP_PORT);
    if (ab->directory == NULL) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Failed to initialise LDAP server.\n"
			       "Check that the servername is valid."));
	perror("ldap_init");
	return FALSE;
    }

    result = ldap_simple_bind_s(ab->directory, NULL, NULL);

    if (result != LDAP_SUCCESS) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Failed to bind to server: %s\n"
			       "Check that the servername is valid."),
			     ldap_err2string(result));
	ldap_unbind_s(ab->directory);
	return FALSE;
    }
    /* ldap_enable_cache(ab->directory, LDAP_CACHE_TIMEOUT, 0); */
    return TRUE;
}


/*
 * ldap_load:
 * opens the connection only, if needed.
 */
static LibBalsaABErr
libbalsa_address_book_ldap_load(LibBalsaAddressBook * ab,
                                LibBalsaAddressBookLoadFunc callback,
                                gpointer closure)
{
    LibBalsaAddressBookLdap *ldap_ab;
    LibBalsaAddress *address;
    LDAPMessage *e, *result;
    int rc;

    g_return_val_if_fail ( LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab), LBABERR_OK);

    if (callback == NULL)
	return LBABERR_OK;

    ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);
    /*
     * Connect to the server.
     */
    if (ldap_ab->directory == NULL) {
	if (!libbalsa_address_book_ldap_open_connection(ldap_ab))
	    return LBABERR_CANNOT_CONNECT;
    }
    
    /* 
     * Attempt to search for e-mail addresses. It returns success 
     * or failure, but not all the matches. 
     */ 
    /* g_print("Performing full lookup...\n"); */
    rc = ldap_search_s(ldap_ab->directory, ldap_ab->base_dn,
		       LDAP_SCOPE_SUBTREE, "(mail=*)", NULL, 0, &result);
    if (rc != LDAP_SUCCESS)
	return LBABERR_CANNOT_SEARCH;
    
    /* 
     * Now loop over all the results, and spit out the output.
     */
    for(e = ldap_first_entry(ldap_ab->directory, result); e != NULL;
	e = ldap_next_entry(ldap_ab->directory, e)) {
	address = libbalsa_address_book_ldap_get_address(ab, e);
	callback(ab, address, closure);
	g_object_unref(address);
    }
    
    callback(ab, NULL, closure);
    /* printf("ldap_load:: result=%p\n", result); */
    ldap_msgfree(result);
    return LBABERR_OK;
}

/* ldap_get_string:
   Return native version of an LDAP encoded string.
 
 */

static gchar*
ldap_get_string(const gchar *ldap_string)
{
    char *in=(char *)ldap_string;
    size_t len=strlen(in), outlen=len;
    char *native_string=calloc(outlen+1, sizeof(char)), *out=native_string;
    iconv_t conv=iconv_open(BALSA_CODESET, LDAP_CODESET);

    while(len>0 && outlen>0) {
	if(iconv(conv, &in, &len, &out, &outlen)!=0) {
	    in++;		/* *** */
	    len--;
	}
    }
    iconv_close(conv);
    
    return (gchar *)native_string;
}

/* ldap_set_string:
   Return native LDAP encoded version of string.
 */

static gchar*
ldap_set_string(const gchar *native_string)
{
    char *in=(char *)native_string;
    size_t len=strlen(in), outlen=2*len; /* Worst case */
    char *ldap_string=calloc(outlen+1, sizeof(char)), *out=ldap_string;
    iconv_t conv=iconv_open(LDAP_CODESET, BALSA_CODESET);

    while(len>0 && outlen>0) {
	if(iconv(conv, &in, &len, &out, &outlen)!=0) {
	    in++;		/* *** */
	    len--;
	}
    }
    iconv_close(conv);
    
    return (gchar *)ldap_string;
}


/* libbalsa_address_book_ldap_get_address:
 * loads a single address from connection specified by LDAPMessage.
 */
static LibBalsaAddress* 
libbalsa_address_book_ldap_get_address(LibBalsaAddressBook * ab,
				       LDAPMessage * e)
{
    LibBalsaAddressBookLdap *ldap_ab;
    gchar *name = NULL, *email = NULL, *id = NULL;
    gchar *first = NULL, *last = NULL;
    LibBalsaAddress *address = NULL;
    char *attr;
    char **vals;
    BerElement *ber = NULL;
    int i;

    ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    for (attr = ldap_first_attribute(ldap_ab->directory, e, &ber);
	 attr != NULL; attr = ldap_next_attribute(ldap_ab->directory, e, ber)) {
	/*
	 * For each attribute, get the attribute name and values.
	 */
	if ((vals = ldap_get_values(ldap_ab->directory, e, attr)) != NULL) {
	    for (i = 0; vals[i] != NULL; i++) {
		if ((strcmp(attr, "sn") == 0) && (!last))
		    last = ldap_get_string(vals[i]);
		if ((strcmp(attr, "cn") == 0) && (!id))
		    id = ldap_get_string(vals[i]);
		if ((strcmp(attr, "givenname") == 0) && (!first))
		    first = ldap_get_string(vals[i]);
		if ((strcmp(attr, "mail") == 0) && (!email))
		    email = ldap_get_string(vals[i]);
	    }
	    ldap_value_free(vals);
	}
    }
    /*
     * Record will have e-mail (searched)
     */
    g_return_val_if_fail(email != NULL, NULL);
    name = create_name(first, last);

    address = libbalsa_address_new();
    address->id = id ? id : g_strdup(_("No-Id"));
    if (name)
	address->full_name = name;
    else if (id)
	address->full_name = g_strdup(id ? id : _("No-Name"));
    address->first_name = first;
    address->last_name = last;
    address->address_list = g_list_prepend(address->address_list, email);

    /*
     * Man page says: please free this when done.
     * If I do, I get segfault.
     * gdb session shows that ldap_unbind attempts to free
     * this later anyway (documentation for older version?)
     if (ber != NULL) ber_free (ber, 0);
     */
    return address;
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
create_name(gchar * first, gchar * last)
{
    if ((first == NULL) && (last == NULL))
	return NULL;
    else if (first == NULL)
	return g_strdup(last);
    else if (last == NULL)
	return g_strdup(first);
    else
	return g_strdup_printf("%s %s", first, last);
}

static void
libbalsa_address_book_ldap_save_config(LibBalsaAddressBook * ab,
				       const gchar * prefix)
{
    LibBalsaAddressBookLdap *ldap;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab));

    ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    gnome_config_set_string("Host", ldap->host);
    if(ldap->base_dn) gnome_config_set_string("BaseDN", ldap->base_dn);

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config(ab, prefix);
}

static void
libbalsa_address_book_ldap_load_config(LibBalsaAddressBook * ab,
				       const gchar * prefix)
{
    LibBalsaAddressBookLdap *ldap;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab));

    ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    ldap->host = gnome_config_get_string("Host");
    ldap->base_dn = gnome_config_get_string("BaseDN");
    if(ldap->base_dn && *ldap->base_dn == 0) { 
	g_free(ldap->base_dn); ldap->base_dn = NULL; 
    }

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config(ab, prefix);
}


/*
 * Format a string according to RFC 2254, which states:
 *
 * If a value should contain any of the following characters
 *
 *          Character       ASCII value
 *          ---------------------------
 *          *               0x2a
 *          (               0x28
 *          )               0x29
 *          \               0x5c
 *          NUL             0x00
 *
 * the character must be encoded as the backslash '\' character (ASCII
 * 0x5c) followed by the two hexadecimal digits representing the ASCII
 * value of the encoded character. The case of the two hexadecimal
 * digits is not significant.
 *
 * NOTE: This function does not escape embedded NUL, as the input
 * function is NUL terminated.
 */
static gchar*
rfc_2254_escape(const gchar *raw)
{
    gchar *new;
    gchar *str;
    gchar *step;

    new = (gchar *)malloc((strlen(raw) * 3) + 1);
    str = new;
    for (step = (gchar *)raw;
         step[0] != '\0';
         step++) {
        switch (step[0]) {
            case '*':
                str[0] = '\\'; str++;
                str[0] = '2'; str++;
                str[0] = 'a'; str++;
                break;
            case '(':
                str[0] = '\\'; str++;
                str[0] = '2'; str++;
                str[0] = '8'; str++;
                break;
            case ')':
                str[0] = '\\'; str++;
                str[0] = '2'; str++;
                str[0] = '9'; str++;
                break;
            case '\\':
                str[0] = '\\'; str++;
                str[0] = '5'; str++;
                str[0] = 'c'; str++;
                break;
            default:
                str[0] = step[0]; str++;
        }
    }
    str[0] = '\0';
    str = g_strdup(new);
    free(new);
    return str;
}


static GList *
libbalsa_address_book_ldap_alias_complete(LibBalsaAddressBook * ab,
					  const gchar * prefix, 
					  gchar ** new_prefix)
{
    static struct timeval timeout = { 15, 0 }; /* 15 sec timeout */
    LibBalsaAddressBookLdap *ldap_ab;
    LibBalsaAddress *addr;
    GList *res = NULL;
    gchar* filter;
    gchar* escaped;
    gchar* ldap;
    int rc;
    LDAPMessage * e, *result;

    g_return_val_if_fail ( LIBBALSA_ADDRESS_BOOK_LDAP(ab), NULL);

    ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    if (!ab->expand_aliases || strlen(prefix)<LDAP_MIN_LEN) return NULL;
    if (ldap_ab->directory == NULL)
        libbalsa_address_book_ldap_open_connection(ldap_ab);

    /*
     * Attempt to search for e-mail addresses.  It returns success
     * or failure, but not all the matches.
     */
    *new_prefix = NULL;
    escaped = rfc_2254_escape(prefix);
    ldap=ldap_set_string(escaped);
    g_free(escaped);

    filter = g_strdup_printf("(&(mail=*)(|(cn=%s*)(sn=%s*)(mail=%s@*)))", 
			     ldap, ldap, ldap);
    g_free(ldap);
    result = NULL;
    rc = ldap_search_st(ldap_ab->directory, ldap_ab->base_dn,
	   LDAP_SCOPE_SUBTREE, filter, attrs, 0, &timeout, &result);
    
    if(DEBUG_LDAP)
        g_print("Sent LDAP request: %s (basedn=%s) res=0x%x\n", 
                filter, ldap_ab->base_dn, rc);
    g_free(filter);
    switch (rc) {
    case LDAP_SUCCESS:
	for(e = ldap_first_entry(ldap_ab->directory, result);
	    e != NULL; e = ldap_next_entry(ldap_ab->directory, e)) {
	    addr = libbalsa_address_book_ldap_get_address(ab, e);
	    if(!*new_prefix) 
		*new_prefix = libbalsa_address_to_gchar(addr, 0);
	    res = g_list_prepend(res, addr);
	}
    case LDAP_SIZELIMIT_EXCEEDED:
    case LDAP_TIMELIMIT_EXCEEDED:
	/*
	 * These are administrative limits, so don't warn about them.
	 * Particularly SIZELIMIT can be nasty on big directories.
	 */
	break;
    case LDAP_SERVER_DOWN:
        libbalsa_address_book_ldap_close_connection
            (LIBBALSA_ADDRESS_BOOK_LDAP(ab));
        g_print("Server down. Next attempt will try to reconnect.\n");
        break;
    default:
	/*
	 * Until we know for sure, complain about all other errors.
	 */
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Failed to do a search: %s."
			       "Check that the base name is valid."),
			     ldap_err2string(rc));
	break;
    }

    /* printf("ldap_alias_complete:: result=%p\n", result); */
    if(result) ldap_msgfree(result);

    if(res) res = g_list_reverse(res);
    return res;
}
#endif				/*LDAP_ENABLED */
