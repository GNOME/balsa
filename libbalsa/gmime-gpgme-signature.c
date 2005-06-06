/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme glue layer library
 * Copyright (C) 2004 Albrecht Dreﬂ <albrecht.dress@arcor.de>
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

#include <gpgme.h>
#include <string.h>
#include <glib.h>
#include "gmime-gpgme-context.h"
#include "gmime-gpgme-signature.h"


/* stuff for the signature status as returned by gpgme as an GObject */
static GObjectClass *g_mime_gpgme_sigstat_parent_class = NULL;

static void g_mime_gpgme_sigstat_class_init(GMimeGpgmeSigstatClass *
					    klass);
static void g_mime_gpgme_sigstat_finalize(GMimeGpgmeSigstat * self);
static void g_mime_gpgme_sigstat_init(GMimeGpgmeSigstat * self);
static gchar *fix_EMail_info(gchar * str);


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
    gpgme_key_t key;
    gpgme_subkey_t subkey;
    gpgme_user_id_t uid;
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

    /* try to get the related key */
    err = gpgme_get_key(ctx, sig_stat->fingerprint, &key, 0);
    if (err != GPG_ERR_NO_ERROR)
         return sig_stat;
    if (key == NULL)
	return sig_stat;

    /* the key is available */
    sig_stat->protocol = key->protocol;
    sig_stat->issuer_serial = g_strdup(key->issuer_serial);
    sig_stat->issuer_name = g_strdup(key->issuer_name);
    sig_stat->chain_id = g_strdup(key->chain_id);
    sig_stat->trust = key->owner_trust;
    uid = key->uids;

    /* Note: there is no way to determine which user id has been used to
     * create the signature. We therefore pick the validity of the primary
     * one and scan uid's to get useable name, email and uid strings */
    sig_stat->validity = uid->validity;
    while (uid) {
	if (!sig_stat->sign_name && uid->name && strlen(uid->name))
	    sig_stat->sign_name = g_strdup(uid->name);
	if (!sig_stat->sign_email && uid->email && strlen(uid->email))
	    sig_stat->sign_email = g_strdup(uid->email);
	if (!sig_stat->sign_uid && uid->uid && strlen(uid->uid))
	    sig_stat->sign_uid = fix_EMail_info(g_strdup(uid->uid));
	uid = uid->next;
    }

    /* get the subkey which can sign */
    subkey = key->subkeys;
    while (subkey && !subkey->can_sign)
	subkey = subkey->next;
    if (subkey) {
	sig_stat->key_created = subkey->timestamp;
	sig_stat->key_expires = subkey->expires;
	sig_stat->key_revoked = subkey->revoked;
	sig_stat->key_expired = subkey->expired;
	sig_stat->key_disabled = subkey->disabled;
	sig_stat->key_invalid = subkey->invalid;
    }
    gpgme_key_unref(key);

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
    g_free(self->sign_name);
    self->sign_name = NULL;
    g_free(self->sign_email);
    self->sign_email = NULL;
    g_free(self->fingerprint);
    self->fingerprint = NULL;
    g_free(self->sign_uid);
    self->sign_uid = NULL;
    g_free(self->issuer_serial);
    self->issuer_serial = NULL;
    g_free(self->issuer_name);
    self->issuer_name = NULL;
    g_free(self->chain_id);
    self->chain_id = NULL;

    g_mime_gpgme_sigstat_parent_class->finalize(G_OBJECT(self));
}


static void
g_mime_gpgme_sigstat_init(GMimeGpgmeSigstat * self)
{
    self->status = GPG_ERR_NOT_SIGNED;
    self->sign_name = NULL;
    self->sign_email = NULL;
    self->fingerprint = NULL;
    self->sign_uid = NULL;
    self->issuer_serial = NULL;
    self->issuer_name = NULL;
    self->chain_id = NULL;
}


/*
 * Change an EMail field in a S/MIME certificate to human-readable text.
 * Note: doesn't do any sophisticated error-checking...
 */
static gchar *
fix_EMail_info(gchar * str)
{
    gchar *p = strstr(str, "1.2.840.113549.1.9.1=#");
    GString *result;

    if (!p)
	return str;

    *p = '\0';
    p += 22;
    result = g_string_new(str);
    result = g_string_append(result, "EMail=");
    while (*p != '\0' && *p != ',') {
	gchar x = 0;

	if (*p >= 'A' && *p <= 'F')
	    x = (*p - 'A' + 10) << 4;
	else if (*p >= 'a' && *p <= 'f')
	    x = (*p - 'a' + 10) << 4;
	else if (*p >= '0' && *p <= '9')
	    x = (*p - '0') << 4;
	p++;
	if (*p != '\0' && *p != ',') {
	    if (*p >= 'A' && *p <= 'F')
		x += *p - 'A' + 10;
	    else if (*p >= 'a' && *p <= 'f')
		x += *p - 'a' + 10;
	    else if (*p >= '0' && *p <= '9')
		x += *p - '0';
	    p++;
	}
	result = g_string_append_c(result, x);
    }
    result = g_string_append(result, p);
    g_free(str);
    p = result->str;
    g_string_free(result, FALSE);
    return p;
}
