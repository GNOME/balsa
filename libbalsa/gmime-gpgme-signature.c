/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme glue layer library
 * Copyright (C) 2004-2013 Albrecht Dreß <albrecht.dress@arcor.de>
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <ctype.h>
#include <gpgme.h>
#include <string.h>
#include <glib.h>
#include "libbalsa-gpgme.h"
#include "misc.h"
#include "gmime-gpgme-signature.h"


/* stuff for the signature status as returned by gpgme as an GObject */
static GObjectClass *g_mime_gpgme_sigstat_parent_class = NULL;

static void g_mime_gpgme_sigstat_class_init(GMimeGpgmeSigstatClass *
					    klass);
static void g_mime_gpgme_sigstat_finalize(GMimeGpgmeSigstat * self);
static void g_mime_gpgme_sigstat_init(GMimeGpgmeSigstat * self);


/* GMimeGpgmeSigstat related stuff */
GType
g_mime_gpgme_sigstat_get_type(void)
{
    static GType g_mime_gpgme_sigstat_type = 0;

    if (!g_mime_gpgme_sigstat_type) {
	static const GTypeInfo g_mime_gpgme_sigstat_info = {
	    sizeof(GMimeGpgmeSigstatClass),	/* class_size */
	    NULL,		/* base_init */
	    NULL,		/* base_finalize */
	    (GClassInitFunc) g_mime_gpgme_sigstat_class_init,	/* class_init */
	    NULL,		/* class_finalize */
	    NULL,		/* class_data */
	    sizeof(GMimeGpgmeSigstat),	/* instance_size */
	    0,			/* n_preallocs */
	    (GInstanceInitFunc) g_mime_gpgme_sigstat_init,	/* instance_init */
	    /* no value_table */
	};

	g_mime_gpgme_sigstat_type =
	    g_type_register_static(G_TYPE_OBJECT, "GMimeGpgmeSigstat",
				   &g_mime_gpgme_sigstat_info, 0);
    }

    return g_mime_gpgme_sigstat_type;
}


GMimeGpgmeSigstat *
g_mime_gpgme_sigstat_new(void)
{

    return
	GMIME_GPGME_SIGSTAT(g_object_new(GMIME_TYPE_GPGME_SIGSTAT, NULL));
}


GMimeGpgmeSigstat *
g_mime_gpgme_sigstat_new_from_gpgme_ctx(gpgme_ctx_t ctx)
{
    GMimeGpgmeSigstat *sig_stat;
    gpgme_verify_result_t result;
    gpgme_error_t err;

    g_return_val_if_fail(ctx, NULL);
    if (!(sig_stat = g_mime_gpgme_sigstat_new()))
	return NULL;

    sig_stat->status = GPG_ERR_NOT_SIGNED;	/* no signature available */
    sig_stat->protocol = gpgme_get_protocol(ctx);

    /* try to retreive the result of a verify operation */
    if (!(result = gpgme_op_verify_result(ctx))
	|| result->signatures == NULL)
	return sig_stat;

    /* there is at least one signature */
    sig_stat->fingerprint = g_strdup(result->signatures->fpr);
    sig_stat->sign_time = result->signatures->timestamp;
    sig_stat->status = gpgme_err_code(result->signatures->status);
    sig_stat->validity = result->signatures->validity;

    /* try to get the related key */
    err = gpgme_get_key(ctx, sig_stat->fingerprint, &sig_stat->key, 0);
    if (err != GPG_ERR_NO_ERROR) {
    	gchar errbuf[4096];		/* should be large enough... */

    	gpgme_strerror_r(err, errbuf, sizeof(errbuf));
    	g_message("could not retrieve the key with fingerprint %s: %s: %s",
    		sig_stat->fingerprint, gpgme_strsource(err), errbuf);
    }

    return sig_stat;
}


static void
g_mime_gpgme_sigstat_class_init(GMimeGpgmeSigstatClass * klass)
{
    GObjectClass *gobject_klass = G_OBJECT_CLASS(klass);
    g_mime_gpgme_sigstat_parent_class = g_type_class_peek(G_TYPE_OBJECT);

    gobject_klass->finalize =
	(GObjectFinalizeFunc) g_mime_gpgme_sigstat_finalize;
}

static void
g_mime_gpgme_sigstat_finalize(GMimeGpgmeSigstat * self)
{
    g_free(self->fingerprint);
    self->fingerprint = NULL;
    if (self->key)
	gpgme_key_unref(self->key);
    self->key = NULL;

    g_mime_gpgme_sigstat_parent_class->finalize(G_OBJECT(self));
}

static gchar *
hex_decode(const gchar *hexstr)
{
    gchar *result;
    gchar *outp;
    int inlen;

    inlen = strlen(hexstr);
    if ((inlen & 1) == 1)
        return g_strdup(hexstr);

    result = g_new0(gchar, (inlen >> 1) + 1);
    for (outp = result; *hexstr; outp++) {
        if (isdigit(*hexstr))
            *outp = (*hexstr - '0') << 4;
        else
            *outp = (toupper(*hexstr) -'A' + 10) << 4;
        hexstr++;
        if (isdigit(*hexstr))
            *outp |= *hexstr - '0';
        else
            *outp |= toupper(*hexstr) -'A' + 10;
        hexstr++;
    }
    return result;
}

/*
 * Change some fields in a S/MIME certificate to human-readable text.
 * Note: doesn't do any sophisticated error-checking...
 */
gchar *
libbalsa_cert_subject_readable(const gchar *subject)
{
    const struct {
        gchar *ldap_id;
        gchar *readable;
    } ldap_id_list[] = {
        { .ldap_id = "2.5.4.4", .readable = "sn" },
        { .ldap_id = "2.5.4.5", .readable = "serialNumber" },
        { .ldap_id = "2.5.4.42", .readable = "givenName" },
        { .ldap_id = "1.2.840.113549.1.9.1", .readable = "email" },
        { .ldap_id = NULL, .readable = NULL }
    }, *ldap_elem;
    gchar **elements;
    gint n;
    GString *result;
    gchar *readable_subject;

    if (!subject)
        return NULL;

    result = g_string_new(NULL);
    elements = g_strsplit(subject, ",", -1);
    for (n = 0; elements[n]; n++) {
        gchar *equals;

        equals = strchr(elements[n], '=');
        if (equals) {
            *equals++ = '\0';
            for (ldap_elem = ldap_id_list;
                 ldap_elem->ldap_id && strcmp(ldap_elem->ldap_id, elements[n]);
                 ldap_elem++);
            if (ldap_elem->ldap_id)
                result = g_string_append(result, ldap_elem->readable);
            else
                result = g_string_append(result, elements[n]);
            result = g_string_append_c(result, '=');
            
            if (*equals == '#') {
                gchar *decoded;

                decoded = hex_decode(equals + 1);
                result = g_string_append(result, decoded);
                g_free(decoded);
            } else
                result = g_string_append(result, equals);
        } else
            result = g_string_append(result, elements[n]);
        if (elements[n + 1])
            result = g_string_append_c(result, ',');
    }
    g_strfreev(elements);
    readable_subject = g_string_free(result, FALSE);
    libbalsa_utf8_sanitize(&readable_subject, TRUE, NULL);
    return readable_subject;
}

static void
g_mime_gpgme_sigstat_init(GMimeGpgmeSigstat * self)
{
    self->status = GPG_ERR_NOT_SIGNED;
    self->key = NULL;
    self->fingerprint = NULL;
}
