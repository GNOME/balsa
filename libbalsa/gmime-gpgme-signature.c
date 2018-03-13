/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme glue layer library
 * Copyright (C) 2004-2018 Albrecht Dreß <albrecht.dress@arcor.de>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
#   include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <ctype.h>
#include <gpgme.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include "libbalsa-gpgme.h"
#include "misc.h"
#include "libbalsa.h"
#include "libbalsa-gpgme-keys.h"
#include "libbalsa-gpgme-widgets.h"
#include "gmime-gpgme-signature.h"

#ifdef G_LOG_DOMAIN
#   undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"


/* stuff for the signature status as returned by gpgme as an GObject */

static void g_mime_gpgme_sigstat_class_init(GMimeGpgmeSigstatClass *klass);
static void g_mime_gpgme_sigstat_init(GMimeGpgmeSigstat *sigstat);
static void g_mime_gpgme_sigstat_finalize(GMimeGpgmeSigstat *sigstat);

/* GMimeGpgmeSigstat related stuff */

struct _GMimeGpgmeSigstat {
    GObject parent;

    /* results form gpgme's verify operation */
    gpgme_protocol_t protocol;
    gpgme_sigsum_t summary;
    gpgme_error_t status;
    gpgme_validity_t validity;
    gchar *fingerprint;
    time_t sign_time;

    /* information about the key used to create the signature */
    gpgme_key_t key;
};

struct _GMimeGpgmeSigstatClass {
    GObjectClass parent;
};

G_DEFINE_TYPE(GMimeGpgmeSigstat, g_mime_gpgme_sigstat, G_TYPE_OBJECT)

static void
g_mime_gpgme_sigstat_init(GMimeGpgmeSigstat *sigstat)
{
}


GMimeGpgmeSigstat *
g_mime_gpgme_sigstat_new(gpgme_ctx_t ctx)
{
    GMimeGpgmeSigstat *sigstat;

    sigstat = GMIME_GPGME_SIGSTAT(g_object_new(GMIME_TYPE_GPGME_SIGSTAT, NULL));
    sigstat->protocol = gpgme_get_protocol(ctx);
    sigstat->status = GPG_ERR_NOT_SIGNED;

    return sigstat;
}


GMimeGpgmeSigstat *
g_mime_gpgme_sigstat_new_from_gpgme_ctx(gpgme_ctx_t ctx)
{
    GMimeGpgmeSigstat *sigstat;
    gpgme_verify_result_t result;

    g_return_val_if_fail(ctx, NULL);
    sigstat = g_mime_gpgme_sigstat_new(ctx);

    /* try to retrieve the result of a verify operation */
    result = gpgme_op_verify_result(ctx);
    if ((result != NULL) && (result->signatures != NULL)) {
        /* there is at least one signature */
        sigstat->fingerprint = g_strdup(result->signatures->fpr);
        sigstat->sign_time = result->signatures->timestamp;
        sigstat->summary = result->signatures->summary;
        sigstat->status = gpgme_err_code(result->signatures->status);
        sigstat->validity = result->signatures->validity;
    }

    return sigstat;
}


void
g_mime_gpgme_sigstat_load_key(GMimeGpgmeSigstat *sigstat)
{
    g_return_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat));

    if ((sigstat->key == NULL) && ((sigstat->summary & GPGME_SIGSUM_KEY_MISSING) == 0)) {
        gpgme_ctx_t ctx;

        ctx = libbalsa_gpgme_new_with_proto(sigstat->protocol, NULL, NULL, NULL);
        sigstat->key = libbalsa_gpgme_load_key(ctx, sigstat->fingerprint, NULL);
        gpgme_release(ctx);
    }
}


const gchar *
g_mime_gpgme_sigstat_protocol_name(GMimeGpgmeSigstat *sigstat)
{
    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), NULL);

    switch (sigstat->protocol) {
    case GPGME_PROTOCOL_OpenPGP:
        return _("PGP signature: ");

    case GPGME_PROTOCOL_CMS:
        return _("S/MIME signature: ");

    default:
        return _("(unknown protocol) ");
    }
}


static inline void
append_time_t(GString     *str,
              const gchar *format,
              time_t       when,
              const gchar *date_string)
{
    if (when != (time_t) 0) {
        gchar *tbuf = libbalsa_date_to_utf8(when, date_string);
        g_string_append_printf(str, format, tbuf);
        g_free(tbuf);
    } else {
        g_string_append_printf(str, format, _("never"));
    }
}


gchar *
g_mime_gpgme_sigstat_to_gchar(GMimeGpgmeSigstat *sigstat,
                              gboolean           full_details,
                              const gchar       *date_string)
{
    GString *msg;

    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), NULL);
    g_return_val_if_fail(date_string != NULL, NULL);

    msg = g_string_new(g_mime_gpgme_sigstat_protocol_name(sigstat));
    msg = g_string_append(msg, libbalsa_gpgme_sig_stat_to_gchar(sigstat->status));
    g_string_append_printf(msg, _("\nSignature validity: %s"),
                           libbalsa_gpgme_validity_to_gchar(sigstat->validity));
    append_time_t(msg, _("\nSigned on: %s"), sigstat->sign_time, date_string);
    if (sigstat->fingerprint != NULL) {
        g_string_append_printf(msg, _("\nKey fingerprint: %s"), sigstat->fingerprint);
    }

    /* append key data */
    if (full_details && (sigstat->key != NULL)) {
        gchar *key_data;

        key_data = libbalsa_gpgme_key_to_gchar(sigstat->key, sigstat->fingerprint);
        g_string_append_printf(msg, "\n%s", key_data);
        g_free(key_data);
    }

    return g_string_free(msg, FALSE);
}


static void
g_mime_gpgme_sigstat_class_init(GMimeGpgmeSigstatClass *klass)
{
    GObjectClass *gobject_klass = G_OBJECT_CLASS(klass);

    gobject_klass->finalize =
        (GObjectFinalizeFunc) g_mime_gpgme_sigstat_finalize;
}


static void
g_mime_gpgme_sigstat_finalize(GMimeGpgmeSigstat *sigstat)
{
    g_free(sigstat->fingerprint);
    if (sigstat->key) {
        gpgme_key_unref(sigstat->key);
    }

    G_OBJECT_CLASS(g_mime_gpgme_sigstat_parent_class)->finalize(G_OBJECT(sigstat));
}


static gchar *
hex_decode(const gchar *hexstr)
{
    gchar *result;
    gchar *outp;
    int inlen;

    inlen = strlen(hexstr);
    if ((inlen & 1) == 1) {
        return g_strdup(hexstr);
    }

    result = g_new0(gchar, (inlen >> 1) + 1);
    for (outp = result; *hexstr; outp++) {
        if (isdigit(*hexstr)) {
            *outp = (*hexstr - '0') << 4;
        } else {
            *outp = (toupper(*hexstr) - 'A' + 10) << 4;
        }
        hexstr++;
        if (isdigit(*hexstr)) {
            *outp |= *hexstr - '0';
        } else {
            *outp |= toupper(*hexstr) - 'A' + 10;
        }
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

    if (!subject) {
        return NULL;
    }

    result = g_string_new(NULL);
    elements = g_strsplit(subject, ",", -1);
    for (n = 0; elements[n]; n++) {
        gchar *equals;

        equals = strchr(elements[n], '=');
        if (equals) {
            *equals++ = '\0';
            for (ldap_elem = ldap_id_list;
                 ldap_elem->ldap_id && strcmp(ldap_elem->ldap_id, elements[n]);
                 ldap_elem++) {
            }
            if (ldap_elem->ldap_id) {
                result = g_string_append(result, ldap_elem->readable);
            } else {
                result = g_string_append(result, elements[n]);
            }
            result = g_string_append_c(result, '=');

            if (*equals == '#') {
                gchar *decoded;

                decoded = hex_decode(equals + 1);
                result = g_string_append(result, decoded);
                g_free(decoded);
            } else {
                result = g_string_append(result, equals);
            }
        } else {
            result = g_string_append(result, elements[n]);
        }
        if (elements[n + 1]) {
            result = g_string_append_c(result, ',');
        }
    }
    g_strfreev(elements);
    readable_subject = g_string_free(result, FALSE);
    libbalsa_utf8_sanitize(&readable_subject, TRUE, NULL);
    return readable_subject;
}


/*
 * Getters
 */

gpgme_error_t
g_mime_gpgme_sigstat_get_status(GMimeGpgmeSigstat *sigstat)
{
    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), (gpgme_error_t) 0);

    return sigstat->status;
}


gpgme_sigsum_t
g_mime_gpgme_sigstat_get_summary(GMimeGpgmeSigstat *sigstat)
{
    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), (gpgme_sigsum_t) 0);

    return sigstat->summary;
}


gpgme_protocol_t
g_mime_gpgme_sigstat_get_protocol(GMimeGpgmeSigstat *sigstat)
{
    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), (gpgme_protocol_t) 0);

    return sigstat->protocol;
}


gpgme_key_t
g_mime_gpgme_sigstat_get_key(GMimeGpgmeSigstat *sigstat)
{
    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), NULL);

    return sigstat->key;
}


const gchar *
g_mime_gpgme_sigstat_get_fingerprint(GMimeGpgmeSigstat *sigstat)
{
    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), NULL);

    return sigstat->fingerprint;
}


/*
 * Setter
 */

void
g_mime_gpgme_sigstat_set_status(GMimeGpgmeSigstat *sigstat,
                                gpgme_error_t      status)
{
    g_return_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat));

    sigstat->status = status;
}
