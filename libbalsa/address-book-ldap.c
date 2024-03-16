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
 * LDAP address book
 * NOTES:
 * a) ldap caching deleted since it is deprecated in openldap-2.1.x series.
 * b) In principle, only inetOrgPerson class may have mail attributes, see
 * eg: http://www.andrew.cmu.edu/user/dd26/ldap.akbkhome.com/objectclass/organizationalPerson.html
 *
 * However, ActiveDirectory apparently gets it wrong (or its
 * administrators, may be?) and all the objects are of
 * organizationalPerson type. We are lenient.
 *
 */
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#if ENABLE_LDAP

#include "address-book-ldap.h"

#include <glib.h>
#include <ldap.h>

#if defined(HAVE_LIBSECRET)
#include <libsecret/secret.h>
#endif                          /* defined(HAVE_LIBSECRET) */

#ifdef HAVE_CYRUS_SASL
#include <sasl.h>
#endif

#include "information.h"
#include "libbalsa-conf.h"
#include <glib/gi18n.h>


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "address-book"


/* don't search when prefix has length shorter than LDAP_MIN_LEN */
static const unsigned ABL_MIN_LEN=2;
static const int ABL_SIZE_LIMIT = 5000;       /* full list   */
static const int ABL_SIZE_LIMIT_LOOKUP = 50; /* quick lookup */
/* Which parameters do we want back? */
static char* book_attrs[] = {
    "cn",        /* maps to displayed name */
    "mail",      /* maps to itself         */
    "sn",        /* maps to last name      */
    "givenname", /* maps to first name     */
    "o",         /* maps to organization   */
    "uid",       /* maps to nick name      */
    NULL
};

static char* complete_attrs[] = {
    "cn",        /* maps to displayed name */
    "mail",      /* maps to itself         */
    "sn",        /* maps to last name      */
    "givenname", /* maps to first name     */
    NULL
};
/* End of FIXME */



#if defined(HAVE_LIBSECRET)
static const SecretSchema ldap_schema = {
    "org.gnome.Balsa.LdapPassword", SECRET_SCHEMA_NONE,
    {
	{ "server", SECRET_SCHEMA_ATTRIBUTE_STRING },
	{ "user", SECRET_SCHEMA_ATTRIBUTE_STRING },
	{ NULL, 0 }
    }
};
#endif                          /* defined(HAVE_LIBSECRET) */


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
static void libbalsa_address_book_ldap_store_passwd(LibBalsaAddressBookLdap *ab_ldap);
static void libbalsa_address_book_ldap_load_passwd(LibBalsaAddressBookLdap *ab_ldap);

static GList *libbalsa_address_book_ldap_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix);

static LibBalsaAddress*
libbalsa_address_book_ldap_get_address(LibBalsaAddressBook * ab,
                                       LDAPMessage * e);


static gchar *create_name(gchar *, gchar *);

struct _LibBalsaAddressBookLdap {
    LibBalsaAddressBook parent;

    gchar *host;
    gchar *base_dn;
    gchar *bind_dn;
    gchar *priv_book_dn; /* location of user-writeable entries */
    gchar *passwd;
    gboolean enable_tls;

    LDAP *directory;
};

G_DEFINE_TYPE(LibBalsaAddressBookLdap, libbalsa_address_book_ldap,
        LIBBALSA_TYPE_ADDRESS_BOOK)

static void
libbalsa_address_book_ldap_class_init(LibBalsaAddressBookLdapClass * klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GObjectClass *object_class;

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
libbalsa_address_book_ldap_init(LibBalsaAddressBookLdap * ab_ldap)
{
    ab_ldap->host = NULL;
    ab_ldap->base_dn = NULL;
    ab_ldap->bind_dn = NULL;
    ab_ldap->priv_book_dn = NULL;
    ab_ldap->passwd  = NULL;
    ab_ldap->enable_tls = FALSE;
    ab_ldap->directory = NULL;
    libbalsa_address_book_set_is_expensive(LIBBALSA_ADDRESS_BOOK(ab_ldap), TRUE);
}

static void
libbalsa_address_book_ldap_finalize(GObject * object)
{
    LibBalsaAddressBookLdap *ab_ldap;

    ab_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(object);

    libbalsa_address_book_ldap_close_connection(ab_ldap);

    g_free(ab_ldap->host);         ab_ldap->host = NULL;
    g_free(ab_ldap->base_dn);      ab_ldap->base_dn = NULL;
    g_free(ab_ldap->bind_dn);      ab_ldap->bind_dn = NULL;
    g_free(ab_ldap->priv_book_dn); ab_ldap->priv_book_dn = NULL;
#if defined(HAVE_LIBSECRET)
    secret_password_free(ab_ldap->passwd);
#else
    g_free(ab_ldap->passwd);
#endif
    ab_ldap->passwd  = NULL;

    G_OBJECT_CLASS(libbalsa_address_book_ldap_parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_ldap_new(const gchar *name, const gchar *host,
                               const gchar *base_dn, const gchar *bind_dn,
                               const gchar *passwd, const gchar *priv_book_dn,
                               gboolean enable_tls)
{
    LibBalsaAddressBookLdap *ab_ldap;
    LibBalsaAddressBook *ab;

    ab_ldap =
        LIBBALSA_ADDRESS_BOOK_LDAP(g_object_new
                                   (LIBBALSA_TYPE_ADDRESS_BOOK_LDAP,
                                    NULL));
    ab = LIBBALSA_ADDRESS_BOOK(ab_ldap);

    libbalsa_address_book_set_name(ab, name);
    ab_ldap->host = g_strdup(host);
    ab_ldap->base_dn = g_strdup(base_dn);
    ab_ldap->bind_dn = g_strdup(bind_dn);
    ab_ldap->priv_book_dn = g_strdup(priv_book_dn ? priv_book_dn : bind_dn);
    ab_ldap->passwd = g_strdup(passwd);
    ab_ldap->enable_tls = enable_tls;

    /* We open on demand... */
    ab_ldap->directory = NULL;
    return ab;
}

/*
 * Close the ldap connection....
 */
void
libbalsa_address_book_ldap_close_connection(LibBalsaAddressBookLdap * ab_ldap)
{
    if (ab_ldap->directory) {
	ldap_unbind_ext(ab_ldap->directory, NULL, NULL);
	ab_ldap->directory = NULL;
    }
}

/*
 * Opens the ldap connection, and binds to the server.
 * returns ldap status.
 */
#ifdef HAVE_CYRUS_SASL
static int
abl_interaction(unsigned flags, sasl_interact_t *interact,
                LibBalsaAddressBookLdap *ab_ldap)
{
    switch(interact->id) {
    case SASL_CB_PASS: break;
    case SASL_CB_GETREALM:
    case SASL_CB_AUTHNAME:
    case SASL_CB_USER:   
    case SASL_CB_NOECHOPROMPT:
    case SASL_CB_ECHOPROMPT:  
        g_debug("unhandled SASL request %d", interact->id);
        return LDAP_INAVAILABLE;
    }

    interact->result = ab_ldap->passwd;
    interact->len = interact->result ? strlen(interact->result) : 0;
    return LDAP_SUCCESS;
}


int abl_interact(LDAP *ld, unsigned flags, void* defaults, void *interact )
{
    if( ld == NULL ) return LDAP_PARAM_ERROR;

    if (flags == LDAP_SASL_INTERACTIVE) {
        g_debug("SASL Interaction");
    }

    while( interact->id != SASL_CB_LIST_END ) {
        int rc = lbabl_interaction(flags, interact, defaults);

        if( rc )  return rc;
        interact++;
    }

    return LDAP_SUCCESS;
}
#endif


/* note: free the returned value by calling ldap_memfree() */
static gchar *
ldap_connection_get_uri(LDAP *ldap)
{
	gchar *conn_uri;
	int res;

	res = ldap_get_option(ldap, LDAP_OPT_URI, &conn_uri);
	if (res != LDAP_OPT_SUCCESS) {
		ldap_memfree(conn_uri);
		conn_uri = NULL;
	}
	return conn_uri;
}


static gboolean
ldap_connection_is_ldaps(LDAP *ldap)
{
	gchar *conn_uri;
	gboolean result = FALSE;

	conn_uri = ldap_connection_get_uri(ldap);
	if (conn_uri != NULL) {
		result = (g_ascii_strncasecmp(conn_uri, "ldaps://", 8U) == 0);
		ldap_memfree(conn_uri);
	}
	return result;
}


static inline const gchar *
ldap_use_config_value(const gchar *item)
{
	/* if the URI or BASE entries from /etc/ldap/ldap.conf, $HOME/ldaprc, $HOME/.ldaprc or $CWD/ldaprc shall be used, they must
	 * be given as NULL to the openldap functions, not as empty string. */
	if ((item == NULL) || (item[0] == '\0')) {
		return NULL;
	} else {
		return item;
	}
}


static int
libbalsa_address_book_ldap_open_connection(LibBalsaAddressBookLdap * ab_ldap)
{
    int result;
    static const int version = LDAP_VERSION3;
    gboolean v3_enabled;
    LibBalsaAddressBook *ab = LIBBALSA_ADDRESS_BOOK(ab_ldap);

    ldap_initialize(&ab_ldap->directory, ldap_use_config_value(ab_ldap->host));
    if (ab_ldap->directory == NULL) { /* very unlikely... */
        libbalsa_address_book_set_status(ab, _("Host not found"));
	return LDAP_SERVER_DOWN;
    }
    /* ignore error if the V3 LDAP cannot be set */
    v3_enabled = 
        ldap_set_option(ab_ldap->directory, LDAP_OPT_PROTOCOL_VERSION, &version)
       == LDAP_OPT_SUCCESS;
    if (!v3_enabled) {
    	gchar *uri;

    	uri = ldap_connection_get_uri(ab_ldap->directory);
    	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
    			_("The LDAP server “%s” does not support LDAPv3, interaction may fail."), uri);
    	ldap_memfree(uri);
    }

    if (v3_enabled && ab_ldap->enable_tls) {
#ifdef HAVE_LDAP_TLS
    	if (ldap_connection_is_ldaps(ab_ldap->directory)) {
        	gchar *uri;

        	uri = ldap_connection_get_uri(ab_ldap->directory);
    		g_debug("LDAP address book '%s', URI '%s', uses TLS, ignore STARTTLS option",
    				libbalsa_address_book_get_name(ab), uri);
        	ldap_memfree(uri);
    	} else {
    		/* turn TLS on */
    		result = ldap_start_tls_s(ab_ldap->directory, NULL, NULL);
    		if (result != LDAP_SUCCESS) {
    			ldap_unbind_ext(ab_ldap->directory, NULL, NULL);
    			ab_ldap->directory = NULL;
    			libbalsa_address_book_set_status(ab, ldap_err2string(result));
    			return result;
    		}
    	}
#else /* HAVE_LDAP_TLS */
     libbalsa_address_book_set_status(ab,
                                      _("TLS requested but not compiled in"));
     return LDAP_INAPPRIOPRIATE_AUTH;
#endif /* HAVE_LDAP_TLS */
    }

#ifdef HAVE_CYRUS_SASL
    result = ldap_sasl_interactive_bind_s(ab_ldap->directory, ab_ldap->bind_dn, NULL,
                                          NULL, NULL,
                                          LDAP_SASL_QUIET, abl_interact, ab_ldap);
#else /* HAVE_CYRUS_SASL */
    {
     struct berval   cred;
     cred.bv_val = ab_ldap->passwd;
     cred.bv_len = ab_ldap->passwd ? strlen(ab_ldap->passwd) : 0;
     result = ldap_sasl_bind_s(ab_ldap->directory, ab_ldap->bind_dn, NULL, &cred,
                              NULL, NULL, NULL);
    }
#endif /* HAVE_CYRUS_SASL */

    /* do not follow referrals (OpenLDAP binds anonymously here, which will usually
     * fail */
    if (result == LDAP_SUCCESS)
	result = ldap_set_option(ab_ldap->directory, LDAP_OPT_REFERRALS, (void *)LDAP_OPT_OFF);

    if (result != LDAP_SUCCESS) {
        libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Cannot open LDAP address book %s: %s"),
            libbalsa_address_book_get_name(ab), ldap_err2string(result));
        libbalsa_address_book_set_status(ab, ldap_err2string(result));
	ldap_unbind_ext(ab_ldap->directory, NULL, NULL);
	ab_ldap->directory = NULL;
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
    LibBalsaAddressBookLdap *ab_ldap;
    LibBalsaAddress *address;
    LDAPMessage *msg, *result;
    int msgid, rc, attempt;
    gchar *ldap_filter;

    g_return_val_if_fail ( LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab), LBABERR_OK);

    if (callback == NULL)
	return LBABERR_OK;

    ab_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);
    /*
     * Connect to the server.
     */
    for (attempt = 0; attempt < 2; attempt++) {
        if (ab_ldap->directory == NULL) {
            if (libbalsa_address_book_ldap_open_connection(ab_ldap) != LDAP_SUCCESS)
                return LBABERR_CANNOT_CONNECT;
        }

        /* 
         * Attempt to search for e-mail addresses. It returns success 
         * or failure, but not all the matches. 
         * we use the asynchronous lookup to fetch the results in chunks
         * in case we exceed administrative limits.
         */ 
        ldap_filter = filter 
            ? g_strdup_printf("(&(objectClass=organizationalPerson)(mail=*)"
                              "(|(cn=%s*)(sn=%s*)(mail=%s*@*)))",
                              filter, filter, filter)
            : g_strdup("(&(objectClass=organizationalPerson)(mail=*))");
	    g_debug("Send LDAP request: %s (basedn=%s)", ldap_filter,
		    ab_ldap->base_dn);
        if(ldap_search_ext(ab_ldap->directory,
        		           ldap_use_config_value(ab_ldap->base_dn),
                           LDAP_SCOPE_SUBTREE, 
                           ldap_filter, book_attrs, 0, NULL, NULL,
                           NULL, ABL_SIZE_LIMIT, &msgid) != LDAP_SUCCESS) {
            libbalsa_address_book_ldap_close_connection(ab_ldap);
            continue; /* try again */
        }
        /* 
         * Now loop over all the results, and spit out the output.
         */
        
        while((rc=ldap_result(ab_ldap->directory, msgid, 
                              LDAP_MSG_ONE, NULL, &result))>0) {
            msg = ldap_first_entry(ab_ldap->directory, result);
            if (!msg || ldap_msgtype( msg ) == LDAP_RES_SEARCH_RESULT)
                break;
            address = libbalsa_address_book_ldap_get_address(ab, msg);
            callback(ab, address, closure);
            g_object_unref(address);
        }
        if(rc == -1) { /* try again */
            libbalsa_address_book_ldap_close_connection(ab_ldap);
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
    LibBalsaAddressBookLdap *ab_ldap;
    gchar *email = NULL;
    gchar *cn = NULL, *org = NULL, *uid = NULL;
    gchar *first = NULL, *last = NULL;
    LibBalsaAddress *address = NULL;
    char *attr;
    struct berval **vals;
    BerElement *ber = NULL;
    int i;

    ab_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    for (attr = ldap_first_attribute(ab_ldap->directory, e, &ber);
	 attr != NULL; attr=ldap_next_attribute(ab_ldap->directory, e, ber)) {
	/*
	 * For each attribute, get the attribute name and values.
	 */
	if ((vals = ldap_get_values_len(ab_ldap->directory, e, attr)) != NULL) {
	    for (i = 0; vals[i] != NULL; i++) {
		if (g_ascii_strcasecmp(attr, "sn") == 0) {
                    if (last == NULL)
                        last = g_strndup(vals[i]->bv_val, vals[i]->bv_len);
                } else if (g_ascii_strcasecmp(attr, "cn") == 0) {
                    if (cn == NULL)
                        cn = g_strndup(vals[i]->bv_val, vals[i]->bv_len);
                } else if (g_ascii_strcasecmp(attr, "givenName") == 0) {
                    if (first == NULL)
                        first = g_strndup(vals[i]->bv_val, vals[i]->bv_len);
                } else if (g_ascii_strcasecmp(attr, "o") == 0) {
                    if (org == NULL)
                        org = g_strndup(vals[i]->bv_val, vals[i]->bv_len);
                } else if (g_ascii_strcasecmp(attr, "uid") == 0) {
                    if (uid == NULL)
                        uid = g_strndup(vals[i]->bv_val, vals[i]->bv_len);
                } else if (g_ascii_strcasecmp(attr, "mail") == 0) {
                    if (email == NULL)
                        email = g_strndup(vals[i]->bv_val, vals[i]->bv_len);
                }
	    }
	    ldap_value_free_len(vals);
	}
        ldap_memfree(attr);
    }
    ber_free(ber, 0);

    /*
     * Record will have e-mail (searched)
     */
    if (email == NULL)
        email = g_strdup("none");

    address = libbalsa_address_new();
    if (cn != NULL) {
        libbalsa_address_set_full_name(address, cn);
        g_free(cn);
    } else {
        gchar *full_name = create_name(first, last);

        if (full_name != NULL) {
            libbalsa_address_set_full_name(address, full_name);
            g_free(full_name);
        } else {
            libbalsa_address_set_full_name(address, _("No-Name"));
        }
    }

    libbalsa_address_set_first_name(address, first);
    g_free(first);
    libbalsa_address_set_last_name(address, last);
    g_free(last);
    libbalsa_address_set_nick_name(address, uid);
    g_free(uid);
    libbalsa_address_set_organization(address, org);
    g_free(org);
    libbalsa_address_append_addr(address, email);
    g_free(email);

    return address;
}

static GList *
lbabl_get_internet_address(GList *addrs, LDAP *dir, LDAPMessage * e)
{
    BerElement *ber = NULL;
    char *attr;
    struct berval **vals;
    int i;
    GList *email = NULL;
    GList *p;
    gchar *sn = NULL, *cn = NULL, *first = NULL;

    for (attr = ldap_first_attribute(dir, e, &ber);
	 attr != NULL; 
         attr = ldap_next_attribute(dir, e, ber)) {
	/*
	 * For each attribute, get the attribute name and values.
	 */
	if ((vals = ldap_get_values_len(dir, e, attr)) != NULL) {
	    for (i = 0; vals[i] != NULL; i++) {
		if ((g_ascii_strcasecmp(attr, "sn") == 0) && (!sn))
		    sn = g_strndup(vals[i]->bv_val, vals[i]->bv_len);
		if ((g_ascii_strcasecmp(attr, "cn") == 0) && (!cn))
		    cn = g_strndup(vals[i]->bv_val, vals[i]->bv_len);
		if ((g_ascii_strcasecmp(attr, "givenName") == 0) && (!first))
		    first = g_strndup(vals[i]->bv_val, vals[i]->bv_len);
		if (g_ascii_strcasecmp(attr, "mail") == 0)
		    email = g_list_prepend(email, g_strndup(vals[i]->bv_val, vals[i]->bv_len));
	    }
	    ldap_value_free_len(vals);
	}
        ldap_memfree(attr);
    }
    ber_free(ber, 0);

    if(!cn)
        cn = create_name(first, sn);
    for (p = email; p != NULL; p = p->next) {
    	addrs = g_list_prepend(addrs, internet_address_mailbox_new(cn, (const gchar *) p->data));
    }
    g_list_free_full(email, g_free);
    g_free(sn); g_free(cn); g_free(first);

    return addrs;
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
    if (first == NULL) /* or if both are NULL */
	return g_strdup(last);
    else if (last == NULL)
	return g_strdup(first);
    else
	return g_strdup_printf("%s %s", first, last);
}

#define SETMOD(mods,modarr,op,attr,strv,val) \
   do { (mods) = &(modarr); (modarr).mod_type=attr; (modarr).mod_op=op;\
        (strv)[0]=(char*)(val); (modarr).mod_values=strv; \
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
    LibBalsaAddressBookLdap *ab_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);
    const gchar *addr;
    const gchar *item;

    g_return_val_if_fail(address != NULL, LBABERR_CANNOT_WRITE);
    addr = libbalsa_address_get_addr(address);
    g_return_val_if_fail(addr != NULL, LBABERR_CANNOT_WRITE);

    if (ab_ldap->directory == NULL) {
        if(libbalsa_address_book_ldap_open_connection(ab_ldap) != LDAP_SUCCESS)
	    return LBABERR_CANNOT_CONNECT;
    }

    if(ab_ldap->priv_book_dn == NULL) {
        libbalsa_address_book_set_status
            (ab, _("Undefined location of user address book"));
        return LBABERR_CANNOT_WRITE;
    }

    dn = g_strdup_printf("mail=%s,%s",
                         addr,
                         ab_ldap->priv_book_dn);
    mods[0] = &modarr[0];
    modarr[0].mod_op = LDAP_MOD_ADD;
    modarr[0].mod_type = "objectClass";
    modarr[0].mod_values = object_class_values;
    cnt = 1;

    item = libbalsa_address_get_full_name(address);
    if (item != NULL) {
        SETMOD(mods[cnt], modarr[cnt], LDAP_MOD_ADD, "cn", cn, item);
        cnt++;
    }

    item = libbalsa_address_get_first_name(address);
    if (item != NULL) {
        SETMOD(mods[cnt], modarr[cnt], LDAP_MOD_ADD, "givenName", gn, item);
        cnt++;
    }

    item = libbalsa_address_get_last_name(address);
    if (item != NULL) {
        SETMOD(mods[cnt], modarr[cnt], LDAP_MOD_ADD, "sn", sn, item);
        cnt++;
    }

    item = libbalsa_address_get_organization(address);
    if (item != NULL) {
        SETMOD(mods[cnt], modarr[cnt], LDAP_MOD_ADD, "o", org, item);
        cnt++;
    }

    SETMOD(mods[cnt], modarr[cnt], LDAP_MOD_ADD, "mail", mail, addr);
    cnt++;
    mods[cnt] = NULL;

    cnt = 0;
    do {
        int rc = ldap_add_ext_s(ab_ldap->directory, dn, mods, NULL, NULL);
        switch(rc) {
        case LDAP_SUCCESS: g_free(dn); return LBABERR_OK;
        case LDAP_ALREADY_EXISTS: 
	    g_free(dn);
	    libbalsa_address_book_set_status(ab, ldap_err2string(rc));
	    return LBABERR_DUPLICATE;
        case LDAP_SERVER_DOWN:
            libbalsa_address_book_ldap_close_connection(ab_ldap);
        if( (rc=libbalsa_address_book_ldap_open_connection(ab_ldap))
	    != LDAP_SUCCESS) {
	    g_free(dn);
	    return LBABERR_CANNOT_CONNECT;
	}
        /* fall through */
        default:
            g_debug("ldap_add for dn=“%s” failed[0x%x]: %s",
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
    LibBalsaAddressBookLdap *ab_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);
    const gchar *addr;
    gchar *dn;
    int cnt, rc;

    g_return_val_if_fail(address != NULL, LBABERR_CANNOT_WRITE);
    addr = libbalsa_address_get_addr(address);
    g_return_val_if_fail(addr != NULL, LBABERR_CANNOT_WRITE);

    if (ab_ldap->directory == NULL) {
        if (libbalsa_address_book_ldap_open_connection(ab_ldap) != LDAP_SUCCESS)
	    return LBABERR_CANNOT_CONNECT;
    }

    dn = g_strdup_printf("mail=%s,%s",
                         addr,
                         ab_ldap->priv_book_dn);
    cnt = 0;
    do {
        rc = ldap_delete_ext_s(ab_ldap->directory, dn, NULL, NULL);
        switch(rc) {
        case LDAP_SUCCESS: g_free(dn); return LBABERR_OK;
        case LDAP_SERVER_DOWN:
            libbalsa_address_book_ldap_close_connection(ab_ldap);
	    if( (rc=libbalsa_address_book_ldap_open_connection(ab_ldap))
		!= LDAP_SUCCESS) {
                g_free(dn);
		return LBABERR_CANNOT_CONNECT;
	    }
            /* fall through */
        default:
            g_debug("ldap_delete for dn=“%s” failed[0x%x]: %s",
                    dn, rc, ldap_err2string(rc));
        }
    } while(cnt++<1);
    g_free(dn);
    libbalsa_address_book_set_status(ab, ldap_err2string(rc));
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
    LibBalsaAddressBookLdap *ab_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);
    const gchar *addr;
    const gchar *new_addr;
    const gchar *item;
    const gchar *new_item;

    g_return_val_if_fail(address != NULL, LBABERR_CANNOT_WRITE);
    addr = libbalsa_address_get_addr(address);
    g_return_val_if_fail(addr != NULL, LBABERR_CANNOT_WRITE);

    g_return_val_if_fail(newval != NULL, LBABERR_CANNOT_WRITE);
    new_addr = libbalsa_address_get_addr(newval);
    g_return_val_if_fail(new_addr != NULL, LBABERR_CANNOT_WRITE);

    if(!STREQ(addr, new_addr)) {
        /* email address has changed, we have to remove old entry and
         * add a new one. */
        if ((rc = libbalsa_address_book_ldap_add_address(ab, newval))
            != LBABERR_OK)
            return rc;
        return libbalsa_address_book_ldap_remove_address(ab, address);
    }

    /* the email address has not changed, continue with changing other
     * attributes. */
    if (ab_ldap->directory == NULL) {
        if (libbalsa_address_book_ldap_open_connection(ab_ldap) != LDAP_SUCCESS)
	    return LBABERR_CANNOT_CONNECT;
    }

    dn = g_strdup_printf("mail=%s,%s",
                         addr,
                         ab_ldap->priv_book_dn);
    cnt = 0;

    item = libbalsa_address_get_full_name(address);
    new_item = libbalsa_address_get_full_name(newval);
    if (!STREQ(item, new_item)) {
        if (new_item != NULL)
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_REPLACE,"cn",cn, new_item);
        else
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_DELETE,"cn",cn, item);
        cnt++;
    }

    item = libbalsa_address_get_first_name(address);
    new_item = libbalsa_address_get_first_name(newval);
    if (!STREQ(item, new_item)) {
        if (new_item != NULL)
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_REPLACE,"givenName",gn, new_item);
        else
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_DELETE,"givenName",gn, item);
        cnt++;
    }

    item = libbalsa_address_get_last_name(address);
    new_item = libbalsa_address_get_last_name(newval);
    if (!STREQ(item, new_item)) {
        if (new_item != NULL)
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_REPLACE,"sn",sn, new_item);
        else
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_DELETE,"sn",sn, item);
        cnt++;
    }

    item = libbalsa_address_get_organization(address);
    new_item = libbalsa_address_get_organization(newval);
    if (!STREQ(item, new_item)) {
        if (new_item != NULL)
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_REPLACE,"o", org, new_item);
        else
            SETMOD(mods[cnt],modarr[cnt],LDAP_MOD_DELETE,"o", org, item);
        cnt++;
    }
    mods[cnt] = NULL;

    if (cnt == 0) {
        /* nothing to modify */
        g_free(dn);
        return LBABERR_OK;
    }

    cnt = 0;
    do {
        rc = ldap_modify_ext_s(ab_ldap->directory, dn, mods, NULL, NULL);
        switch(rc) {
        case LDAP_SUCCESS: return LBABERR_OK;
        case LDAP_SERVER_DOWN:
            libbalsa_address_book_ldap_close_connection(ab_ldap);
	    if( (rc=libbalsa_address_book_ldap_open_connection(ab_ldap))
		!= LDAP_SUCCESS) {
		g_free(dn);
		return LBABERR_CANNOT_CONNECT;
	    }
            /* fall through */
        default:
            g_debug("ldap_modify for dn=“%s” failed[0x%x]: %s",
                    dn, rc, ldap_err2string(rc));
        }
    } while(cnt++<1);
    g_free(dn);
    libbalsa_address_book_set_status(ab, ldap_err2string(rc));
    return LBABERR_CANNOT_WRITE;
}


static void
libbalsa_address_book_ldap_save_config(LibBalsaAddressBook * ab,
				       const gchar * prefix)
{
    LibBalsaAddressBookClass *parent_class =
        LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_ldap_parent_class);
    LibBalsaAddressBookLdap *ab_ldap;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab));

    ab_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    libbalsa_conf_set_string("Host", ab_ldap->host);
    if (ab_ldap->base_dn != NULL)
        libbalsa_conf_set_string("BaseDN", ab_ldap->base_dn);
    if (ab_ldap->bind_dn != NULL)
        libbalsa_conf_private_set_string("BindDN", ab_ldap->bind_dn, FALSE);
    if (ab_ldap->passwd != NULL) {
    	libbalsa_address_book_ldap_store_passwd(ab_ldap);
    }
    if (ab_ldap->priv_book_dn != NULL)
        libbalsa_conf_set_string("BookDN", ab_ldap->priv_book_dn);
    libbalsa_conf_set_bool("EnableTLS", ab_ldap->enable_tls);
    if (parent_class->save_config != NULL)
	parent_class->save_config(ab, prefix);
}

static void
libbalsa_address_book_ldap_load_config(LibBalsaAddressBook * ab,
				       const gchar * prefix)
{
    LibBalsaAddressBookClass *parent_class =
        LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_ldap_parent_class);
    LibBalsaAddressBookLdap *ab_ldap;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab));

    ab_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    ab_ldap->host = libbalsa_conf_get_string("Host");
    ab_ldap->base_dn = libbalsa_conf_get_string("BaseDN");
    if (ab_ldap->base_dn != NULL && *ab_ldap->base_dn == '\0') {
        g_free(ab_ldap->base_dn);
        ab_ldap->base_dn = NULL;
    }

    ab_ldap->bind_dn = libbalsa_conf_private_get_string("BindDN", FALSE);
    if (ab_ldap->bind_dn != NULL && *ab_ldap->bind_dn == '\0') {
	g_free(ab_ldap->bind_dn);
        ab_ldap->bind_dn = NULL;
    }

	if (ab_ldap->bind_dn != NULL) {
		libbalsa_address_book_ldap_load_passwd(ab_ldap);
	} else {
		ab_ldap->passwd = NULL;
	}

    ab_ldap->priv_book_dn = libbalsa_conf_get_string("BookDN");
    if (ab_ldap->priv_book_dn != NULL && *ab_ldap->priv_book_dn == '\0') {
	g_free(ab_ldap->priv_book_dn);
        ab_ldap->priv_book_dn = NULL;
    }
    ab_ldap->enable_tls = libbalsa_conf_get_bool("EnableTLS");

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config(ab, prefix);

    if (libbalsa_address_book_get_is_expensive(ab) < 0)
        libbalsa_address_book_set_is_expensive(ab, TRUE);
}


static void
libbalsa_address_book_ldap_store_passwd(LibBalsaAddressBookLdap *ab_ldap)
{
#if defined(HAVE_LIBSECRET)
	if (libbalsa_conf_use_libsecret()) {
		GError *error = NULL;

		secret_password_store_sync(&ldap_schema, NULL, _("Balsa passwords"), ab_ldap->passwd, NULL, &error,
			"server", ab_ldap->host,
			"user", ab_ldap->bind_dn,
			NULL);
		if (error != NULL) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				/* Translators: #1 address book name; #2 error message */
				_("Error saving credentials for address book “%s” in Secret Service: %s"),
				libbalsa_address_book_get_name(LIBBALSA_ADDRESS_BOOK(ab_ldap)), error->message);
			g_error_free(error);
		} else {
			libbalsa_conf_clean_key("Passwd");
		}
	} else {
		libbalsa_conf_private_set_string("Passwd", ab_ldap->passwd, TRUE);
	}
#else		/* !HAVE_LIBSECRET */
	libbalsa_conf_private_set_string("Passwd", ab_ldap->passwd, TRUE);
#endif
}


static void
libbalsa_address_book_ldap_load_passwd(LibBalsaAddressBookLdap *ab_ldap)
{
#if defined(HAVE_LIBSECRET)
	if (libbalsa_conf_use_libsecret()) {
		GError *error = NULL;

		ab_ldap->passwd = secret_password_lookup_sync(&ldap_schema, NULL, &error,
			"server", ab_ldap->host,
			"user", ab_ldap->bind_dn,
			NULL);
		if (error != NULL) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				/* Translators: #1 address book name; #2 error message */
				_("Error loading credentials for address book “%s” from Secret Service: %s"),
				libbalsa_address_book_get_name(LIBBALSA_ADDRESS_BOOK(ab_ldap)), error->message);
			g_error_free(error);
		}

		/* check the config file if the returned password is NULL, make sure to remove it from the config file otherwise */
		if (ab_ldap->passwd == NULL) {
			ab_ldap->passwd = libbalsa_conf_private_get_string("Passwd", TRUE);
			if (ab_ldap->passwd != NULL) {
				libbalsa_address_book_ldap_store_passwd(ab_ldap);
			}
		} else {
			libbalsa_conf_clean_key("Passwd");
		}
	} else {
		ab_ldap->passwd = libbalsa_conf_private_get_string("Passwd", TRUE);
	}
#else
	ab_ldap->passwd = libbalsa_conf_private_get_string("Passwd", TRUE);
#endif		/* HAVE_LIBSECRET */

	/* reset empty password to NULL */
	if ((ab_ldap->passwd != NULL) && (*ab_ldap->passwd == '\0')) {
#if defined(HAVE_LIBSECRET)
		secret_password_free(ab_ldap->passwd);
#else
		g_free(ab_ldap->passwd);
#endif
		ab_ldap->passwd = NULL;
	}
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

    new = g_new(gchar, (strlen(raw) * 3) + 1);
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
    g_free(new);
    return str;
}


static GList *
libbalsa_address_book_ldap_alias_complete(LibBalsaAddressBook * ab,
					  const gchar * prefix)
{
    static struct timeval timeout = { 15, 0 }; /* 15 sec timeout */
    LibBalsaAddressBookLdap *ab_ldap;
    GList *res = NULL;
    gchar* filter;
    gchar* ldap;
    int rc;
    LDAPMessage * e, *result;

    ab_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    if (!libbalsa_address_book_get_expand_aliases(ab) || strlen(prefix) < ABL_MIN_LEN)
        return NULL;

    if (ab_ldap->directory == NULL) {
        if (libbalsa_address_book_ldap_open_connection(ab_ldap) != LDAP_SUCCESS)
	    return NULL;
    }

    /*
     * Attempt to search for e-mail addresses.  It returns success
     * or failure, but not all the matches.
     */
    ldap = rfc_2254_escape(prefix);

    filter = g_strdup_printf("(&(objectClass=organizationalPerson)(mail=*)"
                             "(|(cn=%s*)(sn=%s*)(mail=%s*@*)))",
			     ldap, ldap, ldap);
    g_free(ldap);
    result = NULL;
    rc = ldap_search_ext_s(ab_ldap->directory, ldap_use_config_value(ab_ldap->base_dn),
                           LDAP_SCOPE_SUBTREE, filter, complete_attrs, 0, 
                           NULL, NULL, &timeout, ABL_SIZE_LIMIT_LOOKUP,
                           &result);
    g_debug("Sent LDAP request: %s (basedn=%s) res=0x%x",
    		filter, ab_ldap->base_dn, rc);
    g_free(filter);
    switch (rc) {
    case LDAP_SUCCESS:
    case LDAP_PARTIAL_RESULTS:
	if (result)
	    for(e = ldap_first_entry(ab_ldap->directory, result);
		e != NULL; e = ldap_next_entry(ab_ldap->directory, e)) {
		res = lbabl_get_internet_address(res, ab_ldap->directory, e);
	    }
    case LDAP_SIZELIMIT_EXCEEDED:
    case LDAP_TIMELIMIT_EXCEEDED:
	/*
	 * These are administrative limits, so don't warn about them.
	 * Particularly SIZELIMIT can be nasty on big directories.
	 */
	break;
    case LDAP_SERVER_DOWN: {
    	gchar *uri;

    	uri = ldap_connection_get_uri(ab_ldap->directory);
        libbalsa_address_book_ldap_close_connection(ab_ldap);
        libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
        		_("LDAP server %s down, next attempt will try to reconnect."),
				uri);
        ldap_memfree(uri);
    }
        break;
    default:
	/*
	 * Until we know for sure, complain about all other errors.
	 */
	g_debug("alias_complete::ldap_search_st: %s",
                ldap_err2string(rc));
	break;
    }

    g_debug("ldap_alias_complete:: result=%p", result);
    if(result) ldap_msgfree(result);

    if(res) res = g_list_reverse(res);

    return res;
}

/*
 * Getters
 */

const gchar *
libbalsa_address_book_ldap_get_host(LibBalsaAddressBookLdap * ab_ldap)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap), NULL);

    return ab_ldap->host;
}

const gchar *
libbalsa_address_book_ldap_get_base_dn(LibBalsaAddressBookLdap * ab_ldap)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap), NULL);

    return ab_ldap->base_dn;
}

const gchar *
libbalsa_address_book_ldap_get_bind_dn(LibBalsaAddressBookLdap * ab_ldap)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap), NULL);

    return ab_ldap->bind_dn;
}

const gchar *
libbalsa_address_book_ldap_get_passwd(LibBalsaAddressBookLdap * ab_ldap)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap), NULL);

    return ab_ldap->passwd;
}

const gchar *
libbalsa_address_book_ldap_get_book_dn(LibBalsaAddressBookLdap * ab_ldap)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap), NULL);

    return ab_ldap->priv_book_dn;
}

gboolean
libbalsa_address_book_ldap_get_enable_tls(LibBalsaAddressBookLdap * ab_ldap)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap), FALSE);

    return ab_ldap->enable_tls;
}

/*
 * Setters
 */

void
libbalsa_address_book_ldap_set_host(LibBalsaAddressBookLdap * ab_ldap,
                                    const gchar             * host)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap));

    g_free(ab_ldap->host);
    ab_ldap->host = g_strdup(host);
}

void
libbalsa_address_book_ldap_set_base_dn(LibBalsaAddressBookLdap * ab_ldap,
                                       const gchar             * base_dn)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap));

    g_free(ab_ldap->base_dn);
    ab_ldap->base_dn = g_strdup(base_dn);
}

void
libbalsa_address_book_ldap_set_bind_dn(LibBalsaAddressBookLdap * ab_ldap,
                                       const gchar             * bind_dn)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap));

    g_free(ab_ldap->bind_dn);
    ab_ldap->bind_dn = g_strdup(bind_dn);
}

void
libbalsa_address_book_ldap_set_passwd(LibBalsaAddressBookLdap * ab_ldap,
                                      const gchar             * passwd)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap));

#if defined(HAVE_LIBSECRET)
    secret_password_free(ab_ldap->passwd);
#else
    g_free(ab_ldap->passwd);
#endif
    ab_ldap->passwd = g_strdup(passwd);
}

void
libbalsa_address_book_ldap_set_book_dn(LibBalsaAddressBookLdap * ab_ldap,
                                       const gchar             * book_dn)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap));

    g_free(ab_ldap->priv_book_dn);
    ab_ldap->priv_book_dn = g_strdup(book_dn);
}

void
libbalsa_address_book_ldap_set_enable_tls(LibBalsaAddressBookLdap * ab_ldap,
                                          gboolean                  enable_tls)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab_ldap));

    ab_ldap->enable_tls = !!enable_tls;
}

#endif				/*LDAP_ENABLED */
