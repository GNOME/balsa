/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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
 * NOTES:
 * ldap caching deleted since it is deprecated in openldap-2.1.x series.
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
/* don't search when prefix has length shorter than LDAP_MIN_LEN */
static const unsigned LDAP_MIN_LEN=2;
/* Which parameters do we want back? */
static char* attrs[] = {
    "cn",        /* maps to displayed name */
    "mail",      /* maps to itself         */
    "sn",        /* maps to last name      */
    "givenname", /* maps to first name     */
    "o",         /* maps to organization   */
    "uid",       /* maps to nick name      */
    NULL
};
/* End of FIXME */

static LibBalsaAddressBookClass *parent_class = NULL;

static void
libbalsa_address_book_ldap_class_init(LibBalsaAddressBookLdapClass *
				      klass);
static void libbalsa_address_book_ldap_init(LibBalsaAddressBookLdap * ab);
static void libbalsa_address_book_ldap_finalize(GObject * object);

static LibBalsaABErr libbalsa_address_book_ldap_load(LibBalsaAddressBook * ab, 
                                                     const gchar *filter,
                                                     LibBalsaAddressBookLoadFunc callback, 
                                                     gpointer closure);

static LibBalsaABErr
libbalsa_address_book_ldap_add_address(LibBalsaAddressBook *ab,
                                       LibBalsaAddress *address);
static LibBalsaABErr
libbalsa_address_book_ldap_remove_address(LibBalsaAddressBook *ab,
                                          LibBalsaAddress *address);
static LibBalsaABErr
libbalsa_address_book_ldap_modify_address(LibBalsaAddressBook *ab,
                                          LibBalsaAddress *address,
                                          LibBalsaAddress *newval);

static gboolean
libbalsa_address_book_ldap_open_connection(LibBalsaAddressBookLdap * ab);

static void libbalsa_address_book_ldap_save_config(LibBalsaAddressBook *ab,
						   const gchar * prefix);
static void libbalsa_address_book_ldap_load_config(LibBalsaAddressBook *ab,
						   const gchar * prefix);

static GList *libbalsa_address_book_ldap_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix, 
							 gchar ** new_prefix);

static LibBalsaAddress*
libbalsa_address_book_ldap_get_address(LibBalsaAddressBook * ab,
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
    address_book_class->add_address = libbalsa_address_book_ldap_add_address;
    address_book_class->remove_address = 
        libbalsa_address_book_ldap_remove_address;
    address_book_class->modify_address =
        libbalsa_address_book_ldap_modify_address;

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
    ab->bind_dn = NULL;
    ab->passwd  = NULL;
    ab->enable_tls = FALSE;
    ab->directory = NULL;
    LIBBALSA_ADDRESS_BOOK(ab)->is_expensive = FALSE;
}

static void
libbalsa_address_book_ldap_finalize(GObject * object)
{
    LibBalsaAddressBookLdap *addr_ldap;

    addr_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(object);

    libbalsa_address_book_ldap_close_connection(addr_ldap);

    g_free(addr_ldap->host);    addr_ldap->host = NULL;
    g_free(addr_ldap->base_dn); addr_ldap->base_dn = NULL;
    g_free(addr_ldap->bind_dn); addr_ldap->bind_dn = NULL;
    g_free(addr_ldap->passwd);  addr_ldap->passwd  = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_ldap_new(const gchar *name, const gchar *host,
                               const gchar *base_dn, const gchar *bind_dn,
                               const gchar *passwd, gboolean enable_tls)
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
    ldap->bind_dn = g_strdup(bind_dn);
    ldap->passwd = g_strdup(passwd);
    ldap->enable_tls = enable_tls;

    /* We open on demand... */
    ldap->directory = NULL;
    return ab;
}

/*
 * Close the ldap connection....
 */
void
libbalsa_address_book_ldap_close_connection(LibBalsaAddressBookLdap * ab)
{
    if (ab->directory) {
	ldap_unbind(ab->directory);
	ab->directory = NULL;
    }
}

/*
 * Opens the ldap connection, and binds to the server.
 * returns ldap status.
 */
static int
libbalsa_address_book_ldap_open_connection(LibBalsaAddressBookLdap * ab)
{
    int result;
    static const int version = LDAP_VERSION3;
    gboolean v3_enabled;
    LibBalsaAddressBook *lbab = LIBBALSA_ADDRESS_BOOK(ab);

    g_return_val_if_fail(ab->host != NULL, FALSE);

    ab->directory = ldap_init(ab->host, LDAP_PORT);
    if (ab->directory == NULL) { /* very unlikely... */
        libbalsa_address_book_set_status(lbab, g_strdup("Host not found"));
	return LDAP_SERVER_DOWN;
    }
    /* ignore error if the V3 LDAP cannot be set */
    v3_enabled = 
        ldap_set_option(ab->directory, LDAP_OPT_PROTOCOL_VERSION, &version)
       == LDAP_OPT_SUCCESS;
    if(!v3_enabled) ldap_perror(ab->directory, "ldap_set_option");
    if(v3_enabled && ab->enable_tls) {
#ifdef HAVE_LDAP_TLS
        /* turn TLS on */
        result = ldap_start_tls_s(ab->directory, NULL, NULL);
        if(result != LDAP_SUCCESS) {
            ldap_unbind(ab->directory);
            ab->directory = NULL;
            libbalsa_address_book_set_status
                (lbab, g_strdup(ldap_err2string(result)));
            return result;
        }
#else /* HAVE_LDAP_TLS */
     libbalsa_address_book_set_status(lbab,
                                      _("TLS requested but not compiled in"));
     return LDAP_INAPPRIOPRIATE_AUTH;
#endif /* HAVE_LDAP_TLS */
    }

    printf("Binding as: %s\n", ab->bind_dn ? ab->bind_dn : "anonymous");
    result = ldap_simple_bind_s(ab->directory, 
                                ab->bind_dn,
                                ab->passwd);

    if (result != LDAP_SUCCESS) {
        libbalsa_address_book_set_status(lbab,
                                         g_strdup(ldap_err2string(result)));
	ldap_unbind_s(ab->directory);
	ab->directory = NULL;
    }
    return result;
}


/*
 * ldap_load:
 * opens the connection only if needed.
 */
static LibBalsaABErr
libbalsa_address_book_ldap_load(LibBalsaAddressBook * ab,
                                const gchar *filter,
                                LibBalsaAddressBookLoadFunc callback,
                                gpointer closure)
{
    LibBalsaAddressBookLdap *ldap_ab;
    LibBalsaAddress *address;
    LDAPMessage *msg, *result;
    int msgid, rc, attempt;
    gchar *ldap_filter;

    g_return_val_if_fail ( LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab), LBABERR_OK);

    if (callback == NULL)
	return LBABERR_OK;

    ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);
    /*
     * Connect to the server.
     */
    for(attempt=0; attempt<2; attempt++) {
        if (ldap_ab->directory == NULL) {
            if ((rc=libbalsa_address_book_ldap_open_connection(ldap_ab))
                != LDAP_SUCCESS)
                return LBABERR_CANNOT_CONNECT;
        }
        
        /* 
         * Attempt to search for e-mail addresses. It returns success 
         * or failure, but not all the matches. 
         * we use the asynchronous lookup to fetch the results in chunks
         * in case we exceed administrative limits.
         */ 
        /* g_print("Performing full lookup...\n"); */
        ldap_filter = filter 
            ? g_strdup_printf("(&(objectClass=inetOrgPerson)"
                              "(|(cn=%s*)(sn=%s*)(mail=%s@*)))",
                              filter, filter, filter)
            : g_strdup("(objectClass=inetOrgPerson)");
        msgid = ldap_search(ldap_ab->directory, ldap_ab->base_dn,
                            LDAP_SCOPE_SUBTREE, 
                            ldap_filter, attrs, 0);
        if (msgid == -1) {
            libbalsa_address_book_ldap_close_connection(ldap_ab);
            continue; /* try again */
        }
        /* 
         * Now loop over all the results, and spit out the output.
         */
        
        while((rc=ldap_result(ldap_ab->directory, msgid, 
                              LDAP_MSG_ONE, NULL, &result))>0) {
            msg = ldap_first_entry(ldap_ab->directory, result);
            if (!msg || ldap_msgtype( msg ) == LDAP_RES_SEARCH_RESULT)
                break;
            address = libbalsa_address_book_ldap_get_address(ab, msg);
            callback(ab, address, closure);
            g_object_unref(address);
        }
        if(rc == -1) { /* try again */
            libbalsa_address_book_ldap_close_connection(ldap_ab);
            continue;
	}
        callback(ab, NULL, closure);
        ldap_msgfree(result);
        libbalsa_address_book_set_status(ab, NULL);
        return LBABERR_OK;
    }
    /* we have tried and failed... */
    /* extended status? */
    return LBABERR_CANNOT_SEARCH;
}


/* libbalsa_address_book_ldap_get_address:
 * loads a single address from connection specified by LDAPMessage.
 */
static LibBalsaAddress* 
libbalsa_address_book_ldap_get_address(LibBalsaAddressBook * ab,
				       LDAPMessage * e)
{
    LibBalsaAddressBookLdap *ldap_ab;
    gchar *email = NULL, *cn = NULL, *org = NULL, *uid = NULL;
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
		if ((g_ascii_strcasecmp(attr, "sn") == 0) && (!last))
		    last = g_strdup(vals[i]);
		if ((g_ascii_strcasecmp(attr, "cn") == 0) && (!cn))
		    cn = g_strdup(vals[i]);
		if ((g_ascii_strcasecmp(attr, "givenName") == 0) && (!first))
		    first = g_strdup(vals[i]);
		if ((g_ascii_strcasecmp(attr, "o") == 0) && (!org))
		    org = g_strdup(vals[i]);
		if ((g_ascii_strcasecmp(attr, "uid") == 0) && (!uid))
		    uid = g_strdup(vals[i]);
		if ((g_ascii_strcasecmp(attr, "mail") == 0) && (!email))
		    email = g_strdup(vals[i]);
	    }
	    ldap_value_free(vals);
	}
    }
    /*
     * Record will have e-mail (searched)
     */
    if(email == NULL) email = g_strdup("none");
    g_return_val_if_fail(email != NULL, NULL);

    address = libbalsa_address_new();
    address->nick_name = cn ? cn : g_strdup(_("No-Id"));
    if (cn) 
	address->full_name = g_strdup(cn);
    else {
	address->full_name = create_name(first, last);
        if(!address->full_name)
            address->full_name = g_strdup(_("No-Name"));
    }
    address->first_name = first;
    address->last_name = last;
    address->nick_name = uid;
    address->organization = org;
    address->address_list = g_list_prepend(address->address_list, email);

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

#define SETMOD(mods,modarr,op,attr,strv,val) \
   do { (mods) = &(modarr); (modarr).mod_type=attr; (modarr).mod_op=op;\
        (strv)[0]=(val); (modarr).mod_values=strv; \
      } while(0)

static LibBalsaABErr
libbalsa_address_book_ldap_add_address(LibBalsaAddressBook *ab,
                                       LibBalsaAddress *address)
{
    static char *object_class_values[] =
        { "person", "organizationalPerson", "inetOrgPerson", NULL };
    gchar *dn;
    LDAPMod *mods[7];
    LDAPMod modarr[6];
    int cnt;
    char *cn[]   = {NULL, NULL};
    char *gn[]   = {NULL, NULL};
    char *org[]  = {NULL, NULL};
    char *sn[]   = {NULL, NULL};
    char *mail[] = {NULL, NULL};
    LibBalsaAddressBookLdap *ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    g_return_val_if_fail(address, LBABERR_CANNOT_WRITE);
    g_return_val_if_fail(address->address_list, LBABERR_CANNOT_WRITE);

    if (ldap_ab->directory == NULL) {
        if(libbalsa_address_book_ldap_open_connection(ldap_ab) != LDAP_SUCCESS)
	    return LBABERR_CANNOT_CONNECT;
    }

    dn = g_strdup_printf("mail=%s,%s",
                         (char*)address->address_list->data,
                         ldap_ab->bind_dn);
    mods[0] = &modarr[0];
    modarr[0].mod_op = LDAP_MOD_ADD;
    modarr[0].mod_type = "objectClass";
    modarr[0].mod_values = object_class_values;
    cnt = 1;

    if(address->full_name) {
        SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_ADD,"cn",cn,address->full_name);
        cnt++;
    }
    if(address->first_name) {
        SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_ADD,"givenName",gn,
               address->first_name);
        cnt++;
    }
    if(address->last_name) {
        SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_ADD,"sn",sn,address->last_name);
        cnt++;
    }
    if(address->organization) {
        SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_ADD,"o",org,
               address->organization);
        cnt++;
    }
    SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_ADD,"mail",mail,
               (char*)address->address_list->data);
    cnt++;
    mods[cnt] = NULL;

    cnt = 0;
    do {
        int rc = ldap_add_s(ldap_ab->directory, dn, mods);
        switch(rc) {
        case LDAP_SUCCESS: g_free(dn); return LBABERR_OK;
        case LDAP_ALREADY_EXISTS: 
	    g_free(dn);
	    libbalsa_address_book_set_status(ab,
					     g_strdup(ldap_err2string(rc)));
	    return LBABERR_DUPLICATE;
        case LDAP_SERVER_DOWN:
            libbalsa_address_book_ldap_close_connection(ldap_ab);
        if( (rc=libbalsa_address_book_ldap_open_connection(ldap_ab))
	    != LDAP_SUCCESS) {
	    g_free(dn);
	    return LBABERR_CANNOT_CONNECT;
	}
        /* fall through */
        default:
            fprintf(stderr, "ldap_add for dn=\"%s\" failed[0x%x]: %s\n",
                    dn, rc, ldap_err2string(rc));
        }
    } while(cnt++<1);
    g_free(dn);
    libbalsa_address_book_set_status(ab, NULL);
    return LBABERR_CANNOT_WRITE;
}

static LibBalsaABErr
libbalsa_address_book_ldap_remove_address(LibBalsaAddressBook *ab,
                                          LibBalsaAddress *address)
{
    LibBalsaAddressBookLdap *ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);
    gchar *dn;
    int cnt, rc;

    g_return_val_if_fail(address, LBABERR_CANNOT_WRITE);
    g_return_val_if_fail(address->address_list, LBABERR_CANNOT_WRITE);

    if (ldap_ab->directory == NULL) {
        if( (rc=libbalsa_address_book_ldap_open_connection(ldap_ab))
	    != LDAP_SUCCESS)
	    return LBABERR_CANNOT_CONNECT;
    }

    dn = g_strdup_printf("mail=%s,%s",
                         (char*)address->address_list->data,
                         ldap_ab->bind_dn);
    cnt = 0;
    do {
        rc = ldap_delete_s(ldap_ab->directory, dn);
        switch(rc) {
        case LDAP_SUCCESS: g_free(dn); return LBABERR_OK;
        case LDAP_SERVER_DOWN:
            libbalsa_address_book_ldap_close_connection(ldap_ab);
	    if( (rc=libbalsa_address_book_ldap_open_connection(ldap_ab))
		!= LDAP_SUCCESS) {
                g_free(dn);
		return LBABERR_CANNOT_CONNECT;
	    }
            /* fall through */
        default:
            fprintf(stderr, "ldap_delete for dn=\"%s\" failed[0x%x]: %s\n",
                    dn, rc, ldap_err2string(rc));
        }
    } while(cnt++<1);
    g_free(dn);
    libbalsa_address_book_set_status(ab,
				     g_strdup(ldap_err2string(rc)));
    return LBABERR_CANNOT_WRITE;
}


/** libbalsa_address_book_ldap_modify_address:
    modify given address. If mail address has changed, remove and add.
*/
#define STREQ(a,b) ((a) && (b) && strcmp((a),(b))==0)
static LibBalsaABErr
libbalsa_address_book_ldap_modify_address(LibBalsaAddressBook *ab,
                                          LibBalsaAddress *address,
                                          LibBalsaAddress *newval)
{
    gchar *dn;
    LDAPMod *mods[5];
    LDAPMod modarr[4];
    int rc, cnt;
    char *cn[]   = {NULL, NULL};
    char *gn[]   = {NULL, NULL};
    char *org[]  = {NULL, NULL};
    char *sn[]   = {NULL, NULL};
    LibBalsaAddressBookLdap *ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    g_return_val_if_fail(address, LBABERR_CANNOT_WRITE);
    g_return_val_if_fail(address->address_list, LBABERR_CANNOT_WRITE);
    g_return_val_if_fail(newval->address_list, LBABERR_CANNOT_WRITE);

    if(!STREQ(address->address_list->data,newval->address_list->data)) {
        /* email address has changed, we have to remove old entry and
         * add a new one. */
        if( (rc=libbalsa_address_book_ldap_add_address(ab, newval)) 
            != LBABERR_OK)
            return rc;
        return libbalsa_address_book_ldap_remove_address(ab, address);
    }
    /* the email address has not changed, continue with changing other 
     * attributes. */
    if (ldap_ab->directory == NULL) {
        if( (rc=libbalsa_address_book_ldap_open_connection(ldap_ab))
	    != LDAP_SUCCESS)
	    return LBABERR_CANNOT_CONNECT;
    }

    dn = g_strdup_printf("mail=%s,%s",
                         (char*)address->address_list->data,
                         ldap_ab->bind_dn);
    cnt = 0;

    if(!STREQ(address->full_name,newval->full_name)) {
        if(newval->full_name)
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_REPLACE,"cn",cn,
                   newval->full_name);
        else
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_DELETE,"cn",cn,
                   address->full_name);
        cnt++;
    }
    if(!STREQ(address->first_name,newval->first_name)) {
        if(newval->first_name)
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_REPLACE,"givenName",gn,
                   newval->first_name);
        else
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_DELETE,"givenName",gn,
                   address->first_name);
        cnt++;
    }
    if(!STREQ(address->last_name,newval->last_name)) {
        if(newval->last_name)
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_REPLACE,"sn",sn,
                   newval->last_name);
        else
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_DELETE,"sn",sn,
                   address->last_name);
        cnt++;
    }
    if(!STREQ(address->organization,newval->organization)) {
        if(newval->organization)
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_REPLACE,"o",
                   org, newval->organization);
        else
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_DELETE,"o",
                   org, address->organization);
        cnt++;
    }
    mods[cnt] = NULL;

    if(cnt == 0) /* nothing to modify */
        return LBABERR_OK; 
    cnt = 0;
    do {
        rc = ldap_modify_s(ldap_ab->directory, dn, mods);
        switch(rc) {
        case LDAP_SUCCESS: return LBABERR_OK;
        case LDAP_SERVER_DOWN:
            libbalsa_address_book_ldap_close_connection(ldap_ab);
	    if( (rc=libbalsa_address_book_ldap_open_connection(ldap_ab))
		!= LDAP_SUCCESS) {
		g_free(dn);
		return LBABERR_CANNOT_CONNECT;
	    }
            /* fall through */
        default:
            fprintf(stderr, "ldap_modify for dn=\2%s\" failed[0x%x]: %s\n",
                    dn, rc, ldap_err2string(rc));
        }
    } while(cnt++<1);
    g_free(dn);
    libbalsa_address_book_set_status(ab,
				     g_strdup(ldap_err2string(rc)));
    return LBABERR_CANNOT_WRITE;
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
    if(ldap->bind_dn) gnome_config_private_set_string("BindDN", ldap->bind_dn);
    if(ldap->passwd)  gnome_config_private_set_string("Passwd", ldap->passwd);
    gnome_config_set_bool("EnableTLS", ldap->enable_tls);
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

    ldap->bind_dn = gnome_config_private_get_string("BindDN");
    if(ldap->bind_dn && *ldap->bind_dn == 0) { 
	g_free(ldap->bind_dn); ldap->bind_dn = NULL; 
    }
    ldap->passwd = gnome_config_private_get_string("Passwd");
    if(ldap->passwd && *ldap->passwd == 0) { 
	g_free(ldap->passwd); ldap->passwd = NULL; 
    }
    ldap->enable_tls = gnome_config_get_bool("EnableTLS");

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
    gchar* ldap;
    int rc;
    LDAPMessage * e, *result;

    g_return_val_if_fail ( LIBBALSA_ADDRESS_BOOK_LDAP(ab), NULL);

    ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    if (!ab->expand_aliases || strlen(prefix)<LDAP_MIN_LEN) return NULL;
    if (ldap_ab->directory == NULL) {
        if( (rc=libbalsa_address_book_ldap_open_connection(ldap_ab))
	    != LDAP_SUCCESS)
	    return NULL;
    }

    /*
     * Attempt to search for e-mail addresses.  It returns success
     * or failure, but not all the matches.
     */
    *new_prefix = NULL;
    ldap = rfc_2254_escape(prefix);

    filter = g_strdup_printf("(&(mail=*)"
                             "(|(cn=%s*)(sn=%s*)(mail=%s@*)))",
			     ldap, ldap, ldap);
    g_free(ldap);
    result = NULL;
    rc = ldap_search_st(ldap_ab->directory, ldap_ab->base_dn,
			LDAP_SCOPE_SUBTREE, filter, attrs, 0, 
			&timeout, &result);
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
        libbalsa_address_book_ldap_close_connection(ldap_ab);
        g_print("Server down. Next attempt will try to reconnect.\n");
        break;
    default:
	/*
	 * Until we know for sure, complain about all other errors.
	 */
	ldap_perror(ldap_ab->directory, "alias_complete::ldap_search_st");
	break;
    }

    /* printf("ldap_alias_complete:: result=%p\n", result); */
    if(result) ldap_msgfree(result);

    if(res) res = g_list_reverse(res);
    return res;
}
#endif				/*LDAP_ENABLED */
