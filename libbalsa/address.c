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

#include "config.h"

#include <string.h>

#include "address.h"

static GObjectClass *parent_class;

static void libbalsa_address_class_init(LibBalsaAddressClass * klass);
static void libbalsa_address_init(LibBalsaAddress * ab);
static void libbalsa_address_finalize(GObject * object);

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
    addr->id = NULL;
    addr->full_name = NULL;
    addr->first_name = NULL;
    addr->middle_name = NULL;
    addr->last_name = NULL;
    addr->organization = NULL;
    addr->address_list = NULL;
    addr->member_list = NULL;
}

static void
libbalsa_address_finalize(GObject * object)
{
    LibBalsaAddress *addr;

    g_return_if_fail(object != NULL);

    addr = LIBBALSA_ADDRESS(object);

    g_free(addr->id);           addr->id = NULL;
    g_free(addr->full_name);    addr->full_name = NULL;
    g_free(addr->first_name);   addr->first_name = NULL;
    g_free(addr->middle_name);  addr->middle_name = NULL;
    g_free(addr->last_name);    addr->last_name = NULL;
    g_free(addr->organization); addr->organization = NULL;

    g_list_foreach(addr->address_list, (GFunc) g_free, NULL);
    g_list_free(addr->address_list);
    addr->address_list = NULL;

    g_list_foreach(addr->member_list, (GFunc) g_object_unref, NULL);
    g_list_free(addr->member_list);
    addr->member_list = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddress *
libbalsa_address_new(void)
{
    return g_object_new(LIBBALSA_TYPE_ADDRESS, NULL);
}


#define ENABLE_OBSOLETED_CODE 1

/*  based on mutt code. GPL, Copyright (C) 1996-8 Michael R. Elkins
  <me@cs.hmc.edu>
*/

#if ENABLE_OBSOLETED_CODE
#include <ctype.h>

#define terminate_string(a, b, c) do { if ((b) < (c)) a[(b)] = 0; else \
       a[(c)] = 0; } while (0)

#define terminate_buffer(a, b) terminate_string(a, b, sizeof (a) - 1)

static const char RFC822Specials[] = "@.,:;<>[]\\\"()";
#define is_special(x) strchr(RFC822Specials,x)

typedef enum {
    RFC822_OK = 0,
    RFC822_ERR_MEMORY,                /* "out of memory" */
    RFC822_ERR_MISMATCH_PAREN,        /* "mismatched parenthesis" */
    RFC822_ERR_MISMATCH_QUOTE,        /* "mismatched quotes"      */
    RFC822_ERR_BAD_ROUTE,             /* "bad route in <>",       */
    RFC822_ERR_BAD_ROUTE_ADDR,        /* "bad address in <>",     */
    RFC822_ERR_BAD_ADDR_SPEC          /* "bad address spec"       */
} RFC822Error;


#define SKIPWS(c) while (*(c) && isspace ((unsigned char) *(c))) c++;

typedef struct RFC822Address_ {
    char *comment, *mailbox;
    struct RFC822Address_ *next; 
    int group:1;
} RFC822Address;

static void
rfc822_address_free(RFC822Address* addr)
{
    RFC822Address* next;
    while(addr) {
        next = addr->next;
        g_free(addr->comment);
        g_free(addr->mailbox);
        g_free(addr);
        addr = next;
    }
}
         
static void
rfc822_dequote_comment (GString *string)
{
    char* str = string->str;
    int w = 0, s;
    
    for (s=0; str[s]; s++) {
        if (str[s] == '\\') {
            if (!str[++s])
                break; /* error? */
            str[w++] = str[s];
        } else if (str[s] != '\"') {
            if (w != s)
                str[w] = str[s];
            w++;
        }
    }
    str[w] = '\0';
    string->len = w;
}

static const char *
parse_comment (const char *s, GString* comment, RFC822Error* err)
{
    int level = 1;
  
    while (*s && level) {
        if (*s == '(')
            level++;
        else if (*s == ')') {
            if (--level == 0) {
                s++;
                break;
            }
        } else if (*s == '\\') {
            if (!*++s)
                break;
        }
        g_string_append_c(comment, *s);
        s++;
    }
    if (level) {
        *err = RFC822_ERR_MISMATCH_PAREN;
        return NULL;
    }
    return s;
}

static const char *
parse_quote(const char *s, GString *token, RFC822Error* err)
{
    g_string_append_c(token, '"');
    while (*s) {
        g_string_append_c(token, *s);
        if (*s == '"')
            return (s + 1);
        if (*s == '\\') {
            if (!*++s)
                break;
            else {
                token->str[token->len-1] = *s;
            }
            s++;
        }
    }
    *err = RFC822_ERR_MISMATCH_QUOTE;
    return NULL;
}

static const char *
next_token(const char *s, GString *token, RFC822Error* err)
{
    if (*s == '(')
        return (parse_comment (s + 1, token, err));
    if (*s == '"')
        return (parse_quote (s + 1, token, err));
    if (is_special (*s)) {
        g_string_append_c(token, *s);
        return (s + 1);
    }
    while (*s) {
        if (isspace(*s) || is_special(*s))
            break;
        g_string_append_c(token, *s);
        s++;
    }
    return s;
}

static const char *
parse_mailboxdomain (const char *s, const char *nonspecial,
		     GString *mailbox, GString *comment, RFC822Error* err)
{
    const char *ps;
    
    while (*s) {
        SKIPWS (s);
        if (strchr (nonspecial, *s) == NULL && is_special (*s))
            return s;
        
        if (*s == '(') {
            g_string_append_c(comment, ' ');
            ps = next_token(s, comment, err);
        } else
            ps = next_token(s, mailbox, err);
        if (!ps)
            return NULL;
        s = ps;
    }
    
    return s;
}
static const char *
parse_address (const char *s, GString *token, GString *comment, 
               RFC822Address *addr, RFC822Error* err)
{
    s = parse_mailboxdomain (s, ".\"(\\", token, comment, err);
    if (!s)
        return NULL;

    if (*s == '@') {
        g_string_append_c(token, '@');
        s = parse_mailboxdomain (s + 1, ".([]\\", token,  comment, err);
        if (!s)
            return NULL;
    }
    
    if (token->len > 0 && token->str[0] != '@' && 
        token->str[token->len-1] != '@' &&
        (token->len >= 4 || !strchr(token->str, '@')))
        addr->mailbox = g_strdup(token->str);
    
    if (comment->len && !addr->comment)
        addr->comment = g_strdup(comment->str);
    
    return s;
}

static const char *
parse_route_addr (const char *s, GString* comment,
		  RFC822Address *addr, RFC822Error* err)
{
    GString* token = g_string_new("");

    SKIPWS (s);

    /* find the end of the route */
    if (*s == '@') {
        while (s && *s == '@') {
            g_string_append_c(token, '@');
            s = parse_mailboxdomain (s + 1, ".\\[](", token, comment, err);
        }
        if (!s || *s != ':') {
            *err = RFC822_ERR_BAD_ROUTE;
            g_string_free(token, TRUE);
            return NULL; /* invalid route */
        }
        g_string_append_c(token, ':');
        s++;
    }
    
    if ((s = parse_address (s, token, comment, addr, err)) == NULL) {
        g_string_free(token, TRUE);
        return NULL;
    }
    
    if (*s != '>' || !addr->mailbox) {
        *err = RFC822_ERR_BAD_ROUTE_ADDR;
        g_string_free(token, TRUE);
        return NULL;
    }
    
    s++;
    g_string_free(token, TRUE);
    return s;
}

static const char *
parse_addr_spec(const char *s,GString *comment, 
		 RFC822Address *addr, RFC822Error* err)
{
    GString *token = g_string_new("");
    
    s = parse_address(s, token, comment, addr, err);
    g_string_free(token, TRUE);

    if (s && *s && *s != ',' && *s != ';') {
        *err = RFC822_ERR_BAD_ADDR_SPEC;
        return NULL;
    }
    return s;
}

/* see rfc2822, 3.4.1 for definition of addr-spec */
static void
add_addrspec (RFC822Address **top, RFC822Address **last, const char *phrase,
	      GString *comment, RFC822Error* err)
{
    RFC822Address *cur = g_new0(RFC822Address,1);
  
    if (parse_addr_spec(phrase, comment, cur, err) == NULL) {
        rfc822_address_free(cur);
        return;
    }
    
    if (*last)
        (*last)->next = cur;
    else
        *top = cur;
    *last = cur;
}

static RFC822Address*
rfc822_parse_adrlist(RFC822Address *top, const char *s, RFC822Error* err)
{
    const char *begin, *ps;
    GString* comment, *phrase;
    RFC822Address *cur, *last = NULL;
  
    *err = 0;
    
    last = top;
    while (last && last->next)
        last = last->next;
    
    /* last-ditch sanity check: */
    if (s == NULL)
        return top;
    comment = g_string_new("");
    phrase  = g_string_new("");
    
    SKIPWS (s);
    begin = s;
    while (*s) {
        if (*s == ',') {
            if (phrase->len) {
                add_addrspec (&top, &last, phrase->str, comment, err);
            } else if (comment->len && last && !last->comment) {
                last->comment = g_strdup(comment->str);
            }
            g_string_truncate(comment, 0);
            g_string_truncate(phrase,  0);
            s++;
            begin = s;
            SKIPWS (begin);
        } else if (*s == '(') {
            if (comment->len)
                g_string_append_c(comment, ' ');
            if ((ps = next_token (s, comment, err)) == NULL) {
                rfc822_address_free(top);
                return NULL;
            }
            s = ps;
        } else if (*s == ':') {
            cur = g_new0(RFC822Address,1);
            cur->mailbox = g_strdup(phrase->str);
            cur->group = 1;
            
            if (last)
                last->next = cur;
            else
                top = cur;
            last = cur;
            
            g_string_truncate(comment, 0);
            g_string_truncate(phrase,  0);
            s++;
            begin = s;
            SKIPWS (begin);
        } else if (*s == ';') {
            if (phrase->len) {
                add_addrspec (&top, &last, phrase->str, comment, err);
            } else if (comment->len && last && !last->comment) {
                last->comment = g_strdup (comment->str);
            }

            /* add group terminator */
            cur = g_new0(RFC822Address,1);
            if (last) {
                last->next = cur;
                last = cur;
            }

            g_string_truncate(comment, 0);
            g_string_truncate(phrase,  0);
            s++;
            begin = s;
            SKIPWS (begin);
        } else if (*s == '<') {
            cur = g_new0(RFC822Address,1);
            if (phrase->len) {
                /* if we get something like "Michael R. Elkins" remove the quotes -
                   - but only in the case it would not introduce ambiguities.
                   The forbidden character set might be too narrow...*/
                if(strpbrk(phrase->str, ",;") == NULL)
                    rfc822_dequote_comment (phrase);
                g_free(cur->comment);
                cur->comment = g_strdup(phrase->str);
            }
            if ((ps = parse_route_addr (s + 1, comment, cur, err)) == NULL) {
                rfc822_address_free(top);
                rfc822_address_free(cur);
                return NULL;
            }
            
            if (last)
                last->next = cur;
            else
                top = cur;
            last = cur;
            
            g_string_truncate(comment, 0);
            g_string_truncate(phrase,  0);
            s = ps;
        } else {
            if (phrase->len && *s != '.')
                g_string_append_c(phrase, ' ');
            if ((ps = next_token (s, phrase, err)) == NULL) {
                rfc822_address_free(top);
                return NULL;
            }
            s = ps;
        }
        SKIPWS(s);
    }

    if (phrase->len) {
        add_addrspec (&top, &last, phrase->str, comment, err);
    } else if (comment->len && last && !last->comment) {
        last->comment = g_strdup(comment->str);
    }

    return top;
}

#endif


/* returns only first address on the list; ignores remaining ones */
LibBalsaAddress *
libbalsa_address_new_from_string(const gchar * str)
{
    RFC822Error err;
    RFC822Address* top = NULL, *list;
    LibBalsaAddress* addr;

    list = rfc822_parse_adrlist(top, str, &err);
    if(!list) return NULL;
    addr = libbalsa_address_new();
    addr->full_name = g_strdup(list->comment);
    addr->address_list = g_list_append(addr->address_list, 
                                       g_strdup(list->mailbox));
    rfc822_address_free(top);
    return addr;
}

GList*
libbalsa_address_new_list_from_string(const gchar * str)
{
    RFC822Error err;
    RFC822Address* top = NULL, *list;
    LibBalsaAddress* addr;
    GList* lst = NULL;

    list = rfc822_parse_adrlist(top, str, &err);
    while(list) {
        addr = libbalsa_address_new();
        addr->full_name = g_strdup(list->comment);
        addr->address_list = g_list_append(addr->address_list, 
                                           g_strdup(list->mailbox));
        list = list->next;
    }
    rfc822_address_free(top);
    return lst;
}

static gboolean
needs_quotes(const gchar *str)
{
    gboolean quoted = FALSE;

    while (*str) {
        if (*str == '\\') {
            if (*++str)
                ++str;
        } else {
            if (*str == '"')
                quoted = !quoted;
            else if (!quoted
                /* RFC 2822 specials, less '"': */
                 && strchr("()<>[]:;@\\,.", *str))
                return TRUE;
            ++str;
        }
    }
    return FALSE;
}

static gchar*
rfc2822_mailbox(const gchar *full_name, gchar *address)
{
    gchar *new_str;

    if(full_name && *full_name) {
        gchar *dequote = g_new(char, strlen(full_name) + 1);
        const gchar *p = full_name;
        gchar *q = dequote;
    
        do {
            if (*p == '\\') {
                *q++ = *p++;
                if (*p)
                    *q++ = *p++;
            } else if (*p == '"')
                ++p;
            else
                *q++ = *p++;
        } while (*p);
        *q = '\0';

        if (needs_quotes(dequote))
	    new_str = g_strdup_printf("\042%s\042 <%s>", dequote, address);
        else
            new_str = g_strdup_printf("%s <%s>", dequote, address);
        g_free(dequote);
    } else
	new_str = g_strdup(address);
    return new_str;
}


static gchar *rfc2822_group(const gchar *full_name, GList *addr_list)
{
    gchar *tmp_str;
    GString *str = g_string_new("");
    GList *addr_entry;

    if(full_name) { 
	if(needs_quotes(full_name))
	    g_string_printf(str, "\042%s\042: ", full_name);
	else
	    g_string_printf(str, "%s: ", full_name);
    }

    if(addr_list) {
	tmp_str = libbalsa_address_to_gchar(LIBBALSA_ADDRESS(addr_list->data), 0);
	g_string_append(str, tmp_str);
	g_free(tmp_str);

	for(addr_entry=g_list_next(addr_list); addr_entry; 
	    addr_entry=g_list_next(addr_entry)) {
	    tmp_str = libbalsa_address_to_gchar(LIBBALSA_ADDRESS(addr_entry->data), 0);
	    g_string_append_printf(str, ", %s", tmp_str);
	    g_free(tmp_str);
	}
    }
    if(full_name)
	g_string_append(str, ";");
    
    tmp_str=str->str;
    g_string_free(str, FALSE);
    
    return tmp_str;
}


static gchar *rfc2822_list(GList *list)
{
    gchar *retc = NULL; 
    GString *str;
    GList *addr_entry;
    
    g_return_val_if_fail(list!=NULL, NULL);

    str=g_string_new((gchar *)list->data);

    for(addr_entry=g_list_next(list); addr_entry; 
	addr_entry=g_list_next(addr_entry)) {
	g_string_append_printf(str, ", %s", (gchar *)addr_entry->data);
    }
    retc=str->str;
    g_string_free(str, FALSE);

    return retc;
}

/* private version */
gchar *
libbalsa_address_to_gchar_p(LibBalsaAddress * address, gint n);
gchar *
libbalsa_address_to_gchar_p(LibBalsaAddress * address, gint n)
{
    gchar *retc = NULL;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    if(address->member_list || (n==-1 && !address->address_list))
	retc = rfc2822_group(address->full_name, address->member_list);
    else if(n==-1) {
	retc = rfc2822_list(address->address_list);
    } else {
	GList *nth_address;

	nth_address = g_list_nth(address->address_list, n);

	g_return_val_if_fail(nth_address != NULL, NULL);

	retc = rfc2822_mailbox(address->full_name, nth_address->data);
    }
    
    return retc;
}

/* 
   Get a string version of this address.

   If n == -1 then return all addresses, else return the n'th one.
   If n > the number of addresses, will cause an error.
*/
gchar *
libbalsa_address_to_gchar(LibBalsaAddress * address, gint n)
{
    return libbalsa_address_to_gchar_p(address, n);
}

const gchar *
libbalsa_address_get_name(const LibBalsaAddress * addr)
{
    return addr->full_name ? addr->full_name :
	(addr->address_list ? addr->address_list->data : NULL);
}

#if ENABLE_ESMTP

/* XXX - added by Brian Stafford <brian@stafford.uklinux.net> */

/* libESMTP works with the RFC 821 mailbox and the RFC 822 phrase and 
   mailbox as seperate entities.  Because of this it is useful to add
   these extra methods. */

/* Extract the RFC 822 phrase from the address.  Almost the same
   as libbalsa_address_get_name() except returns NULL if no phrase. */
const gchar *
libbalsa_address_get_phrase(LibBalsaAddress * address)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    return address->full_name;
}

#endif
/* Extract the nth RFC 821/RFC 822 mailbox from the address. */
const gchar *
libbalsa_address_get_mailbox(LibBalsaAddress * address, gint n)
{
    GList *nth_address;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    nth_address = g_list_nth(address->address_list, n);
    g_return_val_if_fail(nth_address != NULL, NULL);
    return (gchar*)nth_address->data;
}

