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
 * An LDIF addressbook. See rfc-2849 for format.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address-book-ldif.h"

#include <ctype.h>
#include <string.h>

#include <glib/gi18n.h>

static void
libbalsa_address_book_ldif_class_init(LibBalsaAddressBookLdifClass *
                                      klass);

static LibBalsaABErr
libbalsa_address_book_ldif_parse_address(FILE * stream_in,
                                         LibBalsaAddress * address_in,
                                         FILE * stream_out,
                                         LibBalsaAddress * address_out);
static LibBalsaABErr
libbalsa_address_book_ldif_save_address(FILE * stream,
                                        LibBalsaAddress * address);


GType libbalsa_address_book_ldif_get_type(void)
{
    static GType address_book_ldif_type = 0;

    if (!address_book_ldif_type) {
	static const GTypeInfo address_book_ldif_info = {
	    sizeof(LibBalsaAddressBookLdifClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_book_ldif_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddressBookLdif),
            0,                  /* n_preallocs */
	    NULL
	};

	address_book_ldif_type =
            g_type_register_static(LIBBALSA_TYPE_ADDRESS_BOOK_TEXT,
	                           "LibBalsaAddressBookLdif",
			           &address_book_ldif_info, 0);
    }

    return address_book_ldif_type;
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
    LibBalsaAddressBook *ab;

    ab_ldif =
        LIBBALSA_ADDRESS_BOOK_LDIF(g_object_new
                                   (LIBBALSA_TYPE_ADDRESS_BOOK_LDIF,
                                    NULL));
    ab = LIBBALSA_ADDRESS_BOOK(ab_ldif);

    libbalsa_address_book_set_name(ab, name);
    LIBBALSA_ADDRESS_BOOK_TEXT(ab)->path = g_strdup(path);

    return ab;
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
build_name(gchar *cn, gchar *givenname, gchar *surname)
{
    gchar *name = NULL;

    if(cn && *cn) {
	name = g_strdup(cn);
    } else if(givenname && *givenname && surname && *surname) {
	name = g_strconcat (givenname," ",surname,NULL);
    } else if(givenname && *givenname) {
	name = g_strdup(givenname);
    } else if(surname && *surname) {
	name = g_strdup(surname);
    } else name = g_strdup(_("No-Name")); 
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
    address->address_list = address_list;
    
    address->first_name = givenn ? givenn : g_strdup(nickn ? nickn : "");
    address->last_name = surn ? surn : g_strdup("");
    address->full_name = build_name(fulln, address->first_name, surn);
    g_free(fulln);
    address->organization = org ? org : g_strdup("");
    
    address->nick_name = nickn ? nickn : 
	g_strdup(address->full_name ? address->full_name : _("No-Id"));
    
    if (address->full_name == NULL)
	address->full_name = g_strdup(_("No-Name"));
}

/* Class methods */

/* 
 * Write various lines to the output stream.
 */
static void
lbab_ldif_write_dn(FILE * stream, LibBalsaAddress * address)
{
    gchar *cn = NULL;
    gchar *value, *value_spec;

    if (address->full_name != NULL && address->full_name[0] != '\0') {
        cn = g_strdup(address->full_name);
    } else {
        cn = build_name(NULL, address->first_name, address->last_name);
        if (cn == NULL) {
            cn = g_strdup(_("No-Name"));
        } else {
            if (cn[0] == '\0') {
                g_free(cn);
                cn = g_strdup(_("No-Name"));
            }
        }
    }

    if (address->address_list && address->address_list->data) {
        value =
            g_strdup_printf("cn=%s,mail=%s",
                            cn, (gchar *) address->address_list->data);
    } else
        value = g_strdup_printf("cn=%s", cn);
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
    GList *list;

    for (list = address->address_list; list; list = list->next) {
        const gchar *mail = list->data;
        if (mail && *mail) {
            gchar *value_spec = string_to_value_spec(mail);
            fprintf(stream, "mail:%s\n", value_spec);
            g_free(value_spec);
        }
    }
}

static void
lbab_ldif_write_surname(FILE * stream, LibBalsaAddress * address)
{
    if (address->last_name && *(address->last_name)) {
        gchar *value_spec = string_to_value_spec(address->last_name);
        fprintf(stream, "sn:%s\n", value_spec);
        g_free(value_spec);
    }
}

static void
lbab_ldif_write_givenname(FILE * stream, LibBalsaAddress * address)
{
    if (address->first_name && *(address->first_name)) {
        gchar *value_spec = string_to_value_spec(address->first_name);
        fprintf(stream, "givenname:%s\n", value_spec);
        g_free(value_spec);
    }
}

static void
lbab_ldif_write_nickname(FILE * stream, LibBalsaAddress * address)
{
    if (address->nick_name) {
        gchar *value_spec = string_to_value_spec(address->nick_name);
        fprintf(stream, "xmozillanickname:%s\n", value_spec);
        g_free(value_spec);
    }
}

static void
lbab_ldif_write_organization(FILE * stream, LibBalsaAddress * address)
{
    if (address->organization) {
        gchar *value_spec = string_to_value_spec(address->organization);
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
