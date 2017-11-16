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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address.h"

#include <string.h>
#include <gmime/gmime.h>

#include "misc.h"
#include <glib/gi18n.h>

static GObjectClass *parent_class;

static void libbalsa_address_class_init(LibBalsaAddressClass * klass);
static void libbalsa_address_init(LibBalsaAddress * ab);
static void libbalsa_address_finalize(GObject * object);

static gchar ** vcard_strsplit(const gchar * string);

GType libbalsa_address_get_type(void)
{
    static GType address_type = 0;

    if (!address_type) {
	static const GTypeInfo address_info = {
	    sizeof(LibBalsaAddressClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddress),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_address_init
	};

	address_type =
	    g_type_register_static(G_TYPE_OBJECT,
	                           "LibBalsaAddress",
                                   &address_info, 0);
    }

    return address_type;
}

static void
libbalsa_address_class_init(LibBalsaAddressClass * klass)
{
    GObjectClass *object_class;

    parent_class = g_type_class_peek_parent(klass);

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = libbalsa_address_finalize;
}

static void
libbalsa_address_init(LibBalsaAddress * addr)
{
    addr->nick_name = NULL;
    addr->full_name = NULL;
    addr->first_name = NULL;
    addr->last_name = NULL;
    addr->organization = NULL;
    addr->address_list = NULL;
}

static void
libbalsa_address_finalize(GObject * object)
{
    LibBalsaAddress *addr;

    g_return_if_fail(object != NULL);

    addr = LIBBALSA_ADDRESS(object);

    g_free(addr->nick_name);    addr->nick_name = NULL;
    g_free(addr->full_name);    addr->full_name = NULL;
    g_free(addr->first_name);   addr->first_name = NULL;
    g_free(addr->last_name);    addr->last_name = NULL;
    g_free(addr->organization); addr->organization = NULL;

    g_list_foreach(addr->address_list, (GFunc) g_free, NULL);
    g_list_free(addr->address_list);
    addr->address_list = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddress *
libbalsa_address_new(void)
{
    return g_object_new(LIBBALSA_TYPE_ADDRESS, NULL);
}

/** Extract full name in order from <string> that has GnomeCard format
   and returns the pointer to the allocated memory chunk.

   VCARD code attempts to obey published documentation:

   [1] VCARD 1.2 specs: http://www.imc.org/pdi/vcard-21.txt
   [2] VCARD 3.0 specs, RFC 2426 (http://www.ietf.org/rfc/rfc2426.txt)
*/
gchar*
libbalsa_address_extract_name(const gchar * string, gchar ** last_name,
                              gchar ** first_name)
{
    enum GCardFieldOrder { LAST = 0, FIRST, MIDDLE, PREFIX, SUFFIX };
    gint cpt, j;
    gchar **fld, **name_arr;
    gchar *res = NULL;

    fld = vcard_strsplit(string);

    cpt = 0;
    while (fld[cpt] != NULL)
	cpt++;

    if (cpt == 0) {
        /* insane empty name */
        g_strfreev(fld);
        return NULL;
    }

    if (fld[LAST] && *fld[LAST])
        *last_name = g_strdup(fld[LAST]);

    if (fld[FIRST] && *fld[FIRST])
        *first_name = fld[MIDDLE] && *fld[MIDDLE] ?
            g_strconcat(fld[FIRST], " ", fld[MIDDLE], NULL) :
            g_strdup(fld[FIRST]);

    name_arr = g_malloc((cpt + 1) * sizeof(gchar *));

    j = 0;
    if (cpt > PREFIX && *fld[PREFIX] != '\0')
	name_arr[j++] = g_strdup(fld[PREFIX]);

    if (cpt > FIRST && *fld[FIRST] != '\0')
	name_arr[j++] = g_strdup(fld[FIRST]);

    if (cpt > MIDDLE && *fld[MIDDLE] != '\0')
	name_arr[j++] = g_strdup(fld[MIDDLE]);

    if (cpt > LAST && *fld[LAST] != '\0')
	name_arr[j++] = g_strdup(fld[LAST]);

    if (cpt > SUFFIX && *fld[SUFFIX] != '\0')
	name_arr[j++] = g_strdup(fld[SUFFIX]);

    name_arr[j] = NULL;

    g_strfreev(fld);

    /* collect the data to one string */
    res = g_strjoinv(" ", name_arr);
    g_strfreev(name_arr);

    return res;
}

/* Duplicates some code from address-book-vcard.c:
 */
static gchar*
validate_vcard_string(gchar * vcstr, const gchar * charset)
{
    gchar * utf8res;
    gsize b_written;

    /* check if it's a utf8 clean string and return it in this case */
    if (!vcstr || g_utf8_validate(vcstr, -1, NULL))
	return vcstr;

    /* convert from the passed charset or as fallback from the locale setting */
    if (charset && g_ascii_strcasecmp(charset, "utf-8")) {
	utf8res = g_convert(vcstr, -1, "utf-8", charset, NULL, &b_written, NULL);
    } else
	utf8res = g_locale_to_utf8(vcstr, -1, NULL, &b_written, NULL);
    if (!utf8res)
	return vcstr;

    g_free(vcstr);
    return utf8res;
}


static inline gchar *
vcard_qp_decode(gchar * str)
{
    gint len = strlen(str);
    gchar * newstr = g_malloc0(len + 1);
    int state = 0;
    guint32 save;

    /* qp decode the input string */
    g_mime_encoding_quoted_decode_step((unsigned char *) str, len,
				    (unsigned char *) newstr, &state, &save);

    /* free input and return new string */
    g_free(str);
    return newstr;
}


static inline gchar *
vcard_b64_decode(gchar * str)
{
    gint len = strlen(str);
    gchar * newstr = g_malloc0(len + 1);
    int state = 0;
    guint32 save;

    /* base64 decode the input string */
    g_mime_encoding_base64_decode_step((unsigned char *) str, len,
				    (unsigned char *) newstr, &state, &save);

    /* free input and return new string */
    g_free(str);
    return newstr;
}


static inline gchar *
vcard_charset_to_utf8(gchar * str, const gchar * charset)
{
    gsize bytes_written;
    gchar * convstr;

    /* convert only if the source is known and not utf-8 */
    if (!charset || !g_ascii_strcasecmp(charset, "utf-8"))
	return str;

    convstr = g_convert(str, -1, "utf-8", charset, NULL, &bytes_written, NULL);
    g_free(str);
    return convstr ? convstr : g_strdup("");
}


/* mainly copied from g_strsplit, but (a) with the fixed delimiter ';'
 * (b) ignoring '\;' sequences (c) always returning as many elements as
 * possible and (d) unescape '\;' sequences in the resulting array */
static gchar **
vcard_strsplit(const gchar * string)
{
    GSList *string_list = NULL, *slist;
    gchar **str_array, *s;
    guint n = 0;
    const gchar *remainder;

    g_return_val_if_fail(string != NULL, NULL);

    remainder = string;
    s = strchr(remainder, ';');
    while (s && s > remainder && s[-1] == '\\')
	s = strchr(s + 1, ';');

    while (s) {
	gsize len;

	len = s - remainder;
        /* skip empty fields: */
        if (len > 0) {
            string_list =
                g_slist_prepend(string_list, g_strndup(remainder, len));
            n++;
        }
	remainder = s + 1;
	s = strchr(remainder, ';');
	while (s && s > remainder && s[-1] == '\\')
	    s = strchr(s + 1, ';');
    }

    if (*string) {
	n++;
	string_list = g_slist_prepend(string_list, g_strdup(remainder));
    }

    str_array = g_new(gchar*, n + 1);

    str_array[n--] = NULL;
    for (slist = string_list; slist; slist = slist->next) {
	gchar * str = (gchar *) slist->data;
	gchar * p;

	while ((p = strstr(str, "\\;"))) {
	    gchar * newstr = g_malloc(strlen(str));

	    strncpy(newstr, str, p - str);
	    strcpy(newstr + (p - str), p + 1);
	    g_free(str);
	    str = newstr;
	}
	str_array[n--] = str;
    }

    g_slist_free(string_list);

    return str_array;
}

/*
 * Create a LibBalsaAddress from a vCard; return NULL if the string does
 * not contain a complete vCard with at least one email address.
 */

LibBalsaAddress*
libbalsa_address_new_from_vcard(const gchar *str, const gchar *charset)
{
    gchar *name = NULL, *nick_name = NULL, *org = NULL;
    gchar *full_name = NULL, *last_name = NULL, *first_name = NULL;
    gint in_vcard = FALSE;
    GList *address_list = NULL;
    const gchar *string, *next_line;
    gchar * vcard;

    g_return_val_if_fail(str, NULL);

    /* rfc 2425 unfold the string */
    vcard = g_strdup(str);
    while ((string = strstr(vcard, "\r\n ")) ||
	   (string = strstr(vcard, "\r\n\t"))) {
	gchar * newstr = g_malloc0(strlen(vcard) - 2);

	strncpy(newstr, vcard, string - vcard);
	strcpy(newstr + (string - vcard), string + 3);
	g_free(vcard);
	vcard = newstr;
    }
    while ((string = strstr(vcard, "\n ")) ||
	   (string = strstr(vcard, "\n\t"))) {
	gchar * newstr = g_malloc(strlen(vcard) - 1);

	strncpy(newstr, vcard, string - vcard);
	strcpy(newstr + (string - vcard), string + 2);
	g_free(vcard);
	vcard = newstr;
    }

    /* may contain \r's when decoded from base64... */
    while ((string = strstr(vcard, "\r\n ")) ||
	   (string = strstr(vcard, "\r\n\t"))) {
	gchar * newstr = g_malloc(strlen(vcard) - 2);

	strncpy(newstr, vcard, string - vcard);
	strcpy(newstr + (string - vcard), string + 3);
	g_free(vcard);
	vcard = newstr;
    }

    /* process */
    for(string = vcard; *string; string = next_line) {
        gchar *line;

        next_line = strchr(string, '\n');
        if (next_line)
            ++next_line;
        else
            next_line = string + strlen(string);
	/*
	 * Check if it is a card.
	 */
	if (g_ascii_strncasecmp(string, "BEGIN:VCARD", 11) == 0)
	    in_vcard = TRUE;
	else if (in_vcard) {
	    if (g_ascii_strncasecmp(string, "END:VCARD", 9) == 0) {
		/*
		 * We are done loading a card.
		 */
		LibBalsaAddress *address;

		if (!address_list)
                    break;

		address = g_object_new(LIBBALSA_TYPE_ADDRESS, NULL);

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

                return address;
            }

            line = g_strndup(string, next_line - string);
            g_strchomp(line);

	    /* Encoding of national characters:
	     * - vcard 2.1 allows charset=xxx and encoding=(base64|quoted-printable|8bit)
	     * - Thunderbird claims to use vcard 2.1, but uses only "quoted-printable",
	     *   and the charset from part MIME header
	     * - vcard 3.0 (rfc 2426) allows "encoding=b", the charset must be taken
	     *   from the content-type MIME header */
	    if (strchr(line, ':')) {
		gchar ** parts = g_strsplit(line, ":", 2);
		gchar ** tokens;
		gint n;

		/* split control stuff into tokens */
		tokens = g_strsplit(parts[0], ";", -1);

		/* find encoding= */
		for (n = 0; tokens[n]; n++)
		    if (!g_ascii_strncasecmp(tokens[n], "encoding=", 9)) {
			if (!g_ascii_strcasecmp(tokens[n] + 9, "base64")) {
			    /* vcard 2.1: use the charset parameter (below) */
			    parts[1] = vcard_b64_decode(parts[1]);
			} else if (!g_ascii_strcasecmp(tokens[n] + 9, "b")) {
			    /* rfc 2426: charset from MIME part */
			    parts[1] = vcard_b64_decode(parts[1]);
			    parts[1] = vcard_charset_to_utf8(parts[1], charset);
			} else if (!g_ascii_strcasecmp(tokens[n] + 9, "quoted-printable")) {
			    /* vcard 2.1: use the charset parameter (below) */
			    parts[1] = vcard_qp_decode(parts[1]);
			}
		    }

		/* find quoted-printable */
		for (n = 0; tokens[n]; n++)
		    if (!g_ascii_strcasecmp(tokens[n], "quoted-printable")) {
			/* Thunderbird: broken vcard 2.1, charset from MIME part */
			parts[1] = vcard_qp_decode(parts[1]);
			parts[1] = vcard_charset_to_utf8(parts[1], charset);
		    }

		/* find charset= (vcard 2.1 only) */
		for (n = 0; tokens[n]; n++)
		    if (!g_ascii_strncasecmp(tokens[n], "charset=", 8))
			parts[1] = vcard_charset_to_utf8(parts[1], tokens[n] + 8);

		/* construct the result */
		g_free(line);
		line = g_strdup_printf("%s:%s", tokens[0], parts[1]);

		/* clean up */
		g_strfreev(tokens);
		g_strfreev(parts);
	    }

            if (g_ascii_strncasecmp(line, "FN:", 3) == 0) {

                g_free(full_name);
                full_name = g_strdup(line + 3);
                full_name = validate_vcard_string(full_name, charset);

            } else if (g_ascii_strncasecmp(line, "N:", 2) == 0) {

                g_free(name);
                g_free(last_name);
                g_free(first_name);
                name = libbalsa_address_extract_name(line + 2,
                                                     &last_name, &first_name);
                name = validate_vcard_string(name, charset);
                last_name = validate_vcard_string(last_name, charset);
                first_name = validate_vcard_string(first_name, charset);

            } else if (g_ascii_strncasecmp(line, "NICKNAME:", 9) == 0) {

                g_free(nick_name);
                nick_name = g_strdup(line + 9);
                nick_name = validate_vcard_string(nick_name, charset);

            } else if (g_ascii_strncasecmp(line, "ORG:", 4) == 0) {
		gchar ** org_strs = vcard_strsplit(line + 4);

                g_free(org);
                org = g_strjoinv(", ", org_strs);
		g_strfreev(org_strs);
                org = validate_vcard_string(org, charset);

            } else if (g_ascii_strncasecmp(line, "EMAIL:", 6) == 0) {

		address_list =
		    g_list_prepend(address_list, g_strdup(line + 6));
            }
            g_free(line);
        }
    }

    g_free(full_name);
    g_free(name);
    g_free(last_name);
    g_free(first_name);
    g_free(nick_name);
    g_free(org);
    g_list_foreach(address_list, (GFunc) g_free, NULL);
    g_list_free(address_list);

    return NULL;
}

void
libbalsa_address_set_copy(LibBalsaAddress * dest, LibBalsaAddress * src)
{
    GList *src_al, *dst_al;

    if (dest == src)            /* safety check */
        return;

    g_free(dest->nick_name);
    dest->nick_name = g_strdup(src->nick_name);
    g_free(dest->full_name);
    dest->full_name = g_strdup(src->full_name);
    g_free(dest->first_name);
    dest->first_name = g_strdup(src->first_name);
    g_free(dest->last_name);
    dest->last_name = g_strdup(src->last_name);
    g_free(dest->organization);
    dest->organization = g_strdup(src->organization);
    g_list_foreach(dest->address_list, (GFunc) g_free, NULL);
    g_list_free(dest->address_list);

    dst_al = NULL;
    for (src_al = src->address_list; src_al; src_al = src_al->next)
        dst_al = g_list_prepend(dst_al, g_strdup(src_al->data));
    dest->address_list = g_list_reverse(dst_al);
}

static gchar *
rfc2822_mailbox(const gchar * full_name, const gchar * address)
{
    InternetAddress *ia;
    gchar *new_str;

    ia = internet_address_mailbox_new(full_name, address);
    new_str = internet_address_to_string(ia, FALSE);
    g_object_unref(ia);

    return new_str;
}

static gchar*
rfc2822_group(const gchar *full_name, GList *addr_list)
{
    InternetAddress *ia;
    gchar *res;

    ia = internet_address_group_new(full_name);
    for (; addr_list; addr_list = addr_list->next) {
	InternetAddress *member;

	member = internet_address_mailbox_new(NULL, addr_list->data);
	internet_address_group_add_member(INTERNET_ADDRESS_GROUP(ia), member);
	g_object_unref(member);
    }
    res = internet_address_to_string(ia, FALSE);
    g_object_unref(ia);

    return res;
}

/*
   Get a string version of this address.

   If n == -1 then return all addresses, else return the n'th one.
   If n > the number of addresses, will cause an error.
*/
gchar *
libbalsa_address_to_gchar(LibBalsaAddress * address, gint n)
{
    gchar *retc = NULL;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    if(!address->address_list)
        return NULL;
    if(n==-1) {
        if(address->address_list->next)
            retc = rfc2822_group(address->full_name, address->address_list);
        else
            retc = rfc2822_mailbox(address->full_name,
                                   address->address_list->data);
    } else {
	const gchar *mailbox = g_list_nth_data(address->address_list, n);
	g_return_val_if_fail(mailbox != NULL, NULL);

	retc = rfc2822_mailbox(address->full_name, mailbox);
    }

    return retc;
}

/* Helper */
static const gchar *
lba_get_name_or_mailbox(InternetAddressList * address_list,
                        gboolean get_name, gboolean in_group)
{
    const gchar *retval = NULL;
    InternetAddress *ia;
    gint i, len;

    if (address_list == NULL)
	return NULL;

    len = internet_address_list_length(address_list);
    for (i = 0; i < len; i++) {
        ia = internet_address_list_get_address (address_list, i);

        if (get_name && ia->name && *ia->name)
            return ia->name;

        if (INTERNET_ADDRESS_IS_MAILBOX (ia))
            retval = INTERNET_ADDRESS_MAILBOX (ia)->addr;
        else {
            if (in_group)
                g_message("Ignoring nested group address");
            else
                retval = lba_get_name_or_mailbox(INTERNET_ADDRESS_GROUP(ia)->members,
			get_name, TRUE);
        }
        if (retval)
            break;
    }

    return retval;
}

/* Get either a name or a mailbox from an InternetAddressList. */
const gchar *
libbalsa_address_get_name_from_list(InternetAddressList *address_list)
{
    return lba_get_name_or_mailbox(address_list, TRUE, FALSE);
}

/* Get a mailbox from an InternetAddressList. */
const gchar *
libbalsa_address_get_mailbox_from_list(InternetAddressList *address_list)
{
    return lba_get_name_or_mailbox(address_list, FALSE, FALSE);
}

/* Number of individual mailboxes in an InternetAddressList. */
gint
libbalsa_address_n_mailboxes_in_list(InternetAddressList * address_list)
{
    gint i, len, n_mailboxes = 0;

    g_return_val_if_fail(IS_INTERNET_ADDRESS_LIST(address_list), -1);

    len = internet_address_list_length(address_list);
    for (i = 0; i < len; i++) {
        const InternetAddress *ia =
            internet_address_list_get_address(address_list, i);

        if (INTERNET_ADDRESS_IS_MAILBOX(ia))
            ++n_mailboxes;
        else
            n_mailboxes +=
                libbalsa_address_n_mailboxes_in_list(INTERNET_ADDRESS_GROUP
                                                     (ia)->members);
    }

    return n_mailboxes;
}

/* =================================================================== */
/*                                UI PART                              */
/* =================================================================== */

/** libbalsa_address_set_edit_entries() initializes the GtkEntry widgets
    in entries with values from address
*/
void
libbalsa_address_set_edit_entries(const LibBalsaAddress * address,
                                  GtkWidget **entries)
{
    gchar *new_name = NULL;
    gchar *new_email = NULL;
    gchar *new_organization = NULL;
    gchar *first_name = NULL;
    gchar *last_name = NULL;
    gchar *nick_name = NULL;
    gint cnt;
    GtkListStore *store;
    GtkTreeIter iter;

    new_email = g_strdup(address
                         && address->address_list
                         && address->address_list->data ?
                         address->address_list->data : "");
    /* initialize the organization... */
    if (!address || address->organization == NULL)
	new_organization = g_strdup("");
    else
	new_organization = g_strdup(address->organization);

    /* if the message only contains an e-mail address */
    if (!address || address->full_name == NULL)
	new_name = g_strdup(new_email);
    else {
        gchar **names;
        g_assert(address);
	/* make sure address->personal is not all whitespace */
	new_name = g_strstrip(g_strdup(address->full_name));

	/* guess the first name and last name */
	if (*new_name != '\0') {
	    names = g_strsplit(new_name, " ", 0);

	    for (cnt=0; names[cnt]; cnt++)
		;

	    /* get first name */
	    first_name = g_strdup(address->first_name
                                  ? address->first_name : names[0]);

	    /* get last name */
            if(address->last_name)
                last_name = g_strdup(address->last_name);
            else {
                if (cnt == 1)
                    last_name = g_strdup("");
                else
                    last_name = g_strdup(names[cnt - 1]);
            }
	    g_strfreev(names);
	}
    }

    if (first_name == NULL)
	first_name = g_strdup("");
    if (last_name == NULL)
	last_name = g_strdup("");
    if (!address || address->nick_name == NULL)
	nick_name = g_strdup("");
    else
	nick_name = g_strdup(address->nick_name);

    /* Full name must be set after first and last names. */
    gtk_entry_set_text(GTK_ENTRY(entries[FIRST_NAME]), first_name);
    gtk_entry_set_text(GTK_ENTRY(entries[LAST_NAME]), last_name);
    gtk_entry_set_text(GTK_ENTRY(entries[FULL_NAME]), new_name);
    gtk_entry_set_text(GTK_ENTRY(entries[NICK_NAME]), nick_name);
    gtk_entry_set_text(GTK_ENTRY(entries[ORGANIZATION]), new_organization);

    store = GTK_LIST_STORE(gtk_tree_view_get_model
                           (GTK_TREE_VIEW(entries[EMAIL_ADDRESS])));
    gtk_list_store_clear(store);
    if (address) {
        GList *list;

        for (list = address->address_list; list; list = list->next) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, list->data, -1);
        }
    } else {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "", -1);
    }

    gtk_editable_select_region(GTK_EDITABLE(entries[FULL_NAME]), 0, -1);

    for (cnt = FULL_NAME + 1; cnt < NUM_FIELDS; cnt++)
        if (GTK_IS_EDITABLE(entries[cnt]))
            gtk_editable_set_position(GTK_EDITABLE(entries[cnt]), 0);

    g_free(new_name);
    g_free(first_name);
    g_free(last_name);
    g_free(nick_name);
    g_free(new_email);
    g_free(new_organization);
    gtk_widget_grab_focus(entries[FULL_NAME]);
}

/** libbalsa_address_get_edit_widget() returns an widget adapted
    for a LibBalsaAddress edition, with initial values set if address
    is provided. The edit entries are set in entries array
    and enumerated with LibBalsaAddressField constants
*/
static void
lba_entry_changed(GtkEntry * entry, GtkEntry ** entries)
{
    gchar *full_name =
        g_strconcat(gtk_entry_get_text(entries[FIRST_NAME]), " ",
                    gtk_entry_get_text(entries[LAST_NAME]), NULL);
    gtk_entry_set_text(entries[FULL_NAME], full_name);
    g_free(full_name);
}

static void
lba_cell_edited(GtkCellRendererText * cell, const gchar * path_string,
                const gchar * new_text, GtkListStore * store)
{
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store),
                                            &iter, path_string))
        gtk_list_store_set(store, &iter, 0, new_text, -1);
}

static GtkWidget *
lba_address_list_widget(GCallback changed_cb, gpointer changed_data)
{
    GtkListStore *store;
    GtkWidget *tree_view;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    store = gtk_list_store_new(1, G_TYPE_STRING);
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), FALSE);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "editable", TRUE, NULL);
    g_signal_connect(renderer, "edited", G_CALLBACK(lba_cell_edited),
                     store);
    if (changed_cb)
        g_signal_connect_swapped(renderer, "edited",
                                 changed_cb, changed_data);

    column = gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                      "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    return tree_view;
}

static void
add_row(GtkWidget*button, gpointer data)
{
    GtkTreeView *tv = GTK_TREE_VIEW(data);
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(tv));
    GtkTreeIter iter;
    GtkTreePath *path;
    gtk_list_store_insert_with_values(store, &iter, 99999, 0, "", -1);
    gtk_widget_grab_focus(GTK_WIDGET(tv));
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
    gtk_tree_view_set_cursor(tv, path, NULL, TRUE);
    gtk_tree_path_free(path);
}

GtkTargetEntry libbalsa_address_target_list[] = {
    {"text/plain",           0, LIBBALSA_ADDRESS_TRG_STRING },
    {"x-application/x-addr", GTK_TARGET_SAME_APP,LIBBALSA_ADDRESS_TRG_ADDRESS}
};

static void
addrlist_drag_received_cb(GtkWidget * widget, GdkDragContext * context,
                          gint x, gint y, GtkSelectionData * selection_data,
                          guint target_type, guint32 time, gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    gboolean dnd_success = FALSE;
    LibBalsaAddress *addr;

    printf("drag_received:\n");
    /* Deal with what we are given from source */
    if(selection_data
       && gtk_selection_data_get_length(selection_data) >= 0) {
        switch (target_type) {
        case LIBBALSA_ADDRESS_TRG_ADDRESS:
            addr = *(LibBalsaAddress **)
                gtk_selection_data_get_data(selection_data);
            if(addr && addr->address_list) {
                g_print ("string: %s\n", (gchar*)addr->address_list->data);
                gtk_list_store_insert_with_values(GTK_LIST_STORE(model),
                                                  &iter, 99999,
                                                  0,
                                                  addr->address_list->data,
                                                  -1);
                dnd_success = TRUE;
            }
            break;
        case LIBBALSA_ADDRESS_TRG_STRING:
            g_print("text/plain target not implemented.\n");
            break;
        default: g_print ("nothing good");
        }
    }

    if (!dnd_success)
        g_print ("DnD data transfer failed!\n");

    gtk_drag_finish(context, dnd_success, FALSE, time);
}

static gboolean
addrlist_drag_drop_cb(GtkWidget *widget, GdkDragContext *context,
                      gint x, gint y, guint time, gpointer user_data)
{
  gboolean        is_valid_drop_site;
  GdkAtom         target_type;
  GList          *targets;

  /* Check to see if (x,y) is a valid drop site within widget */
  is_valid_drop_site = TRUE;

  /* If the source offers a target */
  targets = gdk_drag_context_list_targets(context);
  if (targets) {
      /* Choose the best target type */
      target_type = GDK_POINTER_TO_ATOM
        (g_list_nth_data (targets, LIBBALSA_ADDRESS_TRG_ADDRESS));

      /* Request the data from the source. */
      printf("drag_drop requests target=%p\n", target_type);
      gtk_drag_get_data
        (
         widget,         /* will receive 'drag-data-received' signal */
         context,        /* represents the current state of the DnD */
         target_type,    /* the target type we want */
         time            /* time stamp */
         );
  } else {
      is_valid_drop_site = FALSE;
  }

  return  is_valid_drop_site;
}


GtkWidget*
libbalsa_address_get_edit_widget(const LibBalsaAddress *address,
                                 GtkWidget **entries,
                                 GCallback changed_cb, gpointer changed_data)
{
    static const gchar *labels[NUM_FIELDS] = {
	N_("D_isplayed Name:"),
	N_("_First Name:"),
	N_("_Last Name:"),
	N_("_Nickname:"),
	N_("O_rganization:"),
        N_("_Email Address:")
    };

    GtkWidget *grid, *label, *lhs;
    gint cnt;

    grid = gtk_grid_new();
#define HIG_PADDING 6
    gtk_grid_set_row_spacing(GTK_GRID(grid), HIG_PADDING);
    gtk_grid_set_column_spacing(GTK_GRID(grid), HIG_PADDING);
    g_object_set(G_OBJECT(grid), "margin", HIG_PADDING, NULL);

    for (cnt = 0; cnt < NUM_FIELDS; cnt++) {
        if (!labels[cnt])
            continue;
	label = gtk_label_new_with_mnemonic(_(labels[cnt]));
	gtk_widget_set_halign(label, GTK_ALIGN_END);
        if (cnt == EMAIL_ADDRESS) {
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
            GtkWidget *but = gtk_button_new_with_mnemonic(_("A_dd"));
            GtkTargetList *list;

            entries[cnt] = lba_address_list_widget(changed_cb,
                                                   changed_data);
            gtk_box_pack_start(GTK_BOX(box), label);
            gtk_box_pack_start(GTK_BOX(box), but);
            lhs = box;
            g_signal_connect(but, "clicked", G_CALLBACK(add_row),
                             entries[cnt]);

            list = gtk_target_list_new(libbalsa_address_target_list,
                                       G_N_ELEMENTS(libbalsa_address_target_list));
            gtk_drag_dest_set(entries[cnt],
                              GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
                              list,
                              GDK_ACTION_COPY);
            gtk_target_list_unref(list);

            g_signal_connect(G_OBJECT(entries[cnt]), "drag-data-received",
                             G_CALLBACK(addrlist_drag_received_cb), NULL);
            g_signal_connect (G_OBJECT(entries[cnt]), "drag-drop",
                              G_CALLBACK (addrlist_drag_drop_cb), NULL);
        } else {
            entries[cnt] = gtk_entry_new();
            if (changed_cb)
                g_signal_connect_swapped(entries[cnt], "changed",
                                         changed_cb, changed_data);
            lhs = label;
        }
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), entries[cnt]);

	gtk_grid_attach(GTK_GRID(grid), lhs, 0, cnt + 1, 1, 1);

	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);

        gtk_widget_set_hexpand(entries[cnt], TRUE);
        gtk_widget_set_vexpand(entries[cnt], TRUE);
	gtk_grid_attach(GTK_GRID(grid), entries[cnt], 1, cnt + 1, 1, 1);
    }
    g_signal_connect(entries[FIRST_NAME], "changed",
                     G_CALLBACK(lba_entry_changed), entries);
    g_signal_connect(entries[LAST_NAME], "changed",
                     G_CALLBACK(lba_entry_changed), entries);

    libbalsa_address_set_edit_entries(address, entries);

    if (changed_cb) {
        GtkTreeModel *model =
            gtk_tree_view_get_model(GTK_TREE_VIEW(entries[EMAIL_ADDRESS]));
        g_signal_connect_swapped(G_OBJECT(model), "row-inserted",
                                 changed_cb, changed_data);
        g_signal_connect_swapped(G_OBJECT(model), "row-changed",
                                 changed_cb, changed_data);
        g_signal_connect_swapped(G_OBJECT(model), "row-deleted",
                                 changed_cb, changed_data);
    }

    return grid;
}

LibBalsaAddress *
libbalsa_address_new_from_edit_entries(GtkWidget ** entries)
{
#define SET_FIELD(f,e)\
  do{ (f) = g_strstrip(gtk_editable_get_chars(GTK_EDITABLE(e), 0, -1));\
      if( !(f) || !*(f)) { g_free(f); (f) = NULL; }                    \
 else { while( (p=strchr(address->full_name,';'))) *p = ','; }  } while(0)

    LibBalsaAddress *address;
    char *p;
    GList *list = NULL;
    GtkTreeModel *model;
    gboolean valid;
    GtkTreeIter iter;

    /* make sure gtk_tree_model looses focus, otherwise the list does
     * not get updated (gtk2-2.8.20) */
    gtk_widget_grab_focus(entries[FULL_NAME]);
    /* FIXME: This problem should be solved in the VCard
       implementation in libbalsa: semicolons mess up how GnomeCard
       processes the fields, so disallow them and replace them
       by commas. */

    address = libbalsa_address_new();
    SET_FIELD(address->full_name,   entries[FULL_NAME]);
    if (!address->full_name) {
        g_object_unref(address);
        return NULL;
    }
    SET_FIELD(address->first_name,  entries[FIRST_NAME]);
    SET_FIELD(address->last_name,   entries[LAST_NAME]);
    SET_FIELD(address->nick_name,   entries[NICK_NAME]);
    SET_FIELD(address->organization,entries[ORGANIZATION]);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(entries[EMAIL_ADDRESS]));
    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        gchar *email;

        gtk_tree_model_get(model, &iter, 0, &email, -1);
        if (email && *email)
            list = g_list_prepend(list, email);
    }
    address->address_list = g_list_reverse(list);

    return address;
}
