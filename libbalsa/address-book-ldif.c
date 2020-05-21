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
 * An LDIF addressbook. See rfc-2849 for format.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address-book-ldif.h"

#include <glib/gi18n.h>

static LibBalsaABErr
libbalsa_address_book_ldif_parse_address(FILE * stream_in,
                                         LibBalsaAddress * address_in,
                                         FILE * stream_out,
                                         LibBalsaAddress * address_out);
static LibBalsaABErr
libbalsa_address_book_ldif_save_address(FILE * stream,
                                        LibBalsaAddress * address);

struct _LibBalsaAddressBookLdif {
    LibBalsaAddressBookText parent;
};

G_DEFINE_TYPE(LibBalsaAddressBookLdif, libbalsa_address_book_ldif,
              LIBBALSA_TYPE_ADDRESS_BOOK_TEXT)

static void
libbalsa_address_book_ldif_init(LibBalsaAddressBookLdif * ab_ldif)
{
}

static void
libbalsa_address_book_ldif_class_init(LibBalsaAddressBookLdifClass * klass)
{
    LibBalsaAddressBookTextClass *address_book_text_class =
        LIBBALSA_ADDRESS_BOOK_TEXT_CLASS(klass);

    address_book_text_class->parse_address =
        libbalsa_address_book_ldif_parse_address;
    address_book_text_class->save_address =
        libbalsa_address_book_ldif_save_address;
}

/* Public method */
LibBalsaAddressBook *
libbalsa_address_book_ldif_new(const gchar * name, const gchar * path)
{
    LibBalsaAddressBookLdif *ab_ldif;

    ab_ldif =
        (LibBalsaAddressBookLdif *)
        g_object_new(LIBBALSA_TYPE_ADDRESS_BOOK_LDIF, NULL);

    libbalsa_address_book_set_name((LibBalsaAddressBook *) ab_ldif, name);
    libbalsa_address_book_text_set_path((LibBalsaAddressBookText *) ab_ldif, path);

    return (LibBalsaAddressBook *) ab_ldif;
}

/* Helpers */

/* according to rfc2849, value_spec must be either 7-bit ASCII
   (safe-string) or a base64-string. Or an url, which is not
   implemented yet.
*/
static gchar*
string_to_value_spec(const gchar* str)
{
    gboolean issafe = TRUE;
    const gchar* p;

    for(p = str; *p != '\0' && issafe; p++)
	issafe = (*p &0x80) == 0;

    if (issafe) {
	return g_strconcat(" ",str, NULL);
    } else {
	gsize len;
	gsize sz;
	gchar *res;
        gsize outlen;
        gint state = 0;
        gint save = 0;

	len = strlen(str);
	sz = (len / 3 + 1) * 4 + 1;
	res = g_malloc(sz + 2);
	strcpy(res, ": ");
        outlen = g_base64_encode_step((const guchar *) str, len, FALSE, res + 2,
                                      &state, &save);
        outlen += g_base64_encode_close(FALSE, res + 2 + outlen, &state, &save);
        res[2 + outlen] = '\0';

	return res;
    }
}

static gchar*
value_spec_to_string(gchar* str)
{
    gchar *res;
    if(str[0] == ':') {
        gsize out_len;

        res = (gchar *) g_base64_decode(g_strchug(str + 1), &out_len);
    } else
	res = g_strdup(g_strstrip(str));
    return res;
}

/* member_value_to_mail:
 * str: dn of a an address book entry.
 * returns: email address associated with the entry.
 * We are cheap and just parse the dn. This may not work generally.
 */
static gchar*
member_value_to_mail(gchar* str)
{
    gchar *res, *unencoded = value_spec_to_string(str);
    gchar *mail = strstr(unencoded, "mail=");
    if(mail) {
        gchar *eos = strchr(mail,',');
        if(eos) *eos = '\0';
        res = g_strdup(mail+5);
    } else res = NULL;
    g_free(unencoded);
    return res;
}

	
static gchar*
read_line(FILE* f)
{
    char buf[256];
    GString* res = g_string_new("");
    int len, emptyp = 1;
    
    while( fgets(buf, sizeof(buf), f)) {
	emptyp = 0;
	g_string_append(res, buf);
	if((len=strlen(buf))> 0 && buf[len-1] == '\n') break;
    }
    g_strchomp(res->str);
    if(res->str[res->len] == '\n') res->str[res->len] = '\0';

    return g_string_free(res, emptyp);
}

/* build_name:
   Builds a full name and returns the pointer to the allocated memory chunk.
   Returns a NULL pointer if it couldn't figure out a name. 
*/
static gchar *
build_name(const gchar *cn, const gchar *givenname, const gchar *surname)
{
    gchar *name = NULL;

    if (cn != NULL && cn[0] != '\0') {
	name = g_strdup(cn);
    } else if(givenname != NULL && givenname[0] != '\0' &&
              surname != NULL && surname[0] != '\0') {
	name = g_strconcat(givenname, " ", surname, NULL);
    } else if (givenname != NULL && givenname[0] != '\0') {
	name = g_strdup(givenname);
    } else if(surname != NULL && surname[0] != '\0') {
	name = g_strdup(surname);
    } else
        name = g_strdup(_("No-Name"));

    return name;
}

/* address_new_prefill:
   takes over the string ownership!
*/
static void
address_new_prefill(LibBalsaAddress * address, GList * address_list,
                    gchar * nickn, gchar * givenn, gchar * surn,
                    gchar * fulln, gchar * org)
{
    GList *list;
    gchar *full_name;

    for (list = address_list; list != NULL; list = list->next)
        libbalsa_address_append_addr(address, (const gchar *) list->data);
    g_list_free_full(address_list, g_free);

    if (givenn != NULL) {
        libbalsa_address_set_first_name(address, givenn);
        g_free(givenn);
    } else {
        libbalsa_address_set_first_name(address, nickn != NULL ? nickn : "");
    }

    if (surn != NULL) {
        libbalsa_address_set_last_name(address, surn);
        g_free(surn);
    } else {
        libbalsa_address_set_last_name(address, "");
    }

    full_name = build_name(fulln, libbalsa_address_get_first_name(address), surn);
    if (full_name != NULL) {
        libbalsa_address_set_full_name(address, full_name);
    } else {
        libbalsa_address_set_full_name(address, _("No-Name"));
    }
    g_free(fulln);

    if (org != NULL) {
        libbalsa_address_set_organization(address, org);
        g_free(org);
    } else {
        libbalsa_address_set_organization(address, "");
    }

    if (nickn != NULL) {
        libbalsa_address_set_nick_name(address, nickn);
        g_free(nickn);
    } else {
        libbalsa_address_set_nick_name(address,
                                       full_name != NULL ? full_name : _("No-Id"));
    }

    g_free(full_name);
}

/* Class methods */

/* 
 * Write various lines to the output stream.
 */
static void
lbab_ldif_write_dn(FILE * stream, LibBalsaAddress * address)
{
    const gchar *full_name = libbalsa_address_get_full_name(address);
    gchar *cn = NULL;
    const gchar *addr;
    gchar *value, *value_spec;

    if (full_name != NULL && full_name[0] != '\0') {
        cn = g_strdup(full_name);
    } else {
        const gchar *first_name = libbalsa_address_get_first_name(address);
        const gchar *last_name  = libbalsa_address_get_last_name(address);

        cn = build_name(NULL, first_name, last_name);
        if (cn == NULL) {
            cn = g_strdup(_("No-Name"));
        } else {
            if (cn[0] == '\0') {
                g_free(cn);
                cn = g_strdup(_("No-Name"));
            }
        }
    }

    addr = libbalsa_address_get_addr(address);
    if (addr != NULL && addr[0] != '\0') {
        value = g_strdup_printf("cn=%s,mail=%s", cn, addr);
    } else {
        value = g_strdup_printf("cn=%s", cn);
    }
    value_spec = string_to_value_spec(value);
    fprintf(stream, "dn:%s\n", value_spec);
    g_free(value_spec);
    g_free(value);

    value_spec = string_to_value_spec(cn);
    fprintf(stream, "cn:%s\n", value_spec);
    g_free(value_spec);
    g_free(cn);
}

static void
lbab_ldif_write_addresses(FILE * stream, LibBalsaAddress * address)
{
    guint n_addrs, n;

    n_addrs = libbalsa_address_get_n_addrs(address);
    for (n = 0; n < n_addrs; ++n) {
        const gchar *mail = libbalsa_address_get_nth_addr(address, n);

        if (mail != NULL && mail[0] != '\0') {
            gchar *value_spec = string_to_value_spec(mail);
            fprintf(stream, "mail:%s\n", value_spec);
            g_free(value_spec);
        }
    }
}

static void
lbab_ldif_write_surname(FILE * stream, LibBalsaAddress * address)
{
    const gchar *last_name = libbalsa_address_get_last_name(address);

    if (last_name != NULL && last_name[0] != '\0') {
        gchar *value_spec = string_to_value_spec(last_name);
        fprintf(stream, "sn:%s\n", value_spec);
        g_free(value_spec);
    }
}

static void
lbab_ldif_write_givenname(FILE * stream, LibBalsaAddress * address)
{
    const gchar *first_name = libbalsa_address_get_first_name(address);

    if (first_name != NULL && first_name[0] != '\0') {
        gchar *value_spec = string_to_value_spec(first_name);
        fprintf(stream, "givenname:%s\n", value_spec);
        g_free(value_spec);
    }
}

static void
lbab_ldif_write_nickname(FILE * stream, LibBalsaAddress * address)
{
    const gchar *nick_name = libbalsa_address_get_nick_name(address);

    if (nick_name != NULL) {
        gchar *value_spec = string_to_value_spec(nick_name);
        fprintf(stream, "xmozillanickname:%s\n", value_spec);
        g_free(value_spec);
    }
}

static void
lbab_ldif_write_organization(FILE * stream, LibBalsaAddress * address)
{
    const gchar *organization = libbalsa_address_get_nick_name(address);

    if (organization != NULL) {
        gchar *value_spec = string_to_value_spec(organization);
        fprintf(stream, "o:%s\n", value_spec);
        g_free(value_spec);
    }
}

/*
 * Parse one address from the input stream; if an output stream is
 * given, create an item on it for the output address.
 */
static LibBalsaABErr
libbalsa_address_book_ldif_parse_address(FILE * stream,
                                         LibBalsaAddress * address,
                                         FILE * stream_out,
                                         LibBalsaAddress * address_out)
{
    gchar *line;
    gchar *surname = NULL, *givenname = NULL, *nickname = NULL,
	*fullname = NULL, *organization = NULL;
    gint in_ldif = FALSE;
    GList *address_list = NULL;
    guint wrote = 0;

    for (; (line=read_line(stream)) != NULL || in_ldif; g_free(line) ) {
        if (line) {
            /*
             * Check if it is a card.
             */
            if (g_ascii_strncasecmp(line, "dn:", 3) == 0) {
                in_ldif = TRUE;
                if (stream_out)
                    lbab_ldif_write_dn(stream_out, address_out);
                continue;
            }

            if (!in_ldif) {
                if (stream_out && *line)
                    fprintf(stream_out, "%s\n", line);
                continue;
            }

            g_strchomp(line);
        }

	if (!line || line[0] == '\0') {
            LibBalsaABErr res = LBABERR_CANNOT_READ;
            /*
             * We are done loading a card.
             */
            if (address_list) {
                if (stream_out) {
                    if (!(wrote & (1 << LAST_NAME)))
                        lbab_ldif_write_surname(stream_out, address_out);
                    if (!(wrote & (1 << FIRST_NAME)))
                        lbab_ldif_write_givenname(stream_out, address_out);
                    if (!(wrote & (1 << NICK_NAME)))
                        lbab_ldif_write_nickname(stream_out, address_out);
                    if (!(wrote & (1 << ORGANIZATION)))
                        lbab_ldif_write_organization(stream_out, address_out);
                    lbab_ldif_write_addresses(stream_out, address_out);
                    res = fprintf(stream_out, "\n") < 0 ?
                        LBABERR_CANNOT_WRITE : LBABERR_OK;
                }

	        if (address) {
                    address_new_prefill(address,
                                        g_list_reverse(address_list),
                                        nickname, givenname, surname,
                                        fullname, organization);
                    g_free(line);
                    return LBABERR_OK;
                }
                g_list_free_full(address_list, g_free);
	    }
            /* Record without e-mail address, or we're not creating
             * addresses: free memory. */
            g_free(fullname);
            g_free(nickname);
            g_free(givenname);
            g_free(surname);
            g_free(organization);
            g_free(line);
            return res;
	}

	if (g_ascii_strncasecmp(line, "cn:", 3) == 0) {
	    fullname = value_spec_to_string(g_strchug(line + 3));
	    continue;
	}

	if (g_ascii_strncasecmp(line, "sn:", 3) == 0) {
	    surname = value_spec_to_string(g_strchug(line + 3));
            if (stream_out) {
                lbab_ldif_write_surname(stream_out, address_out);
                wrote |= 1 << LAST_NAME;
            }
	    continue;
	}

	if (g_ascii_strncasecmp(line, "givenname:", 10) == 0) {
	    givenname = value_spec_to_string(g_strchug(line + 10));
            if (stream_out) {
                lbab_ldif_write_givenname(stream_out, address_out);
                wrote |= 1 << FIRST_NAME;
            }
	    continue;
	}

	if (g_ascii_strncasecmp(line, "xmozillanickname:", 17) == 0) {
	    nickname = value_spec_to_string(g_strchug(line + 17));
            if (stream_out) {
                lbab_ldif_write_nickname(stream_out, address_out);
                wrote |= 1 << NICK_NAME;
            }
	    continue;
	}

	if (g_ascii_strncasecmp(line, "o:", 2) == 0) {
	    organization = value_spec_to_string(g_strchug(line + 2));
            if (stream_out) {
                lbab_ldif_write_organization(stream_out, address_out);
                wrote |= 1 << ORGANIZATION;
            }
	    continue;
	}

	if (g_ascii_strncasecmp(line, "member:", 7) == 0) {
            address_list =
                g_list_prepend(address_list,
                               member_value_to_mail(g_strchug(line + 7)));
	    continue;
	}

	/*
	 * fetch all e-mail fields
	 */
	if (g_ascii_strncasecmp(line, "mail:", 5) == 0) {
	    address_list = 
		g_list_prepend(address_list, 
			       value_spec_to_string(g_strchug(line + 5)));
	    continue;
	}

        /* 
         * unknown line
         */
        if (stream_out && *line)
            fprintf(stream_out, "%s\n", line);
    }

    return LBABERR_CANNOT_READ;
}

/*
 * Write one address to the output stream.
 */
static LibBalsaABErr
libbalsa_address_book_ldif_save_address(FILE * stream,
                                        LibBalsaAddress * address)
{
    if (fseek(stream, -2, SEEK_END) == 0
        && (fgetc(stream) != '\n' || fgetc(stream) != '\n')) {
        fseek(stream, 0, SEEK_END);
        fputc('\n', stream);
    }

    lbab_ldif_write_dn(stream, address);
    lbab_ldif_write_givenname(stream, address);
    lbab_ldif_write_surname(stream, address);
    lbab_ldif_write_nickname(stream, address);
    lbab_ldif_write_organization(stream, address);
    lbab_ldif_write_addresses(stream, address);
    return fprintf(stream, "\n") < 0 ? LBABERR_CANNOT_WRITE : LBABERR_OK;
}
