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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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

#include <string.h>
#include <glib/gi18n.h>

/* FIXME: Perhaps the whole thing could be rewritten to use a g_scanner ?? */

/* FIXME: Arbitrary constant */
#define LINE_LEN 256

static void
libbalsa_address_book_vcard_class_init(LibBalsaAddressBookVcardClass *
                                       klass);

static LibBalsaABErr
libbalsa_address_book_vcard_parse_address(FILE * stream_in,
                                          LibBalsaAddress * address_in,
                                          FILE * stream_out,
                                          LibBalsaAddress * address_out);
static LibBalsaABErr
libbalsa_address_book_vcard_save_address(FILE * stream,
                                         LibBalsaAddress * address);


GType libbalsa_address_book_vcard_get_type(void)
{
    static GType address_book_vcard_type = 0;

    if (!address_book_vcard_type) {
	static const GTypeInfo address_book_vcard_info = {
	    sizeof(LibBalsaAddressBookVcardClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_book_vcard_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddressBookVcard),
            0,                  /* n_preallocs */
	    NULL                /* instance_init */
	};

	address_book_vcard_type =
            g_type_register_static(LIBBALSA_TYPE_ADDRESS_BOOK_TEXT,
	                           "LibBalsaAddressBookVcard",
			           &address_book_vcard_info, 0);
    }

    return address_book_vcard_type;
}

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

/* Public method */
LibBalsaAddressBook *
libbalsa_address_book_vcard_new(const gchar * name, const gchar * path)
{
    LibBalsaAddressBookVcard *abvc;
    LibBalsaAddressBook *ab;

    abvc =
        LIBBALSA_ADDRESS_BOOK_VCARD(g_object_new
                                    (LIBBALSA_TYPE_ADDRESS_BOOK_VCARD,
                                     NULL));
    ab = LIBBALSA_ADDRESS_BOOK(abvc);

    libbalsa_address_book_set_name(ab, name);
    LIBBALSA_ADDRESS_BOOK_TEXT(ab)->path = g_strdup(path);

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
    if (address->full_name && *address->full_name != '\0')
        fprintf(stream, "FN:%s\n", address->full_name);
}

static void
lbab_vcard_write_n(FILE * stream, LibBalsaAddress * address)
{
    if (address->first_name && *address->first_name != '\0') {
        if (address->last_name && *address->last_name != '\0') {
            fprintf(stream, "N:%s;%s\n", address->last_name,
                    address->first_name);
        } else
            fprintf(stream, "N:;%s\n", address->first_name);
    } else if (address->last_name && *address->last_name != '\0') {
        fprintf(stream, "N:%s\n", address->last_name);
    } else
        fprintf(stream, "N:%s\n", _("No-Name"));
}

static void
lbab_vcard_write_nickname(FILE * stream, LibBalsaAddress * address)
{
    if (address->nick_name && *address->nick_name != '\0')
        fprintf(stream, "NICKNAME:%s\n", address->nick_name);
}

static void
lbab_vcard_write_org(FILE * stream, LibBalsaAddress * address)
{
    if (address->organization && *address->organization != '\0')
        fprintf(stream, "ORG:%s\n", address->organization);
}

static void
lbab_vcard_write_addresses(FILE * stream, LibBalsaAddress * address)
{
    GList *list;

    for (list = address->address_list; list; list = list->next)
        fprintf(stream, "EMAIL;INTERNET:%s\n", (gchar *) list->data);
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
    GList *address_list = NULL;
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
	    if (address_list) {
                if (stream_out) {
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
                if (address) {
                    if (full_name) {
                        address->full_name = full_name;
                        g_free(name);
                    } else if (name)
                        address->full_name = name;
                    else if (nick_name)
                        address->full_name = g_strdup(nick_name);
                    else
                        address->full_name = g_strdup(_("No-Name"));

                    address->last_name = last_name;
                    address->first_name = first_name;
                    address->nick_name = nick_name;
                    address->organization = org;
                    address->address_list = g_list_reverse(address_list);

                    return LBABERR_OK;
                }
                g_list_free_full(address_list, g_free);
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
	    if (ptr) {
		address_list =
		    g_list_prepend(address_list, g_strdup(ptr + 1));
	    }
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
