/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

/*
 * An LDIF addressbook. See rfc-2849 for format.
 */

#include "config.h"

#include <gnome.h>

#include <stdio.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>

#include "address-book.h"
#include "abook-completion.h"
#include "address-book-ldif.h"
#include "information.h"

/* FIXME: Make an option */
#define CASE_INSENSITIVE_NAME

static LibBalsaAddressBookClass *parent_class = NULL;

static void libbalsa_address_book_ldif_class_init(LibBalsaAddressBookLdifClass *klass);
static void libbalsa_address_book_ldif_init(LibBalsaAddressBookLdif *ab);
static void libbalsa_address_book_ldif_finalize(GObject * object);

static LibBalsaABErr libbalsa_address_book_ldif_load(LibBalsaAddressBook * ab,
                                                     const gchar *filter,
                                                     LibBalsaAddressBookLoadFunc 
                                                     callback,
                                                     gpointer closure);
static LibBalsaABErr
libbalsa_address_book_ldif_add_address(LibBalsaAddressBook *ab,
                                       LibBalsaAddress *address);
static LibBalsaABErr
libbalsa_address_book_ldif_remove_address(LibBalsaAddressBook *ab,
                                          LibBalsaAddress *address);
static LibBalsaABErr
libbalsa_address_book_ldif_modify_address(LibBalsaAddressBook *ab,
                                          LibBalsaAddress *address,
                                          LibBalsaAddress *newval);

static void libbalsa_address_book_ldif_save_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static void libbalsa_address_book_ldif_load_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static GList *libbalsa_address_book_ldif_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix,
							 gchar ** new_prefix);

static gchar *build_name(gchar *cn, gchar *givenname, gchar *surname);
/*
static CompletionData *completion_data_new(LibBalsaAddress * address,
					   gboolean alias);
static void completion_data_free(CompletionData * data);
static gchar *completion_data_extract(CompletionData * data);
static gint address_compare(LibBalsaAddress *a, LibBalsaAddress *b);*/

static gboolean load_ldif_file(LibBalsaAddressBook *ab);

static gboolean ldif_address_book_need_reload(LibBalsaAddressBookLdif *ab);


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
	    (GInstanceInitFunc) libbalsa_address_book_ldif_init
	};

	address_book_ldif_type =
            g_type_register_static(LIBBALSA_TYPE_ADDRESS_BOOK,
	                           "LibBalsaAddressBookLdif",
			           &address_book_ldif_info, 0);
    }

    return address_book_ldif_type;

}

static void
libbalsa_address_book_ldif_class_init(LibBalsaAddressBookLdifClass *
				       klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GObjectClass *object_class;

    parent_class = g_type_class_peek_parent(klass);

    object_class = G_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->finalize = libbalsa_address_book_ldif_finalize;

    address_book_class->load = libbalsa_address_book_ldif_load;
    address_book_class->add_address =
	libbalsa_address_book_ldif_add_address;
    address_book_class->remove_address =
	libbalsa_address_book_ldif_remove_address;
    address_book_class->modify_address =
	libbalsa_address_book_ldif_modify_address;

    address_book_class->save_config =
	libbalsa_address_book_ldif_save_config;
    address_book_class->load_config =
	libbalsa_address_book_ldif_load_config;

    address_book_class->alias_complete =
	libbalsa_address_book_ldif_alias_complete;

}

static void
libbalsa_address_book_ldif_init(LibBalsaAddressBookLdif * ab)
{
    ab->path = NULL;
    ab->address_list = NULL;
    ab->mtime = 0;

    ab->name_complete  = 
	g_completion_new((GCompletionFunc)completion_data_extract);
    ab->alias_complete = 
	g_completion_new((GCompletionFunc)completion_data_extract);
}

static void
ab_ldif_clear(LibBalsaAddressBookLdif *addr_ldif)
{
    g_list_foreach(addr_ldif->address_list, (GFunc) g_object_unref, NULL);
    g_list_free(addr_ldif->address_list);
    addr_ldif->address_list = NULL;
    
    g_list_foreach(addr_ldif->name_complete->items, 
		   (GFunc)completion_data_free, NULL);
    g_list_foreach(addr_ldif->alias_complete->items, 
		   (GFunc)completion_data_free, NULL);
}

static void
libbalsa_address_book_ldif_finalize(GObject * object)
{
    LibBalsaAddressBookLdif *addr_ldif;

    addr_ldif = LIBBALSA_ADDRESS_BOOK_LDIF(object);

    g_free(addr_ldif->path);

    ab_ldif_clear(addr_ldif);

    g_completion_free(addr_ldif->name_complete); 
    addr_ldif->name_complete = NULL;
    g_completion_free(addr_ldif->alias_complete);
    addr_ldif->alias_complete = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_ldif_new(const gchar * name, const gchar * path)
{
    LibBalsaAddressBookLdif *abvc;
    LibBalsaAddressBook *ab;

    abvc =
        LIBBALSA_ADDRESS_BOOK_LDIF(g_object_new
                                   (LIBBALSA_TYPE_ADDRESS_BOOK_LDIF,
                                    NULL));
    ab = LIBBALSA_ADDRESS_BOOK(abvc);

    ab->name = g_strdup(name);
    abvc->path = g_strdup(path);

    return ab;
}

/* BASE64 conversion routines. Let us know if you know a better place
 * for them. These routines are very closely based on GPL libmutt code.
 */

#define BAD     -1
static int Index_64[128] = {
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
    52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
    -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
    -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
};

#define base64val(c) Index_64[(unsigned int)(c)]
static char B64Chars[64] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
  'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
  't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', '+', '/'
};

/* raw bytes to null-terminated base 64 string */
static void
string_to_base64(unsigned char *out, const unsigned char *in, size_t len,
		 size_t olen)
{
  while (len >= 3 && olen > 10)
  {
    *out++ = B64Chars[in[0] >> 2];
    *out++ = B64Chars[((in[0] << 4) & 0x30) | (in[1] >> 4)];
    *out++ = B64Chars[((in[1] << 2) & 0x3c) | (in[2] >> 6)];
    *out++ = B64Chars[in[2] & 0x3f];
    olen  -= 4;
    len   -= 3;
    in    += 3;
  }

  /* clean up remainder */
  if (len > 0 && olen > 4)
  {
    unsigned char fragment;

    *out++ = B64Chars[in[0] >> 2];
    fragment = (in[0] << 4) & 0x30;
    if (len > 1)
      fragment |= in[1] >> 4;
    *out++ = B64Chars[fragment];
    *out++ = (len < 2) ? '=' : B64Chars[(in[1] << 2) & 0x3c];
    *out++ = '=';
  }
  *out = '\0';
}

/* Convert '\0'-terminated base 64 string to raw bytes.
 * Returns length of returned buffer, or -1 on error */
static int
string_from_base64 (char *out, const char *in)
{
  int len = 0;
  register unsigned char digit1, digit2, digit3, digit4;

  do
  {
    digit1 = in[0];
    if (digit1 > 127 || base64val (digit1) == BAD)
      return -1;
    digit2 = in[1];
    if (digit2 > 127 || base64val (digit2) == BAD)
      return -1;
    digit3 = in[2];
    if (digit3 > 127 || ((digit3 != '=') && (base64val (digit3) == BAD)))
      return -1;
    digit4 = in[3];
    if (digit4 > 127 || ((digit4 != '=') && (base64val (digit4) == BAD)))
      return -1;
    in += 4;

    /* digits are already sanity-checked */
    *out++ = (base64val(digit1) << 2) | (base64val(digit2) >> 4);
    len++;
    if (digit3 != '=')
    {
      *out++ = ((base64val(digit2) << 4) & 0xf0) | (base64val(digit3) >> 2);
      len++;
      if (digit4 != '=')
      {
	*out++ = ((base64val(digit3) << 6) & 0xc0) | base64val(digit4);
	len++;
      }
    }
  }
  while (*in && digit4 != '=');

  *out = '\0';
  return len;
}

/* according to rfc2849, value_spec must be either 7-bit ASCII
   (safe-string) or a base64-string. Or an url, which is not
   implemented yet.
*/
static gchar*
string_to_value_spec(const gchar* str)
{
    gboolean issafe = 1;
    const gchar* p;

    for(p=str; *p && issafe; p++)
	issafe = (*p &0x80) ==0;

    if(issafe) 
	return g_strconcat(" ",str, NULL);
    else {
	int len = strlen(str);
	int sz = (len*4)/3+13;
	gchar* res = g_malloc(sz+2);
	strcpy(res, ": ");
	string_to_base64(res+2, str, len, sz);
	return res;
    }
}

static gchar*
value_spec_to_string(gchar* str)
{
    gchar *res;
    if(str[0] == ':') {
	res = g_malloc(strlen(str)+1);
	string_from_base64(res, g_strchug(str+1));
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
    gchar* str;
    int len, emptyp = 1;
    
    while( fgets(buf, sizeof(buf), f)) {
	emptyp = 0;
	g_string_append(res, buf);
	if((len=strlen(buf))> 0 && buf[len-1] == '\n') break;
    }
    g_strchomp(res->str);
    if(res->str[res->len] == '\n') res->str[res->len] = '\0';
    str = emptyp ? NULL : res->str;
    g_string_free(res, emptyp);
    return str;
}
	
    
static gboolean
ldif_address_book_need_reload(LibBalsaAddressBookLdif *ab)
{
    struct stat stat_buf;

    if ( stat(ab->path, &stat_buf) == -1 )
	return TRUE;

    if ( stat_buf.st_mtime > ab->mtime ) {
	ab->mtime = stat_buf.st_mtime;
	return TRUE;
    } else
	return FALSE;
}

static LibBalsaABErr
libbalsa_address_book_ldif_load(LibBalsaAddressBook * ab, 
                                const gchar *filter,
                                LibBalsaAddressBookLoadFunc callback, 
                                gpointer closure)
{
    GList *lst;

    if(!load_ldif_file(ab)) return LBABERR_CANNOT_READ;

    for (lst = LIBBALSA_ADDRESS_BOOK_LDIF(ab)->address_list; 
	 lst; lst = g_list_next(lst)) {
	if (callback)
	    callback(ab, LIBBALSA_ADDRESS(lst->data), closure);
    }
    if(callback) callback(ab, NULL, closure);
    return LBABERR_OK;
}

/* address_new_prefill:
   takes over the string ownership!
*/
static LibBalsaAddress*
address_new_prefill(GList* address_list, gchar* nickn, gchar* givenn, 
		    gchar* surn, gchar* fulln, gchar* org)
{
    LibBalsaAddress* address = libbalsa_address_new();
    
    address->address_list = address_list;
    
    address->first_name = givenn ? givenn : (nickn ? nickn : g_strdup(""));
    address->last_name = surn ? surn : g_strdup("");
    address->full_name = build_name(fulln, address->first_name, surn);
    address->organization = org ? org : g_strdup("");
    
    address->nick_name = nickn ? nickn : 
	g_strdup(address->full_name ? address->full_name : _("No-Id"));
    
    if (address->full_name == NULL)
	address->full_name = g_strdup(_("No-Name"));

    return address;
}
    
static gboolean
load_ldif_file(LibBalsaAddressBook *ab)
{
    FILE *gc;
    gchar *line;
    gchar *surname = NULL, *givenname = NULL, *nickname = NULL,
	*fullname = NULL, *organization = NULL;
    gint in_ldif = FALSE;
    GList *list = NULL;
    GList *completion_list = NULL;
    GList *address_list = NULL;
    CompletionData *cmp_data;

    LibBalsaAddressBookLdif *addr_ldif = LIBBALSA_ADDRESS_BOOK_LDIF(ab);

    if ( !ldif_address_book_need_reload(addr_ldif) )
	return TRUE;

    ab_ldif_clear(addr_ldif);
    
    g_completion_clear_items(addr_ldif->name_complete);
    g_completion_clear_items(addr_ldif->alias_complete);
    
    if( (gc = fopen(addr_ldif->path, "r")) == NULL)
        return FALSE;

    for (; (line=read_line(gc)) != NULL; g_free(line) ) {
	/*
	 * Check if it is a card.
	 */
	if (g_ascii_strncasecmp(line, "dn:", 3) == 0) {
	    in_ldif = TRUE;
	    continue;
	}

	g_strchomp(line);

	/*
	 * We are done loading a card.
	 */
	if (line[0] == '\0') {
	    LibBalsaAddress *address;
	    if (address_list) {
		address = address_new_prefill(g_list_reverse(address_list),
					      nickname, 
					      givenname, surname, fullname,
					      organization);
		list = g_list_prepend(list, address);
		address_list = NULL;
	    } else {            /* record without e-mail address, ignore */
		g_free(nickname);
		g_free(givenname);
		g_free(surname);
		g_free(organization);
	    }
	    nickname = givenname = surname = organization = NULL;
	    in_ldif = FALSE;
	    continue;
	}

	if (!in_ldif)
	    continue;

	if (g_ascii_strncasecmp(line, "cn:", 3) == 0) {
	    fullname = value_spec_to_string(g_strchug(line + 3));
	    continue;
	}

	if (g_ascii_strncasecmp(line, "sn:", 3) == 0) {
	    surname = value_spec_to_string(g_strchug(line + 3));
	    continue;
	}

	if (g_ascii_strncasecmp(line, "givenname:", 10) == 0) {
	    givenname = value_spec_to_string(g_strchug(line + 10));
	    continue;
	}

	if (g_ascii_strncasecmp(line, "xmozillanickname:", 17) == 0) {
	    nickname = value_spec_to_string(g_strchug(line + 17));
	    continue;
	}

	if (g_ascii_strncasecmp(line, "o:", 2) == 0) {
	    organization = value_spec_to_string(g_strchug(line + 2));
	    continue;
	}

	if (g_ascii_strncasecmp(line, "member:", 7) == 0) {
		address_list = 
		    g_list_prepend(address_list, 
				   member_value_to_mail(g_strchug(line+7)));
	    continue;
	}

	/*
	 * fetch all e-mail fields
	 */
	if (g_ascii_strncasecmp(line, "mail:", 5) == 0) {
	    address_list = 
		g_list_prepend(address_list, 
			       value_spec_to_string(g_strchug(line + 5)));
	}
    }
    fclose(gc);

    if(in_ldif) {
	LibBalsaAddress *address;
	if (address_list) {
	    address = address_new_prefill(address_list, nickname, givenname,
					  surname, fullname, organization);


	    /* FIXME: Split into Firstname and Lastname... */

	    list = g_list_prepend(list, address);
	} else {                /* record without e-mail address, ignore */
	    g_free(nickname);
	    g_free(givenname);
	    g_free(surname);
	    g_free(organization);
	}
    }

    list = g_list_sort(list, (GCompareFunc)address_compare);
    addr_ldif->address_list = list;

    completion_list = NULL;
    for (;list; list = g_list_next(list)) {
	cmp_data = completion_data_new(LIBBALSA_ADDRESS(list->data), FALSE);
	completion_list = g_list_prepend(completion_list, cmp_data);
    }
    completion_list = g_list_reverse(completion_list);
    g_completion_add_items(addr_ldif->name_complete, completion_list);
    g_list_free(completion_list);

    completion_list = NULL;
    for(list = addr_ldif->address_list; list; list = g_list_next(list) ) {
	cmp_data = completion_data_new(LIBBALSA_ADDRESS(list->data), TRUE);
	completion_list = g_list_prepend(completion_list, cmp_data);
    }
    completion_list = g_list_reverse(completion_list);
    g_completion_add_items(addr_ldif->alias_complete, completion_list);
    g_list_free(completion_list);
    ab->dist_list_mode = TRUE;
    return TRUE;
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

static LibBalsaABErr
libbalsa_address_book_ldif_add_address(LibBalsaAddressBook * ab,
                                       LibBalsaAddress * new_address)
{
    GList *list;
    gchar *cn = NULL;
    LibBalsaAddress *address;
    FILE *fp;
    gchar *value, *value_spec;
    LibBalsaABErr res = LBABERR_OK;

    load_ldif_file(ab); /* Ignore error if any; we may be adding */
                        /* the first entry in the book. */
    
    
    for(list = LIBBALSA_ADDRESS_BOOK_LDIF(ab)->address_list;
	list;
	list = g_list_next(list)) {
	address = LIBBALSA_ADDRESS(list->data);
	if (g_ascii_strcasecmp(address->full_name, new_address->full_name)==0)
	    return LBABERR_DUPLICATE;
    }
    
    fp = fopen(LIBBALSA_ADDRESS_BOOK_LDIF(ab)->path, "a");
    if (fp == NULL)
	return LBABERR_CANNOT_WRITE;

    if (new_address->full_name != NULL && 
	new_address->full_name[0] != '\0') {
	cn = g_strdup(new_address->full_name);
    } else {
	cn = build_name(NULL, new_address->first_name, 
			new_address->last_name);
	if (cn == NULL) {
	    cn = g_strdup(_("No-Name"));
	} else {
	    if(cn[0] == '\0') {
		g_free(cn);
		cn = g_strdup(_("No-Name"));
	    }
	}
    }

    if (new_address->address_list && new_address->address_list->data) {
	value = 
	    g_strdup_printf("cn=%s,mail=%s", 
			    cn,
			    (gchar *) new_address->address_list->data);
    } else 
	value = g_strdup_printf("cn=%s", cn);
    value_spec = string_to_value_spec(value);
    fprintf(fp, "\ndn:%s\n", value_spec);
    g_free(value_spec); g_free(value);

    value_spec = string_to_value_spec(cn);
    fprintf(fp, "cn:%s\n", value_spec);
    g_free(value_spec); g_free(cn);
    if (new_address->first_name && *(new_address->first_name)) {
	value_spec = string_to_value_spec(new_address->first_name);
	fprintf(fp, "givenname:%s\n", value_spec);
	g_free(value_spec);
    }
    if (new_address->last_name && *(new_address->last_name)) {
	value_spec = string_to_value_spec(new_address->last_name);
	fprintf(fp, "sn:%s\n", value_spec);
	g_free(value_spec);
    }
    if(new_address->nick_name) { 
	value_spec = string_to_value_spec(new_address->nick_name);
	fprintf(fp, "xmozillanickname:%s\n", value_spec);
	g_free(value_spec); 
    }

    if (new_address->organization && *(new_address->organization)) {
	value_spec = string_to_value_spec(new_address->organization);
	fprintf(fp, "o:%s\n", value_spec);
	g_free(value_spec);
    }
    
    for(list = new_address->address_list; list; list = g_list_next(list)) {
	if (list->data && *(gchar*)(list->data)) {
	    value_spec = string_to_value_spec((gchar *) list->data);
	    if(fprintf(fp, "mail:%s\n", value_spec) < 0)
                res = LBABERR_CANNOT_WRITE;
	    g_free(value_spec);
	}
    }
    fclose(fp);
    return res;
}

static LibBalsaABErr
libbalsa_address_book_ldif_remove_address(LibBalsaAddressBook *ab,
                                          LibBalsaAddress *address)
{
    /* FIXME: implement */
    return LBABERR_CANNOT_WRITE;
}

static LibBalsaABErr
libbalsa_address_book_ldif_modify_address(LibBalsaAddressBook *ab,
                                          LibBalsaAddress *address,
                                          LibBalsaAddress *newval)
{
    /* FIXME: implement */
    return LBABERR_CANNOT_WRITE;
}

static void
libbalsa_address_book_ldif_save_config(LibBalsaAddressBook * ab,
					const gchar * prefix)
{
    LibBalsaAddressBookLdif *vc;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDIF(ab));

    vc = LIBBALSA_ADDRESS_BOOK_LDIF(ab);

    gnome_config_set_string("Path", vc->path);

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config(ab, prefix);
}

static void
libbalsa_address_book_ldif_load_config(LibBalsaAddressBook * ab,
					const gchar * prefix)
{
    LibBalsaAddressBookLdif *vc;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDIF(ab));

    vc = LIBBALSA_ADDRESS_BOOK_LDIF(ab);

    g_free(vc->path);
    vc->path = gnome_config_get_string("Path");

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config(ab, prefix);
}

static GList*
libbalsa_address_book_ldif_alias_complete(LibBalsaAddressBook * ab,
					  const gchar * prefix,
					  gchar ** new_prefix)
{
    LibBalsaAddressBookLdif *vc;
    GList *resa = NULL, *resb = NULL;
    GList *res = NULL;
    gchar *p1 = NULL, *p2 = NULL;
    LibBalsaAddress *addr1, *addr2;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDIF(ab), NULL);

    vc = LIBBALSA_ADDRESS_BOOK_LDIF(ab);

    if ( ab->expand_aliases == FALSE )
	return NULL;

    load_ldif_file(ab);

    resa = g_completion_complete(vc->name_complete, (gchar*)prefix, &p1);
    resb = g_completion_complete(vc->alias_complete, (gchar*)prefix, &p2);

    if ( p1 && p2 ) {
	if ( strlen(p1) > strlen(p2) ) {
	    *new_prefix = p1;
	    g_free(p2);
	} else {
	    *new_prefix = p2;
	    g_free(p1);
	}
    } else {
	*new_prefix = p1?p1:p2;
    }

    /*
      Extract a list of addresses.
      pick any of them if two addresses point to the same structure.
      pick addr1 if it is available and there is no addr2
                    or it is smaller than addr1.
      in other case, pick addr2 (one of addr1 or addr2 must be not-null).
    */
    while ( resa || resb ) {
	addr1 = resa ? ((CompletionData*)resa->data)->address : NULL;
	addr2 = resb ? ((CompletionData*)resb->data)->address : NULL;

	if (addr1 == addr2) {
	    res = g_list_prepend(res, addr1);
	    g_object_ref(addr1);
	    resa = g_list_next(resa);
	    resb = g_list_next(resb);
	} else if (resa != NULL &&
		   (resb == NULL || address_compare(addr1, addr2) > 0) ) {
	    res = g_list_prepend(res, addr1);
	    g_object_ref(addr1);
	    resa = g_list_next(resa);
	} else {
	    res = g_list_prepend(res, addr2);
	    g_object_ref(addr2);
	    resb = g_list_next(resb);
	}
    }
    res = g_list_reverse(res);

    return res;
}
