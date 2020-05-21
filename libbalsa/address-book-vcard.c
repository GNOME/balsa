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
 * A VCard (eg GnomeCard) addressbook.
 * assumes that the file charset is in current locale.
 * the strings are converted to UTF-8.
 * FIXME: verify assumption agains RFC.
 * Obviously, the best method would be to have file encoded in UTF-8.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address-book-vcard.h"

#include <glib/gi18n.h>

/* FIXME: Perhaps the whole thing could be rewritten to use a g_scanner ?? */

/* FIXME: Arbitrary constant */
#define LINE_LEN 256

static LibBalsaABErr
libbalsa_address_book_vcard_parse_address(FILE * stream_in,
                                          LibBalsaAddress * address_in,
                                          FILE * stream_out,
                                          LibBalsaAddress * address_out);
static LibBalsaABErr
libbalsa_address_book_vcard_save_address(FILE * stream,
                                         LibBalsaAddress * address);

struct _LibBalsaAddressBookVcard {
    LibBalsaAddressBookText parent;
};

G_DEFINE_TYPE(LibBalsaAddressBookVcard, libbalsa_address_book_vcard,
              LIBBALSA_TYPE_ADDRESS_BOOK_TEXT)

static void
libbalsa_address_book_vcard_class_init(LibBalsaAddressBookVcardClass *
				       klass)
{
    LibBalsaAddressBookTextClass *address_book_text_class =
        LIBBALSA_ADDRESS_BOOK_TEXT_CLASS(klass);

    address_book_text_class->parse_address =
        libbalsa_address_book_vcard_parse_address;
    address_book_text_class->save_address =
        libbalsa_address_book_vcard_save_address;
}

static void
libbalsa_address_book_vcard_init(LibBalsaAddressBookVcard * ab_vcard)
{
}

/* Public method */
LibBalsaAddressBook *
libbalsa_address_book_vcard_new(const gchar * name, const gchar * path)
{
    LibBalsaAddressBookVcard *ab_vcard;
    LibBalsaAddressBook *ab;

    ab_vcard =
        LIBBALSA_ADDRESS_BOOK_VCARD(g_object_new
                                    (LIBBALSA_TYPE_ADDRESS_BOOK_VCARD,
                                     NULL));
    ab = LIBBALSA_ADDRESS_BOOK(ab_vcard);

    libbalsa_address_book_set_name(ab, name);
    libbalsa_address_book_text_set_path(LIBBALSA_ADDRESS_BOOK_TEXT(ab), path);

    return ab;
}

static gchar *
validate_vcard_string(gchar * vcstr)
{
    gchar * utf8res;
    gsize b_written;

    /* check if it's a utf8 clean string and return it in this case */
    if (!vcstr || g_utf8_validate(vcstr, -1, NULL))
	return vcstr;

    /* try to convert from the user's locale setting */
    utf8res = g_locale_to_utf8(vcstr, -1, NULL, &b_written, NULL);
    if (!utf8res)
	return vcstr;

    g_free(vcstr);
    return utf8res;
}

static void
lbab_vcard_write_begin(FILE * stream)
{
    fprintf(stream, "\nBEGIN:VCARD\n");
}

static void
lbab_vcard_write_fn(FILE * stream, LibBalsaAddress * address)
{
    const gchar *full_name = libbalsa_address_get_full_name(address);

    if (full_name != NULL && full_name[0] != '\0')
        fprintf(stream, "FN:%s\n", full_name);
}

static void
lbab_vcard_write_n(FILE * stream, LibBalsaAddress * address)
{
    const gchar *first_name = libbalsa_address_get_first_name(address);
    const gchar *last_name = libbalsa_address_get_last_name(address);

    if (first_name != NULL && first_name[0] != '\0') {
        if (last_name != NULL && last_name[0] != '\0') {
            fprintf(stream, "N:%s;%s\n", last_name, first_name);
        } else
            fprintf(stream, "N:;%s\n", first_name);
    } else if (last_name != NULL && last_name[0] != '\0') {
        fprintf(stream, "N:%s\n", last_name);
    } else
        fprintf(stream, "N:%s\n", _("No-Name"));
}

static void
lbab_vcard_write_nickname(FILE * stream, LibBalsaAddress * address)
{
    const gchar *nick_name = libbalsa_address_get_nick_name(address);

    if (nick_name != NULL && nick_name[0] != '\0')
        fprintf(stream, "NICKNAME:%s\n", nick_name);
}

static void
lbab_vcard_write_org(FILE * stream, LibBalsaAddress * address)
{
    const gchar *organization = libbalsa_address_get_organization(address);

    if (organization != NULL && organization[0] != '\0')
        fprintf(stream, "ORG:%s\n", organization);
}

static void
lbab_vcard_write_addresses(FILE * stream, LibBalsaAddress * address)
{
    guint n_addrs;
    guint n;

    n_addrs = libbalsa_address_get_n_addrs(address);
    for (n = 0; n < n_addrs; ++n) {
        const gchar *addr = libbalsa_address_get_nth_addr(address, n);
        fprintf(stream, "EMAIL;INTERNET:%s\n", addr);
    }
}

static void
lbab_vcard_write_unknown(FILE * stream, const gchar * string)
{
    if (*string)
        fprintf(stream, "%s\n", string);
}

static LibBalsaABErr
lbab_vcard_write_end(FILE * stream)
{
    return fprintf(stream, "END:VCARD\n") > 0 ?
        LBABERR_OK : LBABERR_CANNOT_WRITE;
}


/* Class methods */

/*
 * Parse one address from the input stream; if an output stream is
 * given, create an item on it for the output address.
 */
static LibBalsaABErr
libbalsa_address_book_vcard_parse_address(FILE * stream,
                                          LibBalsaAddress * address,
                                          FILE * stream_out,
                                          LibBalsaAddress * address_out)
{
    gchar string[LINE_LEN];
    gchar *name = NULL, *nick_name = NULL, *org = NULL;
    gchar *full_name = NULL, *last_name = NULL, *first_name = NULL;
    gint in_vcard = FALSE;
    GList *addr_list = NULL;
    guint wrote = 0;

    while (fgets(string, sizeof(string), stream)) {
	/*
	 * Check if it is a card.
	 */
	if (g_ascii_strncasecmp(string, "BEGIN:VCARD", 11) == 0) {
	    in_vcard = TRUE;
            if (stream_out)
                lbab_vcard_write_begin(stream_out);
	    continue;
	}

	if (g_ascii_strncasecmp(string, "END:VCARD", 9) == 0) {
            LibBalsaABErr res = LBABERR_CANNOT_READ;
            /*
             * We are done loading a card.
             */
	    if (addr_list != NULL) {
                if (stream_out != NULL) {
                    if (!(wrote & (1 << FULL_NAME)))
                        lbab_vcard_write_fn(stream_out, address_out);
                    if (!(wrote & (1 << FIRST_NAME)))
                        lbab_vcard_write_n(stream_out, address_out);
                    if (!(wrote & (1 << NICK_NAME)))
                        lbab_vcard_write_nickname(stream_out, address_out);
                    if (!(wrote & (1 << ORGANIZATION)))
                        lbab_vcard_write_org(stream_out, address_out);
                    lbab_vcard_write_addresses(stream_out, address_out);
                    res = lbab_vcard_write_end(stream_out);
                }
                if (address != NULL) {
                    GList *list;

                    if (full_name != NULL)
                        libbalsa_address_set_full_name(address, full_name);
                    else if (name != NULL)
                        libbalsa_address_set_full_name(address, name);
                    else if (nick_name != NULL)
                        libbalsa_address_set_full_name(address, nick_name);
                    else
                        libbalsa_address_set_full_name(address, _("No-Name"));

                    libbalsa_address_set_last_name(address, last_name);
                    libbalsa_address_set_first_name(address, first_name);
                    libbalsa_address_set_nick_name(address, nick_name);
                    libbalsa_address_set_organization(address, org);

                    addr_list = g_list_reverse(addr_list);
                    for (list = addr_list; list != NULL; list = list->next)
                        libbalsa_address_append_addr(address, (const gchar *) list->data);
                    g_list_free_full(addr_list, g_free);

                    res = LBABERR_OK;
                    g_free(full_name);
                    g_free(name);
                    g_free(last_name);
                    g_free(first_name);
                    g_free(nick_name);
                    g_free(org);

                    return res;
                }

                g_list_free_full(addr_list, g_free);
	    }
            /* Record without e-mail address, or we're not creating
             * addresses: free memory. */
            g_free(full_name);
            g_free(name);
            g_free(last_name);
            g_free(first_name);
            g_free(nick_name);
            g_free(org);

            return res;
	}

	if (!in_vcard)
	    continue;

	g_strchomp(string);

	if (g_ascii_strncasecmp(string, "FN:", 3) == 0) {
	    full_name = g_strdup(string + 3);
	    full_name = validate_vcard_string(full_name);
            if (stream_out) {
                lbab_vcard_write_fn(stream_out, address_out);
                wrote |= 1 << FULL_NAME;
            }
	    continue;
	}

	if (g_ascii_strncasecmp(string, "N:", 2) == 0) {
	    name = libbalsa_address_extract_name(string + 2, &last_name,
                                                 &first_name);
	    name = validate_vcard_string(name);
	    last_name = validate_vcard_string(last_name);
	    first_name = validate_vcard_string(first_name);
            if (stream_out) {
                lbab_vcard_write_n(stream_out, address_out);
                wrote |= 1 << FIRST_NAME;
            }
	    continue;
	}

	if (g_ascii_strncasecmp(string, "NICKNAME:", 9) == 0) {
	    nick_name = g_strdup(string + 9);
	    nick_name = validate_vcard_string(nick_name);
            if (stream_out) {
                lbab_vcard_write_nickname(stream_out, address_out);
                wrote |= 1 << NICK_NAME;
            }
	    continue;
	}

	if (g_ascii_strncasecmp(string, "ORG:", 4) == 0) {
	    org = g_strdup(string + 4);
	    org = validate_vcard_string(org);
            if (stream_out) {
                lbab_vcard_write_org(stream_out, address_out);
                wrote |= 1 << ORGANIZATION;
            }
	    continue;
	}

	/*
	 * fetch all e-mail fields
	 */
	if (g_ascii_strncasecmp(string, "EMAIL", 5) == 0) {
	    gchar *ptr = strchr(string + 5, ':');
	    if (ptr != NULL)
		addr_list = g_list_prepend(addr_list, g_strdup(ptr + 1));
	    continue;
	}

        /*
         * unknown line
         */
        if (stream_out)
            lbab_vcard_write_unknown(stream_out, string);
    }

    return LBABERR_CANNOT_READ;
}

/*
 * Write one address to the output stream.
 */
static LibBalsaABErr
libbalsa_address_book_vcard_save_address(FILE * stream,
                                         LibBalsaAddress * address)
{
    lbab_vcard_write_begin(stream);
    lbab_vcard_write_fn(stream, address);
    lbab_vcard_write_n(stream, address);
    lbab_vcard_write_nickname(stream, address);
    lbab_vcard_write_org(stream, address);
    lbab_vcard_write_addresses(stream, address);
    return lbab_vcard_write_end(stream);
}
