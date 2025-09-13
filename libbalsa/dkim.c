/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2024 Stuart Parmenter and others, see the file AUTHORS for a list.
 *
 * This module implements DMARC/DKIM signature checks, according to the following standards:
 * - RFC 6376: DomainKeys Identified Mail (DKIM) Signatures
 * - RFC 7489: Domain-based Message Authentication, Reporting, and Conformance (DMARC)
 * - RFC 8463: A New Cryptographic Signature Method for DomainKeys Identified Mail (DKIM)
 *
 * RFC 6376, sect. 3.1 defines that DKIM tags and values must be interpreted in a case-sensitive manner unless explicitly specified
 * otherwise.  RFC 7489 sect. 3.1 defines that domain names shall be compared in a case-insensitive manner.  As domain names are
 * case-insensitive (see RFC 1035 sect. 2.3.3 and RFC 4343), it makes sense to treat the domain parts of the DKIM "d" and "i" tags
 * as being case-insensitive although they are not specified as such by RFC 6376.  This avoids some (rare) misleading "domain
 * mismatch" results.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if not, see
 * <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <ctype.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gnutls/abstract.h>
#include "libbalsa.h"
#include "dkim.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "dkim"


#define DKIM_ERROR_QUARK				(g_quark_from_static_string("dkim"))


#define DMARC_DNS_ERROR						-1
#define DMARC_POLICY_UNKNOWN				0
#define DMARC_POLICY_NONE					1
#define DMARC_POLICY_QUARANTINE				2
#define DMARC_POLICY_REJECT					3
#define DMARC_DKIM_STRICT					4


#define IS_5322_WSP(x)					(((x) == ' ') || ((x) == '\t'))
#define IS_5322_EOL(x)					(((x) == '\r') || ((x) == '\n'))


/* @brief DKIM header data and status */
typedef struct {
	gnutls_sign_algorithm_t cryptalg;	/**< the crypto algorithm extracted from the 'a' tag (REQUIRED) */
	GChecksumType hashalg;				/**< the hash algorithm extracted from the 'a' tag (REQUIRED) */
	gnutls_datum_t b;					/**< signature data (REQUIRED) */
	gnutls_datum_t bh;					/**< hash of the canonicalised body part of the message as limited by the "l=" tag
										 * (REQUIRED) */
	gboolean hdr_canon_relaxed;			/**< header canonicalisation mode 'simple' (FALSE, default) or 'relaxed' (TRUE), from the
										 * 'c' tag (OPTIONAL) */
	gboolean body_canon_relaxed;		/**< body canonicalisation mode 'simple' (FALSE, default) or 'relaxed' (TRUE), from the 'c'
										 * tag (OPTIONAL) */
	gchar *d;							/**< SDID claiming responsibility for an introduction of a message into the mail stream,
										 * converted to lower-case (REQUIRED) */
	gchar **h;							/**< signed header fields (REQUIRED) */
	gint64 l;							/**< body length count (OPTIONAL, default == -1 is entire body) */
	gchar *s;							/**< selector subdividing the namespace for the "d=" (domain) tag (REQUIRED) */
	GDateTime *t;						/**< signature time stamp (RECOMMENDED, default is an unknown creation time) */
	GDateTime *x;						/**< signature expiration (RECOMMENDED, default is no expiration) */
	gint status;						/**< DKIM status, @ref DKIM_SUCCESS, etc. */
	gchar *detail;						/**< detail data if the status is not success, may be NULL */
} dkim_header_t;


/** @brief DNS TXT record evaluation function
 * @param[in] txt DNS TXT record
 * @param[in] user_data additional user data needed for the evaluation
 * @param[out] error location for error, may be NULL
 * @return NULL on error, or an application-dependent value on success
 */
typedef gpointer (*dns_eval_fn)(const gchar *txt, gconstpointer user_data, GError **error);


struct _LibBalsaDkim {
	GObject parent;

	gint status;
	gchar *msg_short;
	gchar *msg_long;
};


G_DEFINE_TYPE(LibBalsaDkim, libbalsa_dkim, G_TYPE_OBJECT)


static void libbalsa_dkim_finalize(GObject *self);

static gboolean dkim_error(dkim_header_t *dkim_header,
						   guint          status,
						   const gchar   *format,
						   ...)
	G_GNUC_PRINTF(3, 4);

static LibBalsaDkim *libbalsa_dkim_new(GMimeStream            *stream,
									   LibBalsaMessageHeaders *headers)
	G_GNUC_WARN_UNUSED_RESULT;
static void libbalsa_dkim_body_list(LibBalsaMessageBody *body_list);
static dkim_header_t *eval_dkim_header(const gchar *header)
	G_GNUC_WARN_UNUSED_RESULT;
static gboolean tag_get_base64(const gchar    *value,
							   gnutls_datum_t *target,
							   GError        **error);
static gboolean tag_get_a(const gchar   *value,
						  dkim_header_t *result,
						  GError       **error);
static gboolean tag_get_c(gchar         *value,
						  dkim_header_t *result,
						  GError       **error);
static gboolean tag_get_number(gchar   *value,
							   gint64  *target,
							   GError **error);
static gboolean tag_get_timestamp(gchar      *value,
								  GDateTime **timestamp,
								  GError    **error);

static void dkim_header_free(dkim_header_t *dkim_header);
static gchar *dkim_status_full(GList               *dkim_headers,
							   const dkim_header_t *dmarc_header)
	G_GNUC_WARN_UNUSED_RESULT;
static void dkim_status_append(GString             *status,
							   const dkim_header_t *header);
static inline const gchar *dkim_stat_str(gint status);

static guint dmarc_dns_lookup(InternetAddressList *from,
							  gchar              **dmarc_domain);
static gpointer eval_dmarc_dns_txt(const gchar *txt_str,
								   gpointer     subdomain,
								   GError     **error);
static gboolean dmarc_get_policy(guint       *dest,
								 const gchar *value,
								 const gchar *tag_name,
								 GError     **error);
static gnutls_pubkey_t dkim_get_pubkey(dkim_header_t *dkim_header);

static void dkim_verify_signature(GMimeStream    *stream,
								  dkim_header_t  *dkim_header,
								  gnutls_pubkey_t pubkey);
static GList *collect_headers(GMimeStream          *stream,
							  gchar               **use_dkim_header,
							  const gnutls_datum_t *active_sig,
							  gboolean              unfold)
	G_GNUC_WARN_UNUSED_RESULT;
static gboolean is_active_dkim_header(const gchar          *header,
									  const gnutls_datum_t *active_sig);
static gchar *grab_header(GList      **header_list,
						  const gchar *find_header)
	G_GNUC_WARN_UNUSED_RESULT;
static gchar *canon_header_relaxed(gchar *header);
static void dkim_check_body_hash(GMimeStream   *stream,
								 dkim_header_t *dkim_header);

static gnutls_pubkey_t eval_dkim_dns_txt(const gchar         *txt_str,
										 const dkim_header_t *dkim_header,
										 GError             **error);
static gpointer dns_lookup_txt(const gchar  *rrname,
							   dns_eval_fn   callback,
							   gconstpointer user_data,
							   GError      **error)
	G_GNUC_WARN_UNUSED_RESULT;
static gchar **strsplit_clean(const gchar *value,
							  const gchar *delim,
							  gboolean     strip_empty_last)
	G_GNUC_WARN_UNUSED_RESULT;
static gssize checksum_update_limited(GChecksum    *hash,
									  const guchar *data,
									  gssize        length,
									  gssize        maxlen);
static void dmarc_dns_cache_cleanup(void);
static void dkim_dns_cache_cleanup(void);


/** @brief DMRAC DNS lookup cache
 *
 * Contains the results of dmarc DNS TXT lookups, with the domain extracted from the From: address as key and the domain actually
 * returning a DNS result (the From: address may contain a sub-domain), with the DMARC mode bitmask @ref DMARC_POLICY_NONE, etc.,
 * converted to a single char, prepended to it as value (quite a hack, but simplifies cleaning the cache).
 */
static GHashTable *dmarc_dns_cache = NULL;
/** @brief DMRAC DNS cache access mutex */
G_LOCK_DEFINE_STATIC(dmarc_dns_cache);

/** @brief DKIM DNS lookup cache
 *
 * Contains the results of dkim DNS TXT lookups, with a string containing the DKIM header @em s and @em d tag values and the crypto
 * and hash algorithm ID's, separated by ":", as key and the GnuTLS public key as value.
 */
static GHashTable *dkim_dns_cache = NULL;
/** @brief DKIM DNS cache access mutex */
G_LOCK_DEFINE_STATIC(dkim_dns_cache);


void
libbalsa_dkim_message(LibBalsaMessage *message)
{
	LibBalsaMailbox *mailbox;
	LibBalsaMessageHeaders *headers;
	LibBalsaMessageBody *body_list;

	g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

	mailbox = libbalsa_message_get_mailbox(message);
	libbalsa_mailbox_lock_store(mailbox);
	body_list = libbalsa_message_get_body_list(message);

	/* DKIM status of the message itself */
	headers = libbalsa_message_get_headers(message);
	if ((headers != NULL) && (body_list->dkim == NULL)) {
		GMimeStream *msg_stream;

		msg_stream = libbalsa_message_stream(message);
		g_mime_stream_reset(msg_stream);
		body_list->dkim = libbalsa_dkim_new(msg_stream, headers);
		g_object_unref(msg_stream);
	}

	/* scan the body tree for embedded messages */
	libbalsa_dkim_body_list(body_list);

	libbalsa_mailbox_unlock_store(mailbox);
}


gint
libbalsa_dkim_status_code(LibBalsaDkim *dkim)
{
	return LIBBALSA_IS_DKIM(dkim) ? dkim->status : DKIM_NONE;
}


const gchar *
libbalsa_dkim_status_str_short(LibBalsaDkim *dkim)
{
	g_return_val_if_fail(LIBBALSA_IS_DKIM(dkim), NULL);
	return dkim->msg_short;
}


const gchar *
libbalsa_dkim_status_str_long(LibBalsaDkim *dkim)
{
	g_return_val_if_fail(LIBBALSA_IS_DKIM(dkim), NULL);
	return dkim->msg_long;
}


static void
libbalsa_dkim_class_init(LibBalsaDkimClass *klass)
{
	GObjectClass *gobject_klass = G_OBJECT_CLASS(klass);

	gobject_klass->finalize = libbalsa_dkim_finalize;
}


static void
libbalsa_dkim_init(LibBalsaDkim *self)
{
	self->status = DKIM_NONE;
}


static void
libbalsa_dkim_finalize(GObject *self)
{
	LibBalsaDkim *dkim = LIBBALSA_DKIM(self);
	const GObjectClass *parent_class = G_OBJECT_CLASS(libbalsa_dkim_parent_class);

	g_free(dkim->msg_short);
	g_free(dkim->msg_long);
	(*parent_class->finalize)(self);
}


/** @brief Calculate the DKIM and DMARC status from a message stream and headers
 *
 * @param[in] stream stream containing the complete message or embedded message part
 * @param[in] headers headers of the message or embedded message part
 * @return DKIM status object, never @c NULL
 */
static LibBalsaDkim *
libbalsa_dkim_new(GMimeStream *stream, LibBalsaMessageHeaders *headers)
{
	LibBalsaDkim *result;
	GList *dkim_results = NULL;
	guint summary[3] = {0U, 0U, 0U};
	guint total = 0U;
	gint dmarc_mode = 0;
	GList *p;

	result = LIBBALSA_DKIM(g_object_new(LIBBALSA_TYPE_DKIM, NULL));

	/* evaluate DKIM-Signature headers */
	for (p = headers->user_hdrs; p != NULL; p = p->next) {
		const gchar * const *pair = (const gchar * const *) p->data;

		if (g_ascii_strcasecmp(pair[0], "DKIM-Signature") == 0) {
			dkim_header_t *dkim_header;

			dkim_header = eval_dkim_header(pair[1]);
			dkim_results = g_list_prepend(dkim_results, dkim_header);
			if (dkim_header->status == DKIM_SUCCESS) {
				gnutls_pubkey_t pubkey;

				pubkey = dkim_get_pubkey(dkim_header);
				if (pubkey != NULL) {
					dkim_verify_signature(stream, dkim_header, pubkey);
				}
			} else {
				g_debug("%s: broken DKIM-Signature header: %d: %s", __func__, dkim_header->status, dkim_header->detail);
			}
			summary[dkim_header->status]++;
			total++;
		}
	}
	dkim_results = g_list_reverse(dkim_results);

	/* check the From: address for a DMARC mode */
	if (headers->from != NULL) {
		gchar *dmarc_domain = NULL;

		dmarc_mode = dmarc_dns_lookup(headers->from, &dmarc_domain);
		if (dmarc_mode != 0) {
			size_t dmarc_len;
			const dkim_header_t *dmarc_header = NULL;

			dmarc_len = strlen(dmarc_domain);
			/* RFC 7489 sect. 3.1.1: "Note that a single email can contain multiple DKIM signatures, and it is considered to be
			 * a DMARC "pass" if any DKIM signature is aligned and verifies." */
			for (p = dkim_results;
				((dmarc_header == NULL) || (dmarc_header->status == DKIM_FAILED)) && (p != NULL);
				p = p->next) {
				const dkim_header_t *dkim_header = (const dkim_header_t *) p->data;

				if (strcmp(dmarc_domain, dkim_header->d) == 0) {
					dmarc_header = dkim_header;
				} else if ((dmarc_mode & DMARC_DKIM_STRICT) == 0U) {
					size_t dkim_len;

					dkim_len = strlen(dkim_header->d);
					if ((dkim_len < dmarc_len) && (dmarc_domain[dmarc_len - dkim_len - 1] == '.') &&
						(strcmp(dkim_header->d, &dmarc_domain[dmarc_len - dkim_len]) == 0)) {
						dmarc_header = dkim_header;
					}
				} else {
					/* nothing to do */
				}
			}

			/* evaluate */
			if (dmarc_header == NULL) {
				result->status = DKIM_FAILED;
				/* Translators: #1 DMARC domain; please do not translate DKIM */
				result->msg_short = g_strdup_printf(_("DKIM signature for sender domain “%s”: missing"), dmarc_domain);
			} else {
				result->status = dmarc_header->status;
				/* Translators: #1 DMARC domain; please do not translate DKIM */
				result->msg_short = g_strdup_printf(_("DKIM signature for sender domain “%s”: %s"), dmarc_domain,
					dkim_stat_str(dmarc_header->status));
			}
			result->msg_long = dkim_status_full(dkim_results, dmarc_header);
		}
		g_free(dmarc_domain);
	}

	if (dmarc_mode == 0) {
		gint n;
		GString *short_str;

		/* RFC 6376 sect 6.1 states: "When a signature successfully verifies, a Verifier will either stop processing or attempt
		 * to verify any other signatures, at the discretion of the implementation."  Thus, we use the best available status as
		 * summary. */
		result->status = DKIM_NONE;
		short_str = g_string_new(NULL);
		for (n = DKIM_SUCCESS; n <= DKIM_FAILED; n++) {
			if (summary[n] > 0U) {
				if (result->status == DKIM_NONE) {
					result->status = n;
				}
				if (short_str->len > 0U) {
					g_string_append(short_str, ", ");
				}
				g_string_append_printf(short_str, "%s: %u", dkim_stat_str(n), summary[n]);
			}
		}
		if (result->status == DKIM_NONE) {
			/* Translators: please do not translate DKIM */
			result->msg_short = g_strdup(_("no DKIM signature"));
		} else {
			result->msg_short =
				/* Translators: please do not translate DKIM */
				g_strdup_printf("%s: %s", ngettext(_("DKIM signature"), _("DKIM signatures"), total), short_str->str);
		}
		g_string_free(short_str, TRUE);
		result->msg_long = dkim_status_full(dkim_results, NULL);
	}

	g_list_free_full(dkim_results, (GDestroyNotify) dkim_header_free);

	return result;
}


/** @brief Check the DKIM and DMARC status of embedded message parts
 *
 * @param[in] body_list linked list of bodies
 *
 * Call libbalsa_dkim_new() for every embedded message (i.e. MIME type <c>message/rfc822</c> message part and assign the result to
 * @ref LibBalsaMessageBody::dkim.  The function is called recursively for sub-parts.
 */
static void
libbalsa_dkim_body_list(LibBalsaMessageBody *body_list)
{
	LibBalsaMessageBody *this_body;

	for (this_body = body_list; this_body != NULL; this_body = this_body->next) {
		if (this_body->mime_part != NULL) {
			GMimeContentType *type;

			type = g_mime_object_get_content_type(this_body->mime_part);
			if (g_mime_content_type_is_type(type, "message", "rfc822") && (this_body->dkim == NULL)) {
				GMimeStream *body_stream;

				body_stream = g_mime_stream_mem_new();
				g_mime_object_write_content_to_stream(this_body->mime_part, NULL, body_stream);
				g_mime_stream_flush(body_stream);
				g_mime_stream_reset(body_stream);
				this_body->dkim = libbalsa_dkim_new(body_stream, this_body->embhdrs);
				g_object_unref(body_stream);
			}
		}
		if (this_body->parts != NULL) {
			libbalsa_dkim_body_list(this_body->parts);
		}
	}

}


/** @brief Verify a DKIM signature
 *
 * @param[in] stream message or bydy stream
 * @param[in,out] dkim_header DKIM header data, of which the dkim_header_t::status and if applicable dkim_header_t::detail fields
 *                are updated according to the verification result
 * @param[in] pubkey public key used for the verification
 * @sa 6376 sect. 6.1.3
 */
static void
dkim_verify_signature(GMimeStream *stream, dkim_header_t *dkim_header, gnutls_pubkey_t pubkey)
{
	GList *headers;
	gchar *active_dkim_header = NULL;

	/* extract all headers verbatim, and verify the body hash */
	g_mime_stream_reset(stream);
	headers = collect_headers(stream, &active_dkim_header, &dkim_header->b, dkim_header->hdr_canon_relaxed);
	dkim_check_body_hash(stream, dkim_header);

	/* proceed iff the body hash could be validated */
	if (dkim_header->status != DKIM_FAILED) {
		GChecksum *hash;
		guint8 hashbuf[32];
		gsize hashsize;
		gnutls_datum_t hash_val;
		int gtls_res;
		int n;
		gchar **items;

		/* note: should never be null, fix scan-build warning */
		g_assert(active_dkim_header != NULL);

		hash = g_checksum_new(dkim_header->hashalg);
		for (n = 0; dkim_header->h[n] != NULL; n++) {
			gchar *header_value;

			/* add the requested headers in the proper order and with the requested canonicalisation to the hash */
			header_value = grab_header(&headers, dkim_header->h[n]);
			if (header_value != NULL) {
				if (dkim_header->hdr_canon_relaxed) {
					header_value = canon_header_relaxed(header_value);
				}
				g_checksum_update(hash, (const guchar *) header_value, -1);
				if (dkim_header->hdr_canon_relaxed) {
					g_checksum_update(hash, (const guchar *) "\r\n", 2);
				}
				g_free(header_value);
			}
		}

		/* fix the canonicalisation for the DKIM-Signature header under test */
		if (dkim_header->hdr_canon_relaxed) {
			active_dkim_header = canon_header_relaxed(active_dkim_header);
		} else {
			active_dkim_header[strlen(active_dkim_header) - 2U] = '\0';
		}

		/* ensure an empty "b" tag in the DKIM-Signature header and add it to the hash */
		items = g_strsplit(active_dkim_header, ";", -1);
		for (n = 0; items[n] != NULL; n++) {
			gchar *eql;
			const gchar *b_tag;

			eql = strchr(items[n], '=');
			b_tag = strchr(items[n], 'b');
			if ((b_tag != NULL) && (b_tag < eql) && (b_tag[1] != 'h')) {
				eql[1] = '\0';
			}
		}
		g_free(active_dkim_header);
		active_dkim_header = g_strjoinv(";", items);
		g_strfreev(items);
		g_checksum_update(hash, (const guchar *) active_dkim_header, -1);

		/* get the hash */
		hashsize = sizeof(hashbuf);
		g_checksum_get_digest(hash, hashbuf, &hashsize);
		g_checksum_free(hash);

		/* validate signature */
		hash_val.data = hashbuf;
		hash_val.size = hashsize;
		if (dkim_header->cryptalg == GNUTLS_SIGN_EDDSA_ED25519) {
			gtls_res = gnutls_pubkey_verify_data2(pubkey, dkim_header->cryptalg, 0, &hash_val, &dkim_header->b);
		} else {
			gtls_res = gnutls_pubkey_verify_hash2(pubkey, dkim_header->cryptalg, 0, &hash_val, &dkim_header->b);
		}
		/* Note: these two GnuTLS functions ONLY return "zero or positive code on success", whilst *all* others return
		 * GNUTLS_E_SUCCESS (0); see https://www.gnutls.org/manual/html_node/Operations.html */
		if (gtls_res < GNUTLS_E_SUCCESS) {
			/* Translators: #1 reason for signature verification failure */
			(void) dkim_error(dkim_header, DKIM_FAILED, _("signature verification failed: %s"), gnutls_strerror(gtls_res));
		} else if (dkim_header->x != NULL) {
			GDateTime *now;

			now = g_date_time_new_now_utc();
			if (g_date_time_compare(now, dkim_header->x) > 0) {
				gchar *expire_str;

				expire_str = g_date_time_format(dkim_header->x, "%x %X");
				/* Translators: #1 date end time of signature expiry */
				(void) dkim_error(dkim_header, DKIM_WARNING, _("the signature is valid, but has expired on %s"), expire_str);
				g_free(expire_str);
			}
			g_date_time_unref(now);
		}
	}

	/* clean up */
	g_list_free_full(headers, g_free);
	g_free(active_dkim_header);
}


/** @brief Collect message headers
 *
 * @param[in,out] stream message stream, initially pointing to the start of the message, and to the body start on return
 * @param[out] use_dkim_header filled with a newly allocated copy of the @em DKIM-Signature currently checked one
 * @param[in] active_sig signature ("b" tag) of the currently checked @em DKIM-Signature header
 * @param[in] unfold FALSE for @em simple header canonicalisation mode (as-is, including terminating CRLF), TRUE for @em relaxed
 *            mode (unfold and remove training CRLF, no canonicalisation of the value)
 * @return a list of possibly canonicalised header stringss in reverse order (i.e. newest header is the last in the list)
 *
 * Collect all message headers and return them in the proper order for the DKIM signature verification.  The currently checked
 * header is returned separately.  As usually only a fraction of all headers is included in the signature, the canonicalisation is
 * not applied here.
 *
 * @note Both the list and the currently checked DKIM header @em must be freed by the caller.
 */
static GList *
collect_headers(GMimeStream *stream, gchar **use_dkim_header, const gnutls_datum_t *active_sig, gboolean unfold)
{
	gboolean headers_done = FALSE;
	GList *header_list = NULL;
	GByteArray *line_buf;
	GString *header_buf;

	header_buf = g_string_sized_new(1024U);
	line_buf = g_byte_array_sized_new(1024U);
	while (!headers_done && !g_mime_stream_eos(stream)) {
		guint line_len;

		g_byte_array_set_size(line_buf, 0);
		g_mime_stream_buffer_readln(stream, line_buf);

		/* strip off LF or CRLF */
		line_len = line_buf->len;
		while ((line_len > 0U) && IS_5322_EOL(line_buf->data[line_len - 1U])) {
			line_len--;
		}

		if (((line_len == 0U) || !IS_5322_WSP((char) line_buf->data[0])) && (header_buf->len > 0U)) {
			/* check if the header is the DKIM-Signature which shall be checked, and return it separately so the signature
			 * calculation is not confused if multiple ones are present, and maybe even included in a different signature */
			if (is_active_dkim_header(header_buf->str, active_sig)) {
				*use_dkim_header = g_strdup(header_buf->str);
			} else {
				header_list = g_list_prepend(header_list, g_strdup(header_buf->str));
			}
			g_string_truncate(header_buf, 0U);
		}

		if (line_len > 0U) {
			g_string_append_len(header_buf, (const gchar *) line_buf->data, line_len);
			if (!unfold) {
				g_string_append(header_buf, "\r\n");
			}
		} else {
			headers_done = TRUE;
		}
	}
	g_byte_array_unref(line_buf);
	g_string_free(header_buf, TRUE);
	return header_list;
}


/** @brief Identify the currently checked DKIM-Signature header
 *
 * @param[in] header header string
 * @param[in] active_sig signature ("b" tag) of the currently checked @em DKIM-Signature header
 * @return TRUE iff the passed header is a @em DKIM-Signature whose "b" tag matches the passed signature
 */
static gboolean
is_active_dkim_header(const gchar *header, const gnutls_datum_t *active_sig)
{
	gboolean result = FALSE;

	if ((g_ascii_strncasecmp(header, "DKIM-Signature", 14UL) == 0) && (strchr(": \t", header[14]) != NULL)) {
		gchar **items;
		gboolean b_found = FALSE;
		gint n;

		items = g_strsplit(header, ";", -1);
		for (n = 0; !b_found && (items[n] != NULL); n++) {
			items[n] = g_strstrip(items[n]);
			if ((items[n][0] == 'b') && (strchr("= \t", items[n][1]) != NULL)) {
				gchar *eql;

				b_found = TRUE;
				eql = strchr(items[n], '=');
				if (eql != NULL) {
					gsize this_sig_len;

					g_base64_decode_inplace(&eql[1], &this_sig_len);
					if ((active_sig->size == this_sig_len) && (memcmp(active_sig->data, &eql[1], this_sig_len) == 0)) {
						result = TRUE;
					}
				}
			}
		}
		g_strfreev(items);
	}

	return result;
}


/** @brief Grab a header for DKIM verification
 *
 * @param[in,out] header_list list of extracted headers in reverse order
 * @param[in] find_header the header which shall be processed
 * @return the header value on success, NULL iff it is not in the list
 * @note The list element containing the requested header is removed, i.e. the caller shall free the returned value.
 */
static gchar *
grab_header(GList **header_list, const gchar *find_header)
{
	GList *p;
	gchar *result = NULL;
	size_t find_len;

	find_len = strlen(find_header);
	p =  *header_list;
	while ((p != NULL) && (result == NULL)) {
		gchar *this_header = (gchar *) p->data;

		if ((g_ascii_strncasecmp(find_header, this_header, find_len) == 0) && (strchr(": \t", this_header[find_len]) != NULL)) {
			result = this_header;
			*header_list = g_list_remove_link(*header_list, p);
			g_list_free(p);
			p = NULL;
		} else {
			p = p->next;
		}
	}

	return result;
}


/** @brief Apply "relaxed" DKIM header canonicalisation
 *
 * @param[in] header message header
 * @return the passed header, canonicalised according to RFC 6376 sect. 3.4.2
 */
static gchar *
canon_header_relaxed(gchar *header)
{
	gchar *colon;
	size_t length;
	size_t idx;

	colon = strchr(header, ':');
	length = strlen(header);
	if (colon != NULL) {
		char *p;
		ssize_t wsp_len;

		/* lower-case header field name */
		for (p = header; p < colon; p++) {
			*p = tolower(*p);
		}

		/* remove wsp after the colon */
		for (p = &colon[1]; IS_5322_WSP(*p); p++) {
			/* nothing to do */
		}
		wsp_len = (p - colon) - 1;
		if (wsp_len > 0) {
			memmove(&colon[1], p, length - (p - header) + 1U);
			length -= wsp_len;
		}

		/* remove wsp before the colon */
		for (p = &colon[-1]; (p > header) && IS_5322_WSP(*p); p--) {
			/* nothing to do */
		}
		wsp_len = (colon - p) - 1;
		if (wsp_len > 0) {
			memmove(&p[1], colon, length - (p - header) + 1U);
			length -= wsp_len;
		}
	}

	/* erase all WSP at the end */
	while ((length > 0U) && IS_5322_WSP(header[length - 1U])) {
		length--;
	}
	header[length] = '\0';

	/* replace all WSP sequences by a single SP */
	idx = 0U;
	while (idx < length) {
		if (IS_5322_WSP(header[idx])) {
			ssize_t wsp_len;

			header[idx++] = ' ';
			for (wsp_len = 0; IS_5322_WSP(header[idx + wsp_len]); wsp_len++) {
				/* nothing to do */
			}
			if (wsp_len > 0) {
				memmove(&header[idx], &header[idx + wsp_len], length - (idx + wsp_len) + 1U);
				length -= wsp_len;
			}
		} else {
			idx++;
		}
	}

	return header;
}


/** @brief Validate the message body hash
 *
 * @param[in,out] stream message stream, @em MUST point to the beginning of the message body
 * @param[in,out] dkim_header DKIM header data, of which the dkim_header_t::status and if applicable dkim_header_t::detail fields
 *                are updated according to the verification result
 * @sa RFC 6376, sect. 3.4.3 and 3.4.4
 */
static void
dkim_check_body_hash(GMimeStream *stream, dkim_header_t *dkim_header)
{
	GChecksum *hash;
	guint8 hashbuf[32];
	gsize hashsize;
	guint empty_lines = 0U;
	gboolean empty_body;
	gssize maxlen;
	GByteArray *buffer;

	buffer = g_byte_array_new();
	maxlen = dkim_header->l;
	hash = g_checksum_new(dkim_header->hashalg);
	empty_body = g_mime_stream_eos(stream);
	while (!g_mime_stream_eos(stream) && (maxlen != 0)) {
		guint line_len;

		g_byte_array_set_size(buffer, 0U);
		g_mime_stream_buffer_readln(stream, buffer);
		line_len = buffer->len;
		while ((line_len > 0U) && IS_5322_EOL(buffer->data[line_len - 1U])) {
			line_len--;
		}

		/* relaxed body canonicalisation algorithm: strip trailing whitespace */
		if (dkim_header->body_canon_relaxed) {
			while ((line_len > 0U) && (IS_5322_WSP((char) buffer->data[line_len - 1U]))) {
				line_len--;
			}
		}

		if (line_len == 0U) {
			empty_lines++;
		} else {
			for (; empty_lines > 0U; empty_lines--) {
				maxlen = checksum_update_limited(hash, (const guchar *) "\r\n", 2, maxlen);
			}

			/* relaxed body canonicalisation algorithm: reduce whitespace sequences to a single SP */
			if (dkim_header->body_canon_relaxed) {
				guint n = 0U;

				while (n < line_len) {
					if (IS_5322_WSP(buffer->data[n])) {
						if ((n > 0U) && (buffer->data[n - 1U] == ' ')) {
							line_len--;
							memmove(&buffer->data[n], &buffer->data[n + 1U], line_len - n);
						} else {
							buffer->data[n++] = ' ';
						}
					} else {
						n++;
					}
				}
			}
			maxlen = checksum_update_limited(hash, buffer->data, line_len, maxlen);
			maxlen = checksum_update_limited(hash, (const guchar *) "\r\n", 2, maxlen);
		}
	}
	g_byte_array_unref(buffer);

	/* simple body canonicalisation algorithm: expand a completely empty body to CRLF */
	if (empty_body && !dkim_header->body_canon_relaxed) {
		checksum_update_limited(hash, (const guchar *) "\r\n", 2, maxlen);
	}

	/* evaluate */
	hashsize = sizeof(hashbuf);
	g_checksum_get_digest(hash, hashbuf, &hashsize);
	g_checksum_free(hash);
	if ((hashsize != dkim_header->bh.size) || (memcmp(hashbuf, dkim_header->bh.data, hashsize) != 0)) {
		(void) dkim_error(dkim_header, DKIM_FAILED, _("body hash mismatch"));
	} else if (!g_mime_stream_eos(stream) && (maxlen != -1)) {
		(void) dkim_error(dkim_header, DKIM_WARNING, _("the body hash does not cover the complete body"));
	} else {
		/* success, nothing to do */
	}
}


/** @brief Extract a DKIM-Signature header
 *
 * @param[in] header DKIM-Signature header
 * @return a newly allocated DKIM header structure, @em never NULL
 * @note The dkim_header_t::status field indicates if the header could be parsed or is faulty.
 * @sa 6376 sect. 3.5
 */
static dkim_header_t *
eval_dkim_header(const gchar *header)
{
	dkim_header_t *result;
	gchar **items;
	guint n;
	GString *seen;
	gchar *auid = NULL;
	gboolean success = TRUE;

	result = g_new0(dkim_header_t, 1U);
	result->l = -1;

	/* the header may end with an empty item (RFC 6376, sect. 3.2) which we must ignore */
	items = strsplit_clean(header, ";", TRUE);

	/* remember the already seen tags so we can catch broken headers with duplicated ones (bh == B) */
	seen = g_string_sized_new(16U);

	for (n = 0U; success && (items[n] != NULL); n++) {
		gchar *value;

		value = strchr(items[n], '=');
		if ((value == NULL) || (value == items[n])) {
			success = dkim_error(result, DKIM_FAILED, _("malformed header element “%s”"), items[n]);
		} else {
			size_t tag_len;

			*value++ = '\0';
			items[n] = g_strstrip(items[n]);
			value = g_strstrip(value);
			tag_len = strlen(items[n]);

			/* check for duplicates */
			if (((strcmp(items[n], "bh") == 0) && (strchr(seen->str, 'B') != NULL)) ||
				((tag_len == 1U) && (strchr(seen->str, items[n][0]) != NULL))) {
				success = dkim_error(result, DKIM_FAILED, _("duplicated tag “%s”"), items[n]);
			} else {
				GError *error = NULL;

				if (strcmp(items[n], "bh") == 0) {
					g_string_append_c(seen, 'B');
					success = tag_get_base64(value, &result->bh, &error);
				} else if (tag_len == 1U) {
					g_string_append_c(seen, items[n][0]);
					switch (items[n][0]) {
					case 'a':
						success = tag_get_a(value, result, &error);
						break;
					case 'b':
						success = tag_get_base64(value, &result->b, &error);
						break;
					case 'c':
						success = tag_get_c(value, result, &error);
						break;
					case 'd':
						result->d = g_ascii_strdown(value, -1);
						break;
					case 'h':
						result->h = strsplit_clean(value, ":", FALSE);
						break;
					case 'i':
						auid = g_ascii_strdown(value, -1);
						break;
					case 'l':
						success = tag_get_number(value, &result->l, &error);
						break;
					case 'q':
						break;
					case 's':
						result->s = g_strdup(value);
						break;
					case 't':
						success = tag_get_timestamp(value, &result->t, &error);
						break;
					case 'v':
						if (strcmp(value, "1") != 0) {
							success = dkim_error(result, DKIM_FAILED, _("wrong version “%s”, expect 1"), value);
						}
						break;
					case 'x':
						success = tag_get_timestamp(value, &result->x, &error);
						break;
					default:
						g_debug("%s: ignore tag '%s'", __func__, items[n]);
					}
				} else {
					g_debug("%s: ignore tag '%s'", __func__, items[n]);
				}
				if (error != NULL) {
					result->status = DKIM_FAILED;
					result->detail = g_strdup(error->message);
					g_error_free(error);
				}
			}
		}
	}

	/* check for broken data */
	if (success &&
		((strchr(seen->str, 'v') == NULL) || (result->cryptalg == GNUTLS_SIGN_UNKNOWN) || (result->b.data == NULL) ||
		 (result->bh.data == NULL) || (result->d == NULL) || (result->h == NULL) || (result->s == NULL))) {
		success = dkim_error(result, DKIM_FAILED, _("malformed header: required tag missing"));
	}

	/* h tag must include From */
	if (success) {
		for (n = 0U; (result->h[n] != NULL) && (strcasecmp(result->h[n], "From") != 0); n++) {
			/* nothing to do */
		}
		if (result->h[n] == NULL) {
			success = dkim_error(result, DKIM_FAILED, _("malformed header: From field not signed"));
		}
	}

	/* if present, the i tag shall have the same domain as d, or a subdomain thereof */
	if (success && (auid != NULL)) {
		const gchar *at_sign;

		at_sign = strrchr(auid, '@');
		if (at_sign == NULL) {
			(void) dkim_error(result, DKIM_FAILED, _("malformed “i” header tag"));
		} else {
			ssize_t start;

			start = strlen(at_sign) - strlen(result->d) - 1;
			if (!g_str_has_suffix(at_sign, result->d) || (start < 0) || (strchr("@.", at_sign[start]) == NULL)) {
				(void) dkim_error(result, DKIM_FAILED, _("malformed header: domain mismatch"));
			}
		}
	}

	g_string_free(seen, TRUE);
	g_strfreev(items);
	g_free(auid);
	return result;
}


/** @brief Extract a Base64 encoded item from a DKIM-Signature header or a DKIM DNS entry tag
 *
 * @param[in] value Base64-encoded value
 * @param[out] target filled with the decoded value and size
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE on error, i.e. if the passed value is not a valid number
 * @sa RFC 6376 sect. 3.5, item "b" and "bh"; sect. 3.6.1, item "p"
 */
static gboolean
tag_get_base64(const gchar *value, gnutls_datum_t *target, GError **error)
{
	gsize length;
	gboolean success = TRUE;

	target->data = g_base64_decode(value, &length);
	target->size = length;
	if ((target->data == NULL) || (target->size == 0U)) {
		g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("empty Base64 encoded element"));
		success = FALSE;
	}
	return success;
}


/** @brief Extract the algorithms mode from a DKIM-Signature header "a" tag
 *
 * @param[in] value value of the DKIM-Signature header "a" tag
 * @param[in,out] result DKIM header data, of which the fields dkim_header_t::cryptalg and dkim_header_t::hashalg are updated on
 *                success
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE on error
 * @sa RFC 6376 sect. 3.5, item "a"
 */
static gboolean
tag_get_a(const gchar *value, dkim_header_t *result, GError **error)
{
	gboolean success = TRUE;

	if (strcmp(value, "rsa-sha1") == 0) {
		result->cryptalg = GNUTLS_SIGN_RSA_SHA1;
		result->hashalg = G_CHECKSUM_SHA1;
	} else if (strcmp(value, "rsa-sha256") == 0) {
		result->cryptalg = GNUTLS_SIGN_RSA_SHA256;
		result->hashalg = G_CHECKSUM_SHA256;
	} else if (strcmp(value, "ed25519-sha256") == 0) {
		result->cryptalg = GNUTLS_SIGN_EDDSA_ED25519;
		result->hashalg = G_CHECKSUM_SHA256;
	} else {
		g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("unsupported algorithm “%s”"), value);
		success = FALSE;
	}
	return success;
}


/** @brief Extract the canonicalisation mode from a DKIM-Signature header "c" tag
 *
 * @param[in] value value of the DKIM-Signature header "c" tag
 * @param[in,out] result DKIM header data, of which the fields dkim_header_t::body_canon_relaxed and
 *                dkim_header_t::hdr_canon_relaxed are updated on success
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE on error
 * @sa RFC 6376 sect. 3.5, item "c"
 */
static gboolean
tag_get_c(gchar *value, dkim_header_t *result, GError **error)
{
	gboolean success = TRUE;
	gchar *slash;

	slash = strchr(value, '/');
	if (slash != NULL) {
		if (strcmp(&slash[1], "relaxed") == 0) {
			result->body_canon_relaxed = TRUE;
		} else if (strcmp(&slash[1], "simple") != 0) {
			g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("unsupported  canonicalization mode “%s”"), value);
			success = FALSE;
		}
		slash[0] = '\0';
	}

	if (success) {
		if (strcmp(value, "relaxed") == 0) {
			result->hdr_canon_relaxed = TRUE;
		} else if (strcmp(value, "simple") != 0) {
			g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("unsupported  canonicalization mode “%s”"), value);
			success = FALSE;
		}
	}
	return success;
}


/** @brief Extract an integer number from a DKIM-Signature header tag
 *
 * @param[in] value number as decimal string
 * @param[out] target filled with the converted number success
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE on error, i.e. if the passed value is not a valid number
 * @sa RFC 6376 sect. 3.5, items "l", "t" and "x"
 */
static gboolean
tag_get_number(gchar *value, gint64 *target, GError **error)
{
	gboolean success = TRUE;
	gchar *endptr;

	*target = strtol(value, &endptr, 10);
	if ((*endptr != '\0') || (*target < 0)) {
		g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("bad numerical value “%s”"), value);
		success = FALSE;
	}
	return success;
}


/** @brief Extract a time stamp from a DKIM-Signature header tag
 *
 * @param[in] value timestamp, given as seconds since epoch
 * @param[out] timestamp filled with the time stamp on success
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE on error, i.e. if the passed value is not a valid number
 * @sa RFC 6376 sect. 3.5, items "t" and "x", tag_get_number()
 */
static gboolean
tag_get_timestamp(gchar *value, GDateTime **timestamp, GError **error)
{
	gboolean success;
	gint64 time_secs;

	success = tag_get_number(value, &time_secs, error);
	if (success) {
		*timestamp = g_date_time_new_from_unix_utc(time_secs);
	}
	return success;
}


/** @brief Set DKIM error status
 *
 * @param[in,out] dkim_header DKIM header data, of which the fields dkim_header_t::status and dkim_header_t::detail are updated
 * @param[in] status new dkim_header_t::status value
 * @param[in] format printf()-like format string
 * @return always FALSE
 * @note It is a programming error to call this function is dkim_header_t::detail is not NULL.  The function calls g_assert() in
 *       this case.
 */
static gboolean
dkim_error(dkim_header_t *dkim_header, guint status, const gchar *format, ...)
{
	va_list va_args;

	va_start(va_args, format);
	dkim_header->status = status;
	g_assert(dkim_header->detail == NULL);
	dkim_header->detail = g_strdup_vprintf(format, va_args);
	va_end(va_args);
	return FALSE;
}


/** @brief Free DKIM header data
 *
 * @param[in] dkim_header the DKIM header data to free
 */
static void
dkim_header_free(dkim_header_t *dkim_header)
{
	if (dkim_header != NULL) {
		g_free(dkim_header->b.data);
		g_free(dkim_header->bh.data);
		g_free(dkim_header->d);
		g_strfreev(dkim_header->h);
		g_free(dkim_header->s);
		if (dkim_header->t != NULL) {
			g_date_time_unref(dkim_header->t);
		}
		if (dkim_header->x != NULL) {
			g_date_time_unref(dkim_header->x);
		}
		g_free(dkim_header->detail);
		g_free(dkim_header);
	}
}


/** @brief Create a verbose DKIM and DMARC status string
 *
 * @param[in] dkim_headers list of @ref dkim_header_t check results items
 * @param[in] dmarc_header header related to the DMARC signature, may be @c NULL
 * @return the verbose status string
 */
static gchar *
dkim_status_full(GList *dkim_headers, const dkim_header_t *dmarc_header)
{
	GString *result;
	GList *p;

	result = g_string_new(NULL);
	if (dmarc_header != NULL) {
		guint len;

		/* Translators: please do not translate DMARC */
		g_string_append(result, _("DMARC signature:"));
		dkim_status_append(result, dmarc_header);
		len = g_list_length(dkim_headers) - 1U;
		if (len > 0U) {
			g_string_append_printf(result,
				/* Translators: please do not translate DKIM */
				ngettext("\n%u additional DKIM signature:", "\n%u additional DKIM signatures:", len), len);
		}
	}

	for (p = dkim_headers; p != NULL; p = p->next) {
		if (dmarc_header != p->data) {
			dkim_status_append(result, (const dkim_header_t *) p->data);
		}
	}

	if (result->len == 0U) {
		/* Translators: please do not translate DKIM */
		g_string_append(result, _("No DKIM signatures"));
	}
	return g_string_free(result, FALSE);
}


/** @brief Append DKIM status details to a string
 *
 * @param[in,out] status string buffer to which the status is appended
 * @param[in] header DKIM header check results
 */
static void
dkim_status_append(GString *status, const dkim_header_t *header)
{
	if (status->len > 0) {
		g_string_append_c(status, '\n');
	}
	/* Translators: #1 DKIM Signing Domain Identifier (SDID), #2 signature status */
	g_string_append_printf(status, _("\342\200\242 Signing domain “%s”: %s"), header->d, dkim_stat_str(header->status));
	if (header->t != NULL) {
		gchar *dt_buf;

		dt_buf = g_date_time_format(header->t, "%x %X");
		g_string_append_printf(status, _(", created on %s"), dt_buf);
		g_free(dt_buf);
	}
	if (header->x != NULL) {
		GDateTime *now;
		gchar *dt_buf;

		now = g_date_time_new_now_local();
		dt_buf = g_date_time_format(header->x, "%x %X");
		if (g_date_time_compare(header->x, now) <= 0) {
			g_string_append_printf(status, _(", expired on %s"), dt_buf);
		} else {
			g_string_append_printf(status, _(", expires on %s"), dt_buf);
		}
		g_free(dt_buf);
		g_date_time_unref(now);
	}
	if (header->detail != NULL) {
		g_string_append_printf(status, "\n   %s", header->detail);
	}
}


/** @brief Print the DKIM status as human-readable string
 *
 * @param[in] status DKIM status (@ref DKIM_SUCCESS, etc.)
 * @return the status as string
 * @note The function throws an assertion iff the state is not in the range @ref DKIM_SUCCESS .. @ref DKIM_FAILED
 */
static inline const gchar *
dkim_stat_str(gint status) {
	static const gchar *stat_str[3] = {
		N_("valid"),
		N_("warning"),
		N_("invalid")
	};

	if ((status >= DKIM_SUCCESS) && (status <= DKIM_FAILED)) {
		return _(stat_str[status]);
	}
	g_assert_not_reached();
	return NULL;			/* never reached, make the compiler happy */
}


/** @brief Get the DMARC mode for a From: address
 *
 * @param[in] from From: address
 * @param[out] dmarc_domain filled with the domain extracted from the From: address, converted to lower-case
 * @return the DMARC mode (&gt; 0) on success
 *
 * A DMARC policy can be checked iff the passed From: address contains a single mailbox with a non-empty domain part (see RFC 7489,
 * sect. 6.6.1).  Check if a policy for the domain already exists in the cache and return it.  Otherwise, run dns_lookup_txt() to
 * perform the DMARC DNS TXT lookup.  If the lookup does not find a result, continue with sub-domains until either a result is
 * found, or the remaining domain is too short.
 */
static guint
dmarc_dns_lookup(InternetAddressList *from, gchar **dmarc_domain)
{
	const gchar *domain = NULL;
	guint result = DMARC_POLICY_UNKNOWN;

	if (internet_address_list_length(from) != 1) {
		g_debug("%s: %d From: addresses", __func__, internet_address_list_length(from));
	} else {
		InternetAddress *from_addr;

		from_addr = internet_address_list_get_address(from, 0);
		if (!INTERNET_ADDRESS_IS_MAILBOX(from_addr)) {
			g_debug("%s: From: address is not a mailbox", __func__);
		} else {
			const gchar *addr_spec;

			addr_spec = internet_address_mailbox_get_addr(INTERNET_ADDRESS_MAILBOX(from_addr));
			if (addr_spec == NULL) {
				g_debug("%s: From: address has no addr-spec", __func__);
			} else {
				domain = strchr(addr_spec, '@');
				if ((domain == NULL) || (domain[1] == '\0')) {
					g_debug("%s: From: address '%s' has no domain-part", __func__, addr_spec);
					domain = NULL;
				} else {
					domain = &domain[1];
					*dmarc_domain = g_ascii_strdown(domain, -1);
				}
			}
		}
	}

	if (domain != NULL) {
		const gchar *lookup_res;

		G_LOCK(dmarc_dns_cache);
		if (dmarc_dns_cache == NULL) {
			g_debug("%s: create DMARC policy cache", __func__);
			atexit(dmarc_dns_cache_cleanup);
			dmarc_dns_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		}
		lookup_res = g_hash_table_lookup(dmarc_dns_cache, domain);
		G_UNLOCK(dmarc_dns_cache);

		if (lookup_res != NULL) {
			result = (guint) lookup_res[0];
			g_debug("%s: load DMARC policy for %s from cache", __func__, domain);
		} else {
			const gchar *dom_start;
			gpointer subdom = NULL;

			dom_start = domain;
			do {
				gchar *rrname;
				GError *local_err = NULL;

				rrname = g_strconcat("_dmarc.", dom_start, NULL);
				result = GPOINTER_TO_UINT(dns_lookup_txt(rrname, (dns_eval_fn) eval_dmarc_dns_txt, subdom, &local_err));
				g_free(rrname);
				if (result != DMARC_POLICY_UNKNOWN) {
					G_LOCK(dmarc_dns_cache);
					g_debug("%s: add DMARC policy for %s to cache", __func__, domain);
					(void) g_hash_table_insert(dmarc_dns_cache, g_strdup(domain),
						g_strdup_printf("%c%s", (gchar) result, dom_start));
					G_UNLOCK(dmarc_dns_cache);
				} else if (g_error_matches(local_err, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND)) {
					/* try the next sub-domain iff it still contains at least one '.' */
					dom_start = strchr(dom_start, '.');
					if ((dom_start != NULL) && (strchr(&dom_start[1], '.') != NULL)) {
						dom_start = &dom_start[1];
						subdom = GUINT_TO_POINTER(1U);
					} else {
						dom_start = NULL;			/* give up */
					}
				} else {
					g_debug("%s: %s", __func__, local_err->message);
					dom_start = NULL;				/* other error: give up */
				}
				g_clear_error(&local_err);
			} while ((result == DMARC_POLICY_UNKNOWN) && (dom_start != NULL));
		}
	}

	return result;
}


/** @brief Parse the DMARC DNS TXT record
 *
 * @param[in] txt_str DKIM DNS TXT record
 * @param[in] subdomain NULL if the domain policy shall be returned, != NULL for the subdomain policy
 * @param[out] error location for error, may be NULL
 * @return the DMARC policy, cast'ed to a pointer, on success, NULL on error
 * @note Callback function called from dns_lookup_txt().
 * @sa RFC 7489 sect. 6.3
 */
static gpointer
eval_dmarc_dns_txt(const gchar *txt_str, gpointer subdomain, GError **error)
{
	gchar **items;
	gint n;
	gboolean success = TRUE;
	guint tag_p = DMARC_POLICY_UNKNOWN;
	guint tag_sp = DMARC_POLICY_UNKNOWN;
	gchar tag_adkim = 'x';
	guint result = DMARC_POLICY_UNKNOWN;

	/* the TXT record may end with an empty item (RFC 7489, sect. 6.4) which we must ignore */
	g_debug("%s: %s", __func__, txt_str);
	items = strsplit_clean(txt_str, ";", TRUE);

	for (n = 0; success && (items[n] != NULL); n++) {
		gchar *value;

		value = strchr(items[n], '=');
		if ((value == NULL) || (value == items[n])) {
			/* Translators: please do not translate "DMARC DNS TXT" */
			g_set_error(error, DKIM_ERROR_QUARK, DMARC_DNS_ERROR, _("malformed DMARC DNS TXT record"));
			success = FALSE;
		} else {
			*value++ = '\0';
			items[n] = g_strstrip(items[n]);
			value = g_strstrip(value);
			if (strcmp(items[n], "v") == 0) {				/* version, REQUIRED, must be the 1st element and match "DMARC1" */
				if ((n != 0) || (strcmp(value, "DMARC1") != 0)) {
					/* Translators: please do not translate "DMARC DNS TXT" */
					g_set_error(error, DKIM_ERROR_QUARK, DMARC_DNS_ERROR, _("malformed DMARC DNS TXT record: bad version"));
					success = FALSE;
				}
			} else if (strcmp(items[n], "p") == 0) {		/* policy, REQUIRED */
				success = dmarc_get_policy(&tag_p, value, "p", error);
			} else if (strcmp(items[n], "sp") == 0) {		/* subdomain policy, OPTIONAL, default same as policy */
				success = dmarc_get_policy(&tag_sp, value, "sp", error);
			} else if (strcmp(items[n], "adkim") == 0) {	/* DKIM mode, optional, default relaxed */
				if (tag_adkim != 'x') {
					g_set_error(error, DKIM_ERROR_QUARK, DMARC_DNS_ERROR,
						/* Translators: please do not translate "DMARC DNS TXT" */
						_("malformed DMARC DNS TXT record: duplicated “%s” tag"), "x");
					success = FALSE;
				} else if ((value[1] != '\0') || ((value[0] != 'r') && (value[0] != 's'))) {
					g_set_error(error, DKIM_ERROR_QUARK, DMARC_DNS_ERROR,
						/* Translators: please do not translate "DMARC DNS TXT" */
						_("malformed DMARC DNS TXT record: duplicated “x” tag value “%s”"), value);
					success = FALSE;
				} else {
					tag_adkim = value[0];
				}
			} else {
				g_debug("%s: ignore tag %s", __func__, items[n]);
			}
		}
	}
	g_strfreev(items);

	/* evaluate */
	if (success) {
		if (tag_p == DMARC_POLICY_UNKNOWN) {
			/* Translators: please do not translate "DMARC DNS TXT" */
			g_set_error(error, DKIM_ERROR_QUARK, DMARC_DNS_ERROR, _("malformed DMARC DNS TXT record: mandatory tag “p” missing"));
		} else {
			if (subdomain == NULL) {		/* domain policy */
				result = tag_p;
			} else {
				result = (tag_sp != DMARC_POLICY_UNKNOWN) ? tag_sp : tag_p;
			}
			if (tag_adkim == 's') {
				result |= DMARC_DKIM_STRICT;
			}
		}
	}
	return GUINT_TO_POINTER(result);
}


/** @brief Extract DMARC policy
 *
 * @param[in,out] dest policy destination value, @em MUST contain @ref DMARC_POLICY_UNKNOWN, indicates duplicated tag otherwise
 * @param[in] value DNS TXT tag value
 * @param[in] tag_name tag ID, used for producing an error message only
 * @param[out] error location for error, may be NULL
 * @return TRUE on success, FALSE on error
 */
static gboolean
dmarc_get_policy(guint *dest, const gchar *value, const gchar *tag_name, GError **error)
{
	static const gchar *policy[3] = {"none", "quarantine", "reject"};
	gboolean result = FALSE;

	if (*dest != DMARC_POLICY_UNKNOWN) {
		/* Translators: please do not translate "DMARC DNS TXT" */
		g_set_error(error, DKIM_ERROR_QUARK, DMARC_DNS_ERROR, _("malformed DMARC DNS TXT record: duplicated “%s” tag"), tag_name);
	} else {
		guint n;

		for (n = 0U; (n < 3) && (strcmp(value, policy[n]) != 0); n++) {
			/* nothing to do */
		}
		if (n < 3) {
			*dest = n + 1U;
			result = TRUE;
		} else {
			g_set_error(error, DKIM_ERROR_QUARK, DMARC_DNS_ERROR, _("malformed DMARC DNS TXT record: bad policy “%s”"), value);
		}
	}
	return result;
}


/** @brief Get the DKIM public key
 *
 * @param[in,out] dkim_header DKIM message header data
 * @return the DKIM public key as GnuTLS object on success, or NULL on error
 *
 * Check if the DKIM public key for the (selector, domain, crypto algorithm, hash algorithm) already exists in the cache and return
 * it.  Otherwise, run dns_lookup_txt() to perform the DKIM DNS TXT lookup.
 *
 * @sa RFC 6376 sect. 6.1.2
 */
static gnutls_pubkey_t
dkim_get_pubkey(dkim_header_t *dkim_header)
{
	gchar *rrname;
	gchar *hash_key;
	gnutls_pubkey_t result;

	rrname = g_strconcat(dkim_header->s, "._domainkey.", dkim_header->d, NULL);
	hash_key = g_strdup_printf("%s:%s:%d:%d", dkim_header->s, dkim_header->d, dkim_header->cryptalg, dkim_header->hashalg);

	G_LOCK(dkim_dns_cache);
	if (dkim_dns_cache == NULL) {
		g_debug("%s: create DKIM pubkey cache", __func__);
		atexit(dkim_dns_cache_cleanup);
		dkim_dns_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) gnutls_pubkey_deinit);
	}
	result = g_hash_table_lookup(dkim_dns_cache, hash_key);
	G_UNLOCK(dkim_dns_cache);

	if (result != NULL) {
		g_debug("%s: load DKIM pubkey for %s from cache", __func__, rrname);
	} else {
		GError *local_err = NULL;

		result = dns_lookup_txt(rrname, (dns_eval_fn) eval_dkim_dns_txt, dkim_header, &local_err);
		if (result != NULL) {
			G_LOCK(dkim_dns_cache);
			g_debug("%s: add DKIM pubkey for %s to cache", __func__, hash_key);
			(void) g_hash_table_insert(dkim_dns_cache, g_strdup(hash_key), result);
			G_UNLOCK(dkim_dns_cache);
		} else {
			dkim_header->status = DKIM_FAILED;
			/* Translators: please do not translate "DKIM" */
			dkim_header->detail = g_strdup_printf(_("DKIM public key lookup failed: %s"), local_err->message);
			g_error_free(local_err);
		}
	}
	g_free(hash_key);
	g_free(rrname);

	return result;
}


/** @brief Parse the DKIM DNS TXT record
 *
 * @param[in] txt_str DKIM DNS TXT record
 * @param[in] dkim_header DKIM message header data
 * @param[out] error location for error, may be NULL
 * @return a newly allocated GnuTLS public key object extracted from the DKIM DNS TXT record on success, NULL on error
 * @note Callback function called from dns_lookup_txt().
 * @sa RFC 6376 sect. 3.6.1
 */
static gnutls_pubkey_t
eval_dkim_dns_txt(const gchar *txt_str, const dkim_header_t *dkim_header, GError **error)
{
	gchar **items;
	gint n;
	GString *seen;
	gboolean success = TRUE;
	gnutls_datum_t key_input = {NULL, 0};
	gnutls_pubkey_t result = NULL;

	/* the TXT record may end with an empty item (RFC 6376, sect. 3.2) which we must ignore */
	g_debug("%s: %s", __func__, txt_str);
	items = strsplit_clean(txt_str, ";", TRUE);

	/* remember the already seen tags so we can catch broken TXT records with duplicated ones (bh == B) */
	seen = g_string_sized_new(16U);

	for (n = 0; success && (items[n] != NULL); n++) {
		gchar *value;

		value = strchr(items[n], '=');
		if ((value == NULL) || (value == items[n])) {
			/* Translators: please do not translate "DKIM DNS TXT" */
			g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("malformed DKIM DNS TXT record"));
			success = FALSE;
		} else {
			size_t tag_len;

			*value++ = '\0';
			items[n] = g_strstrip(items[n]);
			value = g_strstrip(value);
			tag_len = strlen(items[n]);

			if (tag_len == 1U) {
				/* check for duplicates */
				if (strchr(seen->str, items[n][0]) != NULL) {
					g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("duplicated tag “%s”"), items[n]);
					success = FALSE;
				} else {
					gchar **elems;

					g_string_append_c(seen, items[n][0]);
					switch (items[n][0]) {
					case 'v':
						/* RECOMMENDED, must be the 1st item */
						if ((n != 0) || (strcmp(value, "DKIM1") != 0)) {
							/* Translators: please do not translate "DKIM DNS TXT" */
							g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("malformed DKIM DNS TXT record: bad version"));
							success = FALSE;
						}
						break;
					case 'h':
						/* OPTIONAL, default all hash algorithms */
						elems = strsplit_clean(value, ":", FALSE);
						if (((dkim_header->hashalg == G_CHECKSUM_SHA1) &&
							!g_strv_contains((const char * const *) elems, "sha1")) ||
							((dkim_header->hashalg == G_CHECKSUM_SHA256) &&
							 !g_strv_contains((const char * const *) elems, "sha256"))) {
							g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("bad key hash algorithm"));
							success = FALSE;
						}
						g_strfreev(elems);
						break;
					case 'k':
						/* OPTIONAL, default is rsa */
						if (((dkim_header->cryptalg == GNUTLS_SIGN_EDDSA_ED25519) && (strcmp(value, "ed25519") != 0)) ||
							((dkim_header->cryptalg != GNUTLS_SIGN_EDDSA_ED25519) && (strcmp(value, "rsa") != 0))) {
							g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("bad key crypto algorithm “%s”"),
								value);
							success = FALSE;
						}
						break;
					case 'p':
						/* REQUIRED, empty if the key has been revoked */
						if (value[0] == '\0') {
							g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("public key has been revoked"));
							success = FALSE;
						} else {
							gsize length;

							key_input.data = g_base64_decode(value, &length);
							key_input.size = length;
						}
						break;
					case 's':
						/* OPTIONAL, default any service type */
						elems = strsplit_clean(value, ":", FALSE);
						if (!g_strv_contains((const char * const *) elems, "email") &&
							!g_strv_contains((const char * const *) elems, "*")) {
							g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("unsupported service type “%s”"),
								value);
							success = FALSE;
						}
						g_strfreev(elems);
						break;
					case 't':
						/* The RFC is not completely clear about using the 's' value of this flag (compare with sect. 3,5, i flag),
						 * so we just do not evaluate it for simplicity. */
					default:
						g_debug("%s: ignore tag '%s'", __func__, items[n]);
					}
				}
			} else {
				g_debug("%s: ignore tag '%s'", __func__, items[n]);
			}
		}
	}

	/* validity checks */
	if (success && (key_input.data == NULL)) {
		/* Translators: please do not translate "DKIM DNS TXT" */
		g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("malformed DKIM DNS TXT record: pubkey missing"));
		success = FALSE;
	}
	if (success && (dkim_header->cryptalg == GNUTLS_SIGN_EDDSA_ED25519) && (strchr(seen->str, 'k') == NULL)) {
		g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("bad key crypto algorithm"));
		success = FALSE;
	}

	/* load the public key into a GnuTLS object */
	if (success) {
		int status;

		status = gnutls_pubkey_init(&result);
		if (status != GNUTLS_E_SUCCESS) {
			g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("cannot initialize public key buffer: %s"),
				gnutls_strerror(status));
		} else {
			if (dkim_header->cryptalg == GNUTLS_SIGN_EDDSA_ED25519) {
				status = gnutls_pubkey_import_ecc_raw(result, GNUTLS_ECC_CURVE_ED25519, &key_input, NULL);
			} else {
				status = gnutls_pubkey_import(result, &key_input, GNUTLS_X509_FMT_DER);
			}
			if (status != GNUTLS_E_SUCCESS) {
				g_set_error(error, DKIM_ERROR_QUARK, DKIM_FAILED, _("cannot load public key: %s"),
					gnutls_strerror(status));
			} else {
				gnutls_datum_t out;

				gnutls_pubkey_print(result, GNUTLS_CRT_PRINT_FULL, &out);
				g_debug("%s: pubkey:\n%s", __func__, (const char *) out.data);
				gnutls_free(out.data);
			}
		}
	}

	/* clean up */
	g_free(key_input.data);
	g_string_free(seen, TRUE);
	g_strfreev(items);
	return result;
}


/** @brief Perform a DNS TXT record lookup
 *
 * @param[in] rrname the DNS name to look up
 * @param[in] callback callback function for evaluating the TXT record
 * @param[in] user_data user data, passed to the callback
 * @param[out] error location for error, may be NULL
 * @return the return value of the callback function on success, NULL if the lookup failed
 * @note If the DNS lookup returns more than one record, the function returns with the first successful result.
 */
static gpointer
dns_lookup_txt(const gchar *rrname, dns_eval_fn callback, gconstpointer user_data, GError **error)
{
	GResolver *resolver;
	GList *lookup_res;
	gpointer result = NULL;

	resolver = g_resolver_get_default();
	lookup_res = g_resolver_lookup_records(resolver, rrname, G_RESOLVER_RECORD_TXT, NULL, error);
	g_debug("%s: lookup '%s': %p", __func__, rrname, lookup_res);
	if (lookup_res != NULL) {
		GList *p;
		GString *dns_txt;
		GError *this_err = NULL;

		dns_txt = g_string_new(NULL);
		for (p = lookup_res; (result == NULL) && (p != NULL); p = p->next) {
			GVariant *value = (GVariant *) p->data;
			GVariantIter *iter;
			const gchar *txt_item;

			g_string_truncate(dns_txt, 0U);
			g_variant_get(value, "(as)", &iter);
			while (g_variant_iter_next(iter, "&s", &txt_item)) {
				g_string_append(dns_txt, txt_item);
			}
			g_variant_iter_free(iter);
			result = callback(dns_txt->str, user_data, &this_err);
			if (result == NULL) {
				g_debug("%s: %s", __func__, (this_err != NULL) ? this_err->message : "???");
				if (p->next == NULL) {
					g_propagate_error(error, this_err);
				} else {
					g_clear_error(&this_err);
				}
			}
		}
		g_string_free(dns_txt, TRUE);
		g_list_free_full(lookup_res, (GDestroyNotify) g_variant_unref);
	}
	g_object_unref(resolver);
	return result;
}


/** @brief Split a string and strip the elements
 *
 * @param[in] value string to split
 * @param[in] delim delimiter string
 * @param[in] strip_empty_last TRUE if an empty item at the end of the list shall be omitted
 * @return NULL-terminated array of strings
 *
 * This functions basically splits the string by calling g_strsplit(), removes leading and trailing whitespace from each element,
 * and if required removes an empty element from the end of the list, unless the resulting list would be empty.
 */
static gchar **
strsplit_clean(const gchar *value, const gchar *delim, gboolean strip_empty_last)
{
	int n;
	gchar **items;

	items = g_strsplit(value, delim, -1);
	for (n = 0; items[n] != NULL; n++) {
		items[n] = g_strstrip(items[n]);
	}
	if (strip_empty_last && (n > 0) && (items[n - 1][0] == '\0')) {
		n--;
		g_free(items[n]);
		items[n] = NULL;
	}
	return items;
}


/** @brief Update a hash optionally limiting the size of input bytes
 *
 * @param[in,out] hash hash object
 * @param[in] data data to add to the hash
 * @param[in] length number of bytes in data
 * @param[in] maxlen the maximum number of bytes to add to the hash, -1 for no limit
 * @return -1 if maxlen is -1, or the maximum number of bytes which may still be added to the hash calculation
 */
static inline gssize
checksum_update_limited(GChecksum *hash, const guchar * data, gssize length, gssize maxlen)
{
	if (maxlen == -1) {
		g_checksum_update(hash, data, length);
	} else if (maxlen > 0) {
		if (length > maxlen) {
			g_checksum_update(hash, data, maxlen);
			maxlen = 0;
		} else {
			g_checksum_update(hash, data, length);
			maxlen -= length;
		}
	} else {
		/* nothing to do */
	}
	return maxlen;
}


/** @brief Release the DMARC DNS cache
 */
static void
dmarc_dns_cache_cleanup(void)
{
	G_LOCK(dmarc_dns_cache);
	if (dmarc_dns_cache != NULL) {
		g_debug("release DMARC DNS cache");
		g_hash_table_unref(dmarc_dns_cache);
		dmarc_dns_cache = NULL;
	}
	G_UNLOCK(dmarc_dns_cache);
}


/** @brief Release the DKIM DNS cache
 */
static void
dkim_dns_cache_cleanup(void)
{
	G_LOCK(dkim_dns_cache);
	if (dkim_dns_cache != NULL) {
		g_debug("release DKIM DNS cache");
		g_hash_table_unref(dkim_dns_cache);
		dkim_dns_cache = NULL;
	}
	G_UNLOCK(dkim_dns_cache);
}
