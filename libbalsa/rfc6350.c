/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others, see the file AUTHORS for a list.
 *
 * This module provides a simple RFC 6350 (aka VCard 4.0, see https://tools.ietf.org/html/rfc6350) parser which extracts a single
 * VCard from a GDataInputStream and returns it as LibBalsaAddress.
 *
 * Written by Copyright (C) 2016 Albrecht Dreß <albrecht.dress@arcor.de>.
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

#if defined(HAVE_OSMO) || defined(HAVE_WEBDAV)

#include <glib/gi18n.h>
#include "rfc6350.h"


/* define GError stuff for the RFC 6350/VCard parser */
#define RFC6350_ERROR_QUARK				(g_quark_from_static_string("rfc6350-parser"))
#define RFC6350_ERROR_NO_COLON			1
#define RFC6350_ERROR_EMPTY				2
#define RFC6350_ERROR_BEGIN				3
#define RFC6350_ERROR_END				4

#define CRLF							"\r\n"


static gboolean rfc6350_eval_line(gchar			  *line,
								  LibBalsaAddress *address,
								  GError		  **error);
static gchar **rfc6350_strsplit(const gchar *item,
								guint       count);
static gchar *rfc6350_get_name(gchar *name);
static void rfc6350_unescape(gchar *item);
static gchar *rfc6350_fn_from_n(gchar **n_items);
static gchar *rfc6350_escape(const gchar *value)
	G_GNUC_WARN_UNUSED_RESULT;
static void rfc6350_add_folded(GString     *strbuf,
							   const gchar *label,
							   const gchar *value);


LibBalsaAddress *
rfc6350_parse_from_stream(GDataInputStream *stream,
						  gboolean		   *eos,
						  GError		   **error)
{
	gchar *line;
	LibBalsaAddress *result = NULL;

	line = g_data_input_stream_read_line_utf8(stream, NULL, NULL, error);
	if (line == NULL) {
		if (*error == NULL) {
			*eos = TRUE;
		}
	} else if (g_ascii_strcasecmp(line, "BEGIN:VCARD") != 0) {
		g_set_error(error, RFC6350_ERROR_QUARK, RFC6350_ERROR_BEGIN, _("malformed card, BEGIN:VCARD expected"));
		g_free(line);
	} else {
		gboolean parse_done = FALSE;

		result = libbalsa_address_new();
		while (result && (line != NULL) && !parse_done) {
			if (g_ascii_strcasecmp(line, "END:VCARD") == 0) {
				parse_done = TRUE;
				g_free(line);
			} else {
				gchar *nextline;

				/* perform unfolding (RFC 6350, sect. 3.2. "Line Delimiting and Folding") */
				nextline = g_data_input_stream_read_line_utf8(stream, NULL, NULL, error);
				while ((nextline) != NULL && ((nextline[0] == ' ') || (nextline[0] == '\t'))) {
					gchar *unfold;

					unfold = g_strconcat(line, &nextline[1], NULL);
					g_free(line);
					g_free(nextline);
					line = unfold;
					nextline = g_data_input_stream_read_line_utf8(stream, NULL, NULL, error);
				}

				/* evaluate unfolded line, drop address on error */
				if (!rfc6350_eval_line(line, result, error)) {
					g_object_unref(result);
					result = NULL;
				}

				/* process next line */
				g_free(line);
				line = nextline;
			}
		}

		if (!parse_done) {
			g_set_error(error, RFC6350_ERROR_QUARK, RFC6350_ERROR_END, _("malformed card, END:VCARD missing"));
			g_object_unref(result);
			result = NULL;
		}
	}

	/* ignore items without an Email address, fill empty full name if necessary */
	if (result != NULL) {
		if (libbalsa_address_get_addr(result) == NULL) {
			g_object_unref(result);
			result = NULL;
		} else if (libbalsa_address_get_full_name(result) == NULL) {
                        libbalsa_address_set_full_name(result, _("No-Name"));
		}
	}

	return result;
}


gchar *
rfc6350_from_address(LibBalsaAddress *address,
					 gboolean         vcard4,
					 gboolean         add_uuid)
{
	GString *vcbuf;
	const gchar *first_name;
	const gchar *last_name;
	gchar *buf;
	gchar *n_last_buf;
	gchar *n_first_buf;
	guint addr_idx;

	g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

	/* header */
	vcbuf = g_string_new("BEGIN:VCARD" CRLF "VERSION:");
	g_string_append_c(vcbuf, vcard4 ? '4' : '3');
	g_string_append(vcbuf, ".0" CRLF);

	/* UUID if requested */
	if (add_uuid) {
		buf = g_uuid_string_random();
		g_string_append_printf(vcbuf, "UID:urn:uuid:%s" CRLF, buf);
		g_free(buf);
	}

	/* FN field, must be present */
	first_name = libbalsa_address_get_first_name(address);
	last_name = libbalsa_address_get_last_name(address);
	if (libbalsa_address_get_full_name(address) != NULL) {
		buf = rfc6350_escape(libbalsa_address_get_full_name(address));
	} else {
		gchar *fn_buf;

		if ((first_name != NULL) && (last_name != NULL)) {
			fn_buf = g_strconcat(first_name, " ", last_name, NULL);
		} else if (first_name != NULL) {
			fn_buf = g_strdup(first_name);
		} else if (last_name != NULL)  {
			fn_buf = g_strdup(last_name);
		} else {
			fn_buf = g_strdup(_("unknown"));
		}
		buf = rfc6350_escape(fn_buf);
		g_free(fn_buf);
	}
	rfc6350_add_folded(vcbuf, "FN", buf);
	g_free(buf);

	/* N field, should be present */
	if (last_name != NULL) {
		n_last_buf = rfc6350_escape(last_name);
	} else {
		n_last_buf = g_strdup("");
	}
	if (first_name != NULL) {
		n_first_buf = rfc6350_escape(first_name);
	} else {
		n_first_buf = g_strdup("");
	}
	buf = g_strconcat(n_last_buf, ";", n_first_buf, ";;;", NULL);
	g_free(n_first_buf);
	g_free(n_last_buf);
	rfc6350_add_folded(vcbuf, "N", buf);
	g_free(buf);

	/* ORG field (optional) */
	if (libbalsa_address_get_organization(address) != NULL) {
		buf = rfc6350_escape(libbalsa_address_get_organization(address));
		rfc6350_add_folded(vcbuf, "ORG", buf);
		g_free(buf);
	}

	/* EMAIL fields */
	for (addr_idx = 0U; addr_idx < libbalsa_address_get_n_addrs(address); addr_idx++) {
		buf = rfc6350_escape(libbalsa_address_get_nth_addr(address, addr_idx));
		rfc6350_add_folded(vcbuf, "EMAIL", buf);
		g_free(buf);
	}

	/* footer */
	g_string_append(vcbuf, "END:VCARD" CRLF);

	return g_string_free(vcbuf, FALSE);
}


/** \brief Extract a VCard name item
 *
 * \param name input name field, modified in place
 * \return the name field, with group and parameters stripped
 *
 * Remove the \em group and \em param parts of the VCard \em name field (see RFC 6350, sect. 3.3. "ABNF Format Definition").
 *
 * \note Do \em not free the returned value.
 */
static gchar *
rfc6350_get_name(gchar *name)
{
	gchar *result;
	gchar *semicolon;

	/* skip group */
	result = strchr(name, '.');
	if (result == NULL) {
		result = name;
	} else {
		result = &result[1];
	}

	/* drop any name parameters */
	semicolon = strchr(result, ';');
	if (semicolon != NULL) {
		semicolon[0] = '\0';
	}

	return result;
}


/** \brief RFC 6350 unescape a string
 *
 * \param item VCard item, modified in place
 *
 * Unescape a string according to RFC 6350, sect. 3.4. "Property Value Escaping".  Note that all other escaped characters than those
 * defined in RFC 6350 are simply ignored, although they \em should be regarded as errors.
 *
 * \note Do \em not free the returned value.
 */
static void
rfc6350_unescape(gchar *item)
{
	gchar *p;
	gchar *bslash;

	g_assert(item != NULL);

	p = item;
	do {
		bslash = strchr(p, '\\');
		if (bslash != NULL) {
			if (strchr(",;\\nN", bslash[1]) != NULL) {
				if (g_ascii_tolower(bslash[1]) == 'n') {
					bslash[1] = '\n';
				}
				memmove(bslash, &bslash[1], strlen(bslash));
			}
			p = &bslash[1];
		} else {
			p = NULL;
		}
	} while (p != NULL);
}


/** \brief Split a ';' delimited list into items
 *
 * \param item value string consisting of ';' delimited items
 * \param count maximum number of fields to split into
 * \return a newly allocated NULL-terminated array of unescaped fields
 *
 * Split the passed value string into fields, and unescape the resulting values.  If the \em item contains more than \em count
 * fields, the extra delimiters are ignored.
 *
 * \note Free the returned value by calling g_strfreev() on it.
 */
static gchar **
rfc6350_strsplit(const gchar *item,
				 guint 		 count)
{
	gchar **result;
	const gchar *start;
	guint index;

	result = g_new0(gchar *, count + 1U);
	start = item;
	index = 0U;

	while ((start != NULL) && (index < count)) {
		if (start[0] == ';') {
			result[index] = g_strdup("");
			start = &start[1];
		} else {
			const gchar *delim;

			delim = strchr(start, ';');
			while ((delim != NULL) && (delim[-1] == '\\')) {
				delim = strchr(&delim[1], ';');
			}
			if (delim != NULL) {
				result[index] = g_strndup(start, delim - start);
				start = &delim[1];
			} else {
				result[index] = g_strdup(start);
				start = NULL;
			}
			rfc6350_unescape(result[index]);
		}
		index++;
	}

	return result;
}


/** \brief Evaluate a VCard line
 *
 * \param line VCard line
 * \param address target address object
 * \param error filled with error information on error
 * \return TRUE on success, or FALSE is the VCard line is malformed
 *
 * Evaluate the VCard line, extract a N, FN, NICKNAME, ORG or EMAIL item and assign it to the appropriate fields in the target
 * address item.
 */
static gboolean
rfc6350_eval_line(gchar			  *line,
				  LibBalsaAddress *address,
				  GError		  **error)
{
	gchar *value;
	gboolean result;

	/* split into name and value */
	value = strchr(line, ':');
	if (value == NULL) {
		g_set_error(error, RFC6350_ERROR_QUARK, RFC6350_ERROR_NO_COLON, _("malformed line “%s”, missing “:”"), line);
		result = FALSE;
	} else {
		gchar *namepart;
		gchar *name;

		/* get the name and make sure that neither name nor value are empty */
		namepart = g_strndup(line, value - line);
		name = rfc6350_get_name(namepart);
		value = &value[1];
		if ((name[0] == '\0') || (value[0] == '\0')) {
			g_set_error(error, RFC6350_ERROR_QUARK, RFC6350_ERROR_EMPTY, _("malformed line “%s”, empty name or value"), line);
		} else {
			g_debug("%s: line='%s' name='%s', value='%s'", __func__, line, name, value);
			if (g_ascii_strcasecmp(name, "FN") == 0) {
				rfc6350_unescape(value);
                                libbalsa_address_set_full_name(address, value);
			} else if (g_ascii_strcasecmp(name, "N") == 0) {
				gchar **n_items;

				n_items = rfc6350_strsplit(value, 5U);
                                libbalsa_address_set_first_name(address, n_items[1]);
                                libbalsa_address_set_last_name(address, n_items[0]);
				if (libbalsa_address_get_full_name(address) == NULL) {
                                        libbalsa_address_set_full_name
                                            (address, rfc6350_fn_from_n(n_items));
				}
				g_strfreev(n_items);
			} else if (g_ascii_strcasecmp(name, "NICKNAME") == 0) {
				rfc6350_unescape(value);
                                libbalsa_address_set_nick_name(address, value);
			} else if (g_ascii_strcasecmp(name, "ORG") == 0) {
				gchar **n_items;

				n_items = rfc6350_strsplit(value, 2U);
                                libbalsa_address_set_organization(address, n_items[0]);
				g_strfreev(n_items);
			} else if (g_ascii_strcasecmp(name, "EMAIL") == 0) {
				rfc6350_unescape(value);
                                libbalsa_address_append_addr(address, value);
			} else {
				/* ignore any other items */
			}
		}
		g_free(namepart);
		result = TRUE;
	}

	return result;
}


/** \brief Create a full name from VCard "N" items
 *
 * \param n_items extracted VCard N items
 * \return the full name
 *
 * Construct a full name by concatenating -in this order- the VCard "N" elements Honorific Prefixes, Given Names, Additional Names,
 * Family Names and Honorific Suffixes, separated by a single space each.
 */
static gchar *
rfc6350_fn_from_n(gchar **n_items)
{
	GString *fn;
	static const guint add_idx[5] = { 3U, 1U, 2U, 0U, 4U };
	guint n;

	fn = g_string_new(NULL);

	for (n = 0; n < 5U; n++) {
		if ((n_items[add_idx[n]] != NULL) && (n_items[add_idx[n]][0] != '\0')) {
			if (fn->len > 0U) {
				fn = g_string_append_c(fn, ' ');
			}
			fn = g_string_append(fn, n_items[add_idx[n]]);
		}
	}

	return g_string_free(fn, FALSE);
}


/** \brief RFC 6350 escape a string
 *
 * \param[in] value string to escape
 * \return the newly allocated, escaped string
 *
 * Escape a string according to RFC 6350, sect. 3.4. "Property Value Escaping".
 */
static gchar *
rfc6350_escape(const gchar *value)
{
	GString *buf;
	const gchar *p;

	buf = g_string_sized_new(strlen(value));
	p = value;
	do {
		size_t chunk;

		chunk = strcspn(p, ";,\\\n");
		if (chunk > 0U) {
			g_string_append_len(buf, p, chunk);
			p = &p[chunk];
		}
		if (*p != '\0') {
			g_string_append_c(buf, '\\');
			g_string_append_c(buf, *p);
			p = &p[1];
		}
	} while (*p != '\0');
	return g_string_free(buf, FALSE);
}


/** \brief RFC 6350 fold a VCard entry
 *
 * \param[in,out] strbuf VCard buffer
 * \param[in] label VCard item label
 * \param[in] value VCard item value
 *
 * Append the passed label and value to the VCard string buffer, folding it according to RFC 6350, sect. 3.2. "Line Delimiting and
 * Folding".
 */
static void
rfc6350_add_folded(GString     *strbuf,
				   const gchar *label,
				   const gchar *value)
{
	long int label_offs;
	const gchar *p;

	p = value;
	g_string_append_printf(strbuf, "%s:", label);
	label_offs = strlen(label) + 1L;
	do {
		const gchar *chunk_end;

		chunk_end = g_utf8_next_char(p);
		do {
			chunk_end = g_utf8_next_char(chunk_end);
		} while ((*chunk_end != '\0') && ((chunk_end - p) <= (71 - label_offs)));
		g_string_append_len(strbuf, p, chunk_end - p);
		if (*chunk_end != '\0') {
			g_string_append(strbuf, CRLF " ");
		}
		label_offs = 0;
		p = chunk_end;
	} while (*p != '\0');
	g_string_append(strbuf, CRLF);
}


#endif /* HAVE_OSMO  || HAVE_WEBDAV */
