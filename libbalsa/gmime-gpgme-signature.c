/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme glue layer library
 * Copyright (C) 2004-2016 Albrecht Dreß <albrecht.dress@arcor.de>
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <ctype.h>
#include <gpgme.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gnutls/x509.h>
#include "libbalsa-gpgme.h"
#include "misc.h"
#include "libbalsa.h"
#include "libbalsa-gpgme-keys.h"
#include "libbalsa-gpgme-widgets.h"
#include "gmime-gpgme-signature.h"

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"


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


G_DEFINE_TYPE(GMimeGpgmeSigstat, g_mime_gpgme_sigstat, G_TYPE_OBJECT)


/* stuff for the signature status as returned by gpgme as an GObject */
static void g_mime_gpgme_sigstat_finalize(GObject *object);

static gchar *cert_subject_cn_mail(const gchar *subject)
	G_GNUC_WARN_UNUSED_RESULT;


/* GMimeGpgmeSigstat related stuff */

GMimeGpgmeSigstat *
g_mime_gpgme_sigstat_new(gpgme_ctx_t ctx)
{
	GMimeGpgmeSigstat *result;

	result = GMIME_GPGME_SIGSTAT(g_object_new(GMIME_TYPE_GPGME_SIGSTAT, NULL));
	result->protocol = gpgme_get_protocol(ctx);
	result->status = GPG_ERR_NOT_SIGNED;
    return result;
}


GMimeGpgmeSigstat *
g_mime_gpgme_sigstat_new_from_gpgme_ctx(gpgme_ctx_t ctx)
{
    GMimeGpgmeSigstat *sig_stat;
    gpgme_verify_result_t result;

    g_return_val_if_fail(ctx, NULL);
    sig_stat = g_mime_gpgme_sigstat_new(ctx);

    /* try to retrieve the result of a verify operation */
    result = gpgme_op_verify_result(ctx);
    if ((result != NULL) && (result->signatures != NULL)) {
        /* There is at least one signature - note that multiple signatures
         * are never created by a MUA and may indicate an attack, see
         * https://github.com/RUB-NDS/Johnny-You-Are-Fired/blob/master/paper/johnny-fired.pdf */
    	if (result->signatures->next != NULL) {
    		sig_stat->status = GPG_ERR_MULT_SIGNATURES;
    	} else {
			sig_stat->fingerprint = g_strdup(result->signatures->fpr);
			sig_stat->sign_time = result->signatures->timestamp;
			sig_stat->summary = result->signatures->summary;
			sig_stat->status = gpgme_err_code(result->signatures->status);
			sig_stat->validity = result->signatures->validity;

			/* load the key unless it is known to be missing, using a different context */
			if ((sig_stat->summary & GPGME_SIGSUM_KEY_MISSING) == 0) {
				gpgme_ctx_t key_ctx;
				GError *error = NULL;

				key_ctx = libbalsa_gpgme_new_with_proto(sig_stat->protocol, &error);
				if (key_ctx != NULL) {
					sig_stat->key = libbalsa_gpgme_load_key(key_ctx, sig_stat->fingerprint, &error);
					gpgme_release(key_ctx);
				}
				if (error != NULL) {
					g_info("%s: error loading key with fp %s: %s", __func__, sig_stat->fingerprint, error->message);
					g_clear_error(&error);
				}
			}
    	}
    }

	return sig_stat;
}

void
g_mime_gpgme_sigstat_set_status(GMimeGpgmeSigstat *sigstat,
								gpgme_error_t      status)
{
    g_return_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat));
    sigstat->status = status;
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
g_mime_gpgme_sigstat_info(GMimeGpgmeSigstat *info,
						  gboolean           with_signer)
{
	gchar *signer_str = NULL;
	gchar *status_str;
	gchar *res_msg;

    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(info), NULL);

    if (with_signer) {
    	signer_str = g_mime_gpgme_sigstat_signer(info);
    }
    status_str = libbalsa_gpgme_sig_stat_to_gchar(info->status);
    if (signer_str != NULL) {
		/* Translators: #1 signature crypto protocol; #2 signer; #3 status message */
    	res_msg = g_strdup_printf(_("%s signature of “%s”: %s"), libbalsa_gpgme_protocol_name(info->protocol),
    			signer_str, status_str);
    	g_free(signer_str);
    } else {
		/* Translators: #1 signature crypto protocol; #2 status message */
    	res_msg = g_strdup_printf(_("%s signature: %s"), libbalsa_gpgme_protocol_name(info->protocol), status_str);
    }
    g_free(status_str);
    return res_msg;
}

gchar *
g_mime_gpgme_sigstat_to_gchar( GMimeGpgmeSigstat *info,
							  gboolean            full_details,
				 	 	 	  const gchar        *date_string)
{
    GString *msg;
    gchar *status_str;

    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(info), NULL);
    g_return_val_if_fail(date_string != NULL, NULL);
    msg = g_string_new(libbalsa_gpgme_protocol_name(info->protocol));
    status_str = libbalsa_gpgme_sig_stat_to_gchar(info->status);
    g_string_append_printf(msg, _(" signature: %s"), status_str);
    g_free(status_str);
    if ((info->status != GPG_ERR_BAD_SIGNATURE) && (info->status != GPG_ERR_NO_DATA)) {
    	if (info->status != GPG_ERR_NO_PUBKEY) {
    		g_string_append_printf(msg, _("\nSignature validity: %s"), libbalsa_gpgme_validity_to_gchar(info->validity));
    	}
    	if (info->sign_time != (time_t) 0) {
    		append_time_t(msg, _("\nSigned on: %s"), info->sign_time, date_string);
    	}
    }
    if (info->fingerprint) {
    	g_string_append_printf(msg, _("\nKey fingerprint: %s"), info->fingerprint);
    }

    /* append key data */
    if (full_details && (info->key != NULL)) {
    	gchar *key_data;

    	key_data = libbalsa_gpgme_key_to_gchar(info->key, info->fingerprint);
    	g_string_append_printf(msg, "\n%s", key_data);
    	g_free(key_data);
    }

    return g_string_free(msg, FALSE);
}


gchar *
g_mime_gpgme_sigstat_signer(GMimeGpgmeSigstat *sigstat)
{
	gchar *result = NULL;

    g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), NULL);

    if (sigstat->key != NULL) {
    	gpgme_user_id_t use_uid;

    	/* skip revoked and invalid uid's... */
    	use_uid = sigstat->key->uids;
    	while ((use_uid != NULL) && ((use_uid->invalid != 0) || (use_uid->revoked != 0))) {
    		use_uid = use_uid->next;
    	}

    	/* ...but fall back to the first if we didn't find one */
    	if (use_uid == NULL) {
    		use_uid = sigstat->key->uids;
    	}

    	if ((use_uid == NULL) || (use_uid->uid == NULL)) {
    		result = g_strdup(_("unknown"));
    	} else if (sigstat->protocol == GPGME_PROTOCOL_CMS) {
    		result = cert_subject_cn_mail(use_uid->uid);
    	} else {
    		result = g_strdup(use_uid->uid);
    	}
    }

    return result;
}


gpgme_protocol_t
g_mime_gpgme_sigstat_protocol(GMimeGpgmeSigstat *sigstat)
{
	g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), GPGME_PROTOCOL_UNKNOWN);
	return sigstat->protocol;
}


gpgme_error_t
g_mime_gpgme_sigstat_status(GMimeGpgmeSigstat *sigstat)
{
	g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), GPG_ERR_GENERAL);
	return sigstat->status;
}


gpgme_sigsum_t
g_mime_gpgme_sigstat_summary(GMimeGpgmeSigstat *sigstat)
{
	g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), GPGME_SIGSUM_SYS_ERROR);
	return sigstat->summary;
}


gpgme_key_t
g_mime_gpgme_sigstat_key(GMimeGpgmeSigstat *sigstat)
{
	g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), NULL);
	return sigstat->key;
}


const gchar *
g_mime_gpgme_sigstat_fingerprint(GMimeGpgmeSigstat *sigstat)
{
	g_return_val_if_fail(GMIME_IS_GPGME_SIGSTAT(sigstat), NULL);
	return sigstat->fingerprint;
}


static void
g_mime_gpgme_sigstat_class_init(GMimeGpgmeSigstatClass * klass)
{
    GObjectClass *gobject_klass = G_OBJECT_CLASS(klass);

    gobject_klass->finalize = g_mime_gpgme_sigstat_finalize;
}

static void
g_mime_gpgme_sigstat_init(G_GNUC_UNUSED GMimeGpgmeSigstat *self)
{
	/* nothing to do */
}

static void
g_mime_gpgme_sigstat_finalize(GObject *object)
{
	GMimeGpgmeSigstat *self = GMIME_GPGME_SIGSTAT(object);
	const GObjectClass *parent_class = G_OBJECT_CLASS(g_mime_gpgme_sigstat_parent_class);

    g_free(self->fingerprint);
    self->fingerprint = NULL;
    if (self->key)
	gpgme_key_unref(self->key);
    self->key = NULL;

    (*parent_class->finalize)(object);
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

/** \brief Split a S/MIME certificate subject string into tokens
 *
 * \param subject certificate subject string
 * \param unescape unescape escaped parameters if set
 * \return a NULL-terminated array of sanitised (OID name; value) pairs, or a single item
 *
 * Split the passed DN string of a S/MIME x509 certificate (see RFC 5280) into (OID name; value) pairs.  Numerical OID's are
 * replaced by their string representations, and hex-encoded values are decoded.  If required, escaped values are unescaped.  All
 * items are guaranteed to be utf8-clean.
 *
 * If the string starts with a '<', it contains an email address, and is returned untouched in the array.
 */
static gchar **
tokenize_subject(const gchar *subject,
				 gboolean     unescape)
{
	static GRegex *split_re = NULL;
	static volatile guint initialized = 0U;
	gchar **result;

	/* create the reqular expression when called for teh first time */
	if (g_atomic_int_or(&initialized, 1U) == 0U) {
		/* split a DN string at unescaped ',' and '=' chars */
		split_re = g_regex_new("(?<!\\\\)[,=]", 0, 0, NULL);
		g_assert(split_re != NULL);
	}

	/* catch empty string */
	if (subject == NULL) {
		return NULL;
	}

	/* string starting with '<' indicates an email address */
	if (subject[0] == '<') {
		result = g_new0(gchar *, 2U);
		result[0] = g_strdup(subject);
	} else {
		/* split into (oid, value) pairs */
		result = g_regex_split(split_re, subject, 0);
		if (result != NULL) {
			gint n;

			for (n = 0; (result[n] != NULL) && (result[n + 1] != NULL); n += 2) {
				gchar *buffer;

				/* fix oid if necessary */
				buffer = g_strdup(gnutls_x509_dn_oid_name(result[n], GNUTLS_X509_DN_OID_RETURN_OID));
				g_free(result[n]);
				result[n] = buffer;

				/* value: decode hex-encoded */
				if (result[n + 1][0] == '#') {
					buffer = hex_decode(&result[n + 1][1]);
					g_free(result[n + 1]);
					result[n + 1] = buffer;
				} else if (unescape) {
					buffer = g_strcompress(result[n + 1]);
					g_free(result[n + 1]);
					result[n + 1] = buffer;
				} else {
					/* keep value */
				}
				libbalsa_utf8_sanitize(&result[n + 1], TRUE, NULL);
			}

			if ((result[n] != NULL) && (result[n + 1] == NULL)) {
				libbalsa_utf8_sanitize(&result[n], TRUE, NULL);
			}
		}
	}

	return result;
}

/** \brief Create a readable string from a S/MIME certificate subject
 *
 * \param subject certificate subject string
 * \return a readable string
 *
 * Extract the CN and EMAIL items from the passed subject, and return it as "CN <EMAIL>", omitting every part which is not
 * available.  If both items are unavailable, return the readable subject by calling libbalsa_cert_subject_readable().  If the
 * subject starts with '<', it contains an email address, which is just returned.
 */
static gchar *
cert_subject_cn_mail(const gchar *subject)
{
	gchar *readable_subject = NULL;
	gchar **elements;

	/* catch empty string */
	if (subject == NULL) {
		return NULL;
	}

	/* string starting with '<' indicates an email address */
	if (subject[0] == '<') {
		return g_strdup(subject);
	}

	/* extract CN and/or EMAIL items */
	elements = tokenize_subject(subject, TRUE);
	if (elements != NULL) {
		GString *buffer;
		gint n;

		buffer = g_string_new(NULL);
		for (n = 0; (elements[n] != NULL) && (elements[n + 1] != NULL); n += 2) {
			if (g_ascii_strcasecmp(elements[n], "CN") == 0) {
				if (buffer->len > 0U) {
					g_string_prepend_c(buffer, ' ');
				}
				g_string_prepend(buffer, elements[n + 1]);
			} else if (g_ascii_strcasecmp(elements[n], "EMAIL") == 0) {
				if (buffer->len > 0U) {
					g_string_append_c(buffer, ' ');
				}
				g_string_append_printf(buffer, "<%s>", elements[n + 1]);
			} else {
				/* ignore all others */
			}
		}
		g_strfreev(elements);

		if (buffer->len > 0U) {
			readable_subject = g_string_free(buffer, FALSE);
		} else {
			g_string_free(buffer, TRUE);
			readable_subject = libbalsa_cert_subject_readable(subject);
		}
	}

	return readable_subject;
}

/*
 * Return a S/MIME certificate subject as human-readable text.
 */
gchar *
libbalsa_cert_subject_readable(const gchar *subject)
{
    gchar **elements;
    gchar *readable_subject = NULL;

    elements = tokenize_subject(subject, FALSE);
    if (elements != NULL) {
        GString *buffer;
        gint n;

        buffer = g_string_new(NULL);
		for (n = 0; (elements[n] != NULL) && (elements[n + 1] != NULL); n += 2) {
			if (n > 0) {
				g_string_append(buffer, ", ");
			}
			g_string_append_printf(buffer, "%s=%s", elements[n], elements[n + 1]);
		}

		if (elements[n] != NULL) {
			if (n > 0) {
				g_string_append_printf(buffer, ", %s", elements[n]);
			} else {
				g_string_append(buffer, elements[n]);
			}
		}

		g_strfreev(elements);
		readable_subject = g_string_free(buffer, FALSE);
    }

    return readable_subject;
}
