/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * Rubrica2 address book support was written by Copyright (C)
 * Albrecht Dreﬂ <albrecht.dress@arcor.de> 2007.
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
 * A Rubrica (XML) addressbook.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address-book-rubrica.h"

#if HAVE_RUBRICA

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "libbalsa-conf.h"
#include "abook-completion.h"
#include "misc.h"
#include <glib/gi18n.h>


static void
libbalsa_address_book_rubrica_class_init(LibBalsaAddressBookRubricaClass *
					 klass);
static void libbalsa_address_book_rubrica_init(LibBalsaAddressBookRubrica *
					       ab_rubrica);
static void libbalsa_address_book_rubrica_finalize(GObject * object);

static LibBalsaABErr libbalsa_address_book_rubrica_load(LibBalsaAddressBook
							* ab,
							const gchar *
							filter,
							LibBalsaAddressBookLoadFunc
							callback,
							gpointer data);
static GList
    *libbalsa_address_book_rubrica_alias_complete(LibBalsaAddressBook * ab,
						  const gchar * prefix);
static LibBalsaABErr
libbalsa_address_book_rubrica_add_address(LibBalsaAddressBook * ab,
					  LibBalsaAddress * new_address);
static LibBalsaABErr
libbalsa_address_book_rubrica_remove_address(LibBalsaAddressBook * ab,
					     LibBalsaAddress * address);
static LibBalsaABErr
libbalsa_address_book_rubrica_modify_address(LibBalsaAddressBook * ab,
					     LibBalsaAddress * address,
					     LibBalsaAddress * newval);

static LibBalsaABErr lbab_rubrica_load_xml(LibBalsaAddressBookRubrica *
					   ab_rubrica, xmlDocPtr * docptr);
static void lbab_insert_address_node(const LibBalsaAddress * address,
				     xmlNodePtr parent);
static gboolean lbab_rubrica_starts_from(const gchar * str,
					 const gchar * filter_hi);

static GSList *extract_cards(xmlNodePtr card);
static void extract_data(xmlNodePtr entry, gchar ** first_name,
			 gchar ** last_name, gchar ** nick_name);
static void extract_work(xmlNodePtr entry, gchar ** org);
static void extract_net(xmlNodePtr entry, GList ** mail_addrs);
static gchar *xml_node_get_attr(xmlNodePtr node, const xmlChar * attname);
static gchar *xml_node_get_text(xmlNodePtr node);

#define CXMLCHARP(x)  ((const xmlChar *)(x))

G_DEFINE_TYPE(LibBalsaAddressBookRubrica, libbalsa_address_book_rubrica,
        LIBBALSA_TYPE_ADDRESS_BOOK_TEXT);


static void
libbalsa_address_book_rubrica_class_init(LibBalsaAddressBookRubricaClass *
					 klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->finalize = libbalsa_address_book_rubrica_finalize;

    address_book_class->load = libbalsa_address_book_rubrica_load;
    address_book_class->add_address =
	libbalsa_address_book_rubrica_add_address;
    address_book_class->remove_address =
	libbalsa_address_book_rubrica_remove_address;
    address_book_class->modify_address =
	libbalsa_address_book_rubrica_modify_address;

    address_book_class->alias_complete =
	libbalsa_address_book_rubrica_alias_complete;
}

static void
libbalsa_address_book_rubrica_init(LibBalsaAddressBookRubrica * ab_rubrica)
{
    LibBalsaAddressBookText *ab_text =
        LIBBALSA_ADDRESS_BOOK_TEXT(ab_rubrica);
    ab_text->path = NULL;
    ab_text->item_list = NULL;
    ab_text->mtime = 0;

    ab_text->name_complete =
        libbalsa_completion_new((LibBalsaCompletionFunc)
                                completion_data_extract);
    libbalsa_completion_set_compare(ab_text->name_complete, strncmp_word);
}

static void
libbalsa_address_book_rubrica_finalize(GObject * object)
{
    LibBalsaAddressBookText *ab_text;

    ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(object);

    /* item_list is different from the default
     * LibBalsaAddressBookText::item_list, so we free it here. */
    g_slist_free_full(ab_text->item_list, g_object_unref);

    G_OBJECT_CLASS(libbalsa_address_book_rubrica_parent_class)->finalize(object);
}

/* Public method */
LibBalsaAddressBook *
libbalsa_address_book_rubrica_new(const gchar * name, const gchar * path)
{
    LibBalsaAddressBookRubrica *ab_rubrica;
    LibBalsaAddressBookText *ab_text;
    LibBalsaAddressBook *ab;

    ab_rubrica =
	LIBBALSA_ADDRESS_BOOK_RUBRICA(g_object_new
				      (LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA,
				       NULL));
    ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab_rubrica);
    ab = LIBBALSA_ADDRESS_BOOK(ab_rubrica);

    ab->name = g_strdup(name);
    ab_text->path = g_strdup(path);

    return ab;
}

/* Class methods */
static LibBalsaABErr
libbalsa_address_book_rubrica_load(LibBalsaAddressBook * ab,
				   const gchar * filter,
				   LibBalsaAddressBookLoadFunc callback,
				   gpointer data)
{
    LibBalsaAddressBookRubrica *ab_rubrica =
	LIBBALSA_ADDRESS_BOOK_RUBRICA(ab);
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    LibBalsaABErr load_res;
    gchar *filter_hi = NULL;
    GSList *list;

    g_return_val_if_fail(ab_text->path != NULL, LBABERR_CANNOT_READ);

    /* try to load the xml file if necessary */
    load_res = lbab_rubrica_load_xml(ab_rubrica, NULL);
    if (load_res != LBABERR_OK)
	return load_res;

    if (filter)
	filter_hi = g_utf8_strup(filter, -1);

    for (list = ab_text->item_list; list; list = list->next) {
	LibBalsaAddress *address = LIBBALSA_ADDRESS(list->data);

	if (!address)
	    continue;

	if (callback &&
	    (!filter_hi ||
	     lbab_rubrica_starts_from(address->last_name, filter_hi) ||
	     lbab_rubrica_starts_from(address->full_name, filter_hi)))
	    callback(ab, address, data);
    }
    if (callback)
	callback(ab, NULL, data);

    g_free(filter_hi);

    return LBABERR_OK;
}

/* Alias complete method */
static GList *
libbalsa_address_book_rubrica_alias_complete(LibBalsaAddressBook * ab,
					     const gchar * prefix)
{
    LibBalsaAddressBookRubrica *ab_rubrica =
        LIBBALSA_ADDRESS_BOOK_RUBRICA(ab);
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    GList *list;
    GList *res = NULL;

    if (ab->expand_aliases == FALSE)
        return NULL;

    if (lbab_rubrica_load_xml(ab_rubrica, NULL) != LBABERR_OK)
        return NULL;

    for (list =
         libbalsa_completion_complete(ab_text->name_complete,
                                      (gchar *) prefix);
         list; list = list->next) {
        InternetAddress *ia = ((CompletionData *) list->data)->ia;
        res = g_list_prepend(res, g_object_ref(ia));
    }

    return g_list_reverse(res);
}

/* store address method */
static LibBalsaABErr
libbalsa_address_book_rubrica_add_address(LibBalsaAddressBook * ab,
					  LibBalsaAddress * new_address)
{
    LibBalsaAddressBookRubrica *ab_rubrica =
	LIBBALSA_ADDRESS_BOOK_RUBRICA(ab);
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    int fd;
    xmlDocPtr doc = NULL;
    xmlNodePtr root_element = NULL;
    LibBalsaABErr result;

    /* try to load the current file (an empty file is ok) */
    result = lbab_rubrica_load_xml(ab_rubrica, &doc);
    if (result != LBABERR_OK && result != LBABERR_CANNOT_READ)
	return result;

    /* eject if we already have this address */
    if (g_slist_find_custom(ab_text->item_list, new_address,
			    (GCompareFunc) address_compare)) {
	xmlFreeDoc(doc);
	return LBABERR_DUPLICATE;
    }

    /* try to open the address book for writing */
    if ((fd = open(ab_text->path, O_WRONLY | O_CREAT, 0666)) == -1) {
	xmlFreeDoc(doc);
	return LBABERR_CANNOT_WRITE;
    }
    if (libbalsa_lock_file(ab_text->path, fd, TRUE, TRUE, FALSE) < 0) {
	xmlFreeDoc(doc);
	close(fd);
	return LBABERR_CANNOT_WRITE;
    }

    /* create a new xml document if necessary */
    if (!doc) {
	doc = xmlNewDoc(CXMLCHARP("1.0"));
	root_element =
	    xmlNewDocNode(doc, NULL, CXMLCHARP("Rubrica"), NULL);
	xmlDocSetRootElement(doc, root_element);
	xmlNewProp(root_element, CXMLCHARP("version"), CXMLCHARP("2.0.1"));
	xmlNewProp(root_element, CXMLCHARP("fileformat"), CXMLCHARP("0"));
	xmlNewProp(root_element, CXMLCHARP("doctype"),
		   CXMLCHARP("AddressBook"));
    } else
	root_element = xmlDocGetRootElement(doc);

    /* insert a new card */
    lbab_insert_address_node(new_address, root_element);

    /* store the document */
    if (xmlSaveFormatFileEnc(ab_text->path, doc, "UTF-8", 1) == -1)
	result = LBABERR_CANNOT_WRITE;
    else
	result = LBABERR_OK;
    libbalsa_unlock_file(ab_text->path, fd, FALSE);
    close(fd);
    xmlFreeDoc(doc);
    ab_text->mtime = 0;	/* force re-load upon the next access */

    /* done */
    return result;
}

/* Remove address method */
static LibBalsaABErr
libbalsa_address_book_rubrica_remove_address(LibBalsaAddressBook * ab,
					     LibBalsaAddress * address)
{
    return libbalsa_address_book_rubrica_modify_address(ab, address, NULL);
}

/* Remove address method */
static LibBalsaABErr
libbalsa_address_book_rubrica_modify_address(LibBalsaAddressBook * ab,
					     LibBalsaAddress * address,
					     LibBalsaAddress * newval)
{
    LibBalsaAddressBookRubrica *ab_rubrica =
	LIBBALSA_ADDRESS_BOOK_RUBRICA(ab);
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    int fd;
    xmlDocPtr doc = NULL;
    xmlNodePtr root_element;
    xmlNodePtr card;
    LibBalsaABErr result;
    gboolean found;

    /* try to load the current file */
    if ((result = lbab_rubrica_load_xml(ab_rubrica, &doc)) != LBABERR_OK)
	return result;

    /* check if we have a node with the correct full name */
    if (!(root_element = xmlDocGetRootElement(doc)) ||
	xmlStrcmp(root_element->name, CXMLCHARP("Rubrica"))) {
	xmlFreeDoc(doc);
	return LBABERR_ADDRESS_NOT_FOUND;
    }
    card = root_element->children;
    found = FALSE;
    while (card && !found) {
	if (xmlStrcmp(card->name, CXMLCHARP("Card"))) {
	    gchar *full_name = xml_node_get_attr(card, CXMLCHARP("name"));

	    if (full_name) {
		found = !g_ascii_strcasecmp(address->full_name, full_name);
		g_free(full_name);
	    }
	}
    }
    if (!found) {
	xmlFreeDoc(doc);
	return LBABERR_ADDRESS_NOT_FOUND;
    }

    /* remove the card from the document */
    xmlUnlinkNode(card);
    xmlFreeNode(card);

    /* add the new card */
    if (newval)
	lbab_insert_address_node(newval, root_element);

    /* try to open the address book for writing */
    if ((fd = open(ab_text->path, O_WRONLY | O_CREAT, 0666)) == -1) {
	xmlFreeDoc(doc);
	return LBABERR_CANNOT_WRITE;
    }
    if (libbalsa_lock_file(ab_text->path, fd, TRUE, TRUE, FALSE) < 0) {
	xmlFreeDoc(doc);
	close(fd);
	return LBABERR_CANNOT_WRITE;
    }

    /* store the document */
    if (xmlSaveFormatFileEnc(ab_text->path, doc, "UTF-8", 1) == -1)
	result = LBABERR_CANNOT_WRITE;
    else
	result = LBABERR_OK;
    libbalsa_unlock_file(ab_text->path, fd, FALSE);
    close(fd);
    xmlFreeDoc(doc);
    ab_text->mtime = 0;	/* force re-load upon the next access */

    /* done */
    return result;
}

/* -- helpers -- */
static LibBalsaABErr
lbab_rubrica_load_xml(LibBalsaAddressBookRubrica * ab_rubrica,
		      xmlDocPtr * docptr)
{
    LibBalsaAddressBookText *ab_text =
        LIBBALSA_ADDRESS_BOOK_TEXT(ab_rubrica);
    struct stat stat_buf;
    int fd;
    xmlDocPtr doc = NULL;
    xmlNodePtr root_element = NULL;
    GList *completion_list;
    CompletionData *cmp_data;
    GSList *list;

    /* init the return pointer (if any) */
    if (docptr)
	*docptr = NULL;

    /* eject if the file did not change on disk and no document result pointer is passed */
    if (!docptr && stat(ab_text->path, &stat_buf) == 0) {
	if (stat_buf.st_mtime == ab_text->mtime)
	    return LBABERR_OK;
	else
	    ab_text->mtime = stat_buf.st_mtime;
    }

    /* free old data */
    g_slist_foreach(ab_text->item_list, (GFunc) g_object_unref, NULL);
    g_slist_free(ab_text->item_list);
    ab_text->item_list = NULL;

    libbalsa_completion_clear_items(ab_text->name_complete);


    /* try to read the address book */
    if ((fd = open(ab_text->path, O_RDONLY)) == -1)
	return LBABERR_CANNOT_READ;
    if (libbalsa_lock_file(ab_text->path, fd, FALSE, TRUE, FALSE) < 0) {
	close(fd);
	return LBABERR_CANNOT_READ;
    }

    doc = xmlParseFile(ab_text->path);
    libbalsa_unlock_file(ab_text->path, fd, FALSE);
    close(fd);
    if (!doc)
	return LBABERR_CANNOT_READ;

    /* Get the root element node and extract cards if it is a Rubrica book */
    root_element = xmlDocGetRootElement(doc);
    if (!xmlStrcmp(root_element->name, CXMLCHARP("Rubrica")))
	ab_text->item_list = extract_cards(root_element->children);

    /* return the document if requested of free it */
    if (docptr)
	*docptr = doc;
    else
	xmlFreeDoc(doc);

    /* build the completion list */
    // FIXME - Rubrica provides groups...
    completion_list = NULL;
    for (list = ab_text->item_list; list; list = list->next) {
	LibBalsaAddress *address = LIBBALSA_ADDRESS(list->data);
	GList *l;

	if (!address)
	    continue;

	if (address->address_list->next
	    && LIBBALSA_ADDRESS_BOOK(ab_rubrica)->dist_list_mode) {
	    /* Create a group address. */
	    InternetAddress *ia =
		internet_address_group_new(address->full_name);
            InternetAddressGroup *group = (InternetAddressGroup *) ia;

	    for (l = address->address_list; l; l = l->next) {
		InternetAddress *member =
		    internet_address_mailbox_new(NULL, l->data);
		internet_address_group_add_member(group, member);
		g_object_unref(member);
	    }
	    cmp_data = completion_data_new(ia, address->nick_name);
	    completion_list = g_list_prepend(completion_list, cmp_data);
	    g_object_unref(ia);
	} else {
	    /* Create name addresses. */
	    GList *l;

	    for (l = address->address_list; l; l = l->next) {
		InternetAddress *ia =
		    internet_address_mailbox_new(address->full_name, l->data);
		cmp_data = completion_data_new(ia, address->nick_name);
		completion_list =
		    g_list_prepend(completion_list, cmp_data);
		g_object_unref(ia);
	    }
	}
    }

    completion_list = g_list_reverse(completion_list);
    libbalsa_completion_add_items(ab_text->name_complete, completion_list);
    g_list_free(completion_list);

    return LBABERR_OK;
}

static void
lbab_insert_address_node(const LibBalsaAddress * address,
			 xmlNodePtr parent)
{
    xmlNodePtr new_addr;
    xmlNodePtr new_data;
    GList *l;

    /* create a new card */
    new_addr = xmlNewChild(parent, NULL, CXMLCHARP("Card"), NULL);
    xmlNewProp(new_addr, CXMLCHARP("name"), CXMLCHARP(address->full_name));

    /* create the Data section of the card */
    new_data = xmlNewChild(new_addr, NULL, CXMLCHARP("Data"), NULL);
    xmlNewChild(new_data, NULL, CXMLCHARP("FirstName"),
		CXMLCHARP(address->first_name));
    xmlNewChild(new_data, NULL, CXMLCHARP("LastName"),
		CXMLCHARP(address->last_name));
    xmlNewChild(new_data, NULL, CXMLCHARP("NickName"),
		CXMLCHARP(address->nick_name));

    /* create the Work section of the card */
    new_data = xmlNewChild(new_addr, NULL, CXMLCHARP("Work"), NULL);
    xmlNewChild(new_data, NULL, CXMLCHARP("Organization"),
		CXMLCHARP(address->organization));

    /* create the Net section of the card */
    new_data = xmlNewChild(new_addr, NULL, CXMLCHARP("Net"), NULL);
    for (l = address->address_list; l != NULL; l = l->next) {
	xmlNodePtr new_mail =
	    xmlNewChild(new_data, NULL, CXMLCHARP("Uri"),
			CXMLCHARP(l->data));
	xmlNewProp(new_mail, CXMLCHARP("type"), CXMLCHARP("email"));
    }
}

/* Case-insensitive utf-8 string-has-prefix */
static gboolean
lbab_rubrica_starts_from(const gchar * str, const gchar * filter_hi)
{
    if (!str)
	return FALSE;

    while (*str && *filter_hi &&
	   g_unichar_toupper(g_utf8_get_char(str)) ==
	   g_utf8_get_char(filter_hi)) {
	str = g_utf8_next_char(str);
	filter_hi = g_utf8_next_char(filter_hi);
    }

    return *filter_hi == '\0';
}


/* XML stuff to extract the data we need from the Rubrica file */
static GSList *
extract_cards(xmlNodePtr card)
{
    GSList *addrlist = NULL;

    while (card) {
	if (!xmlStrcmp(card->name, CXMLCHARP("Card"))) {
	    LibBalsaAddress *address = libbalsa_address_new();
	    xmlNodePtr children;

	    address->full_name =
		xml_node_get_attr(card, CXMLCHARP("name"));
	    children = card->children;
	    while (children) {
		if (!xmlStrcmp(children->name, CXMLCHARP("Data")))
		    extract_data(children->children, &address->first_name,
				 &address->last_name, &address->nick_name);
		else if (!xmlStrcmp(children->name, CXMLCHARP("Work")))
		    extract_work(children->children,
				 &address->organization);
		else if (!xmlStrcmp(children->name, CXMLCHARP("Net")))
		    extract_net(children->children,
				&address->address_list);

		children = children->next;
	    }

	    if (address->address_list)
		addrlist = g_slist_prepend(addrlist, address);
	    else
		g_object_unref(address);
	}

	card = card->next;
    }

    return addrlist;
}


static void
extract_data(xmlNodePtr entry, gchar ** first_name, gchar ** last_name,
	     gchar ** nick_name)
{
    gchar *title = NULL;
    gchar *prefix = NULL;
    gchar *first = NULL;
    gchar *middle = NULL;
    GString *_first_name = NULL;

    while (entry) {
	if (!xmlStrcmp(entry->name, CXMLCHARP("FirstName")))
	    first = xml_node_get_text(entry);
	else if (!xmlStrcmp(entry->name, CXMLCHARP("MiddleName")))
	    middle = xml_node_get_text(entry);
	else if (!xmlStrcmp(entry->name, CXMLCHARP("Title")))
	    title = xml_node_get_text(entry);
	else if (!xmlStrcmp(entry->name, CXMLCHARP("NamePrefix")))
	    prefix = xml_node_get_text(entry);
	else if (!xmlStrcmp(entry->name, CXMLCHARP("LastName")))
	    *last_name = xml_node_get_text(entry);
	else if (!xmlStrcmp(entry->name, CXMLCHARP("NickName")))
	    *nick_name = xml_node_get_text(entry);

	entry = entry->next;
    }

    /* construct first name */
    if (title)
	_first_name = g_string_new(title);

    if (prefix) {
	if (_first_name) {
	    _first_name = g_string_append_c(_first_name, ' ');
	    _first_name = g_string_append(_first_name, prefix);
	} else
	    _first_name = g_string_new(prefix);
    }

    if (first) {
	if (_first_name) {
	    _first_name = g_string_append_c(_first_name, ' ');
	    _first_name = g_string_append(_first_name, first);
	} else
	    _first_name = g_string_new(first);
    }

    if (middle) {
	if (_first_name) {
	    _first_name = g_string_append_c(_first_name, ' ');
	    _first_name = g_string_append(_first_name, middle);
	} else
	    _first_name = g_string_new(middle);
    }

    if (_first_name)
	*first_name = g_string_free(_first_name, FALSE);
    else
	*first_name = NULL;
}


static void
extract_work(xmlNodePtr entry, gchar ** org)
{
    while (entry) {
	if (!xmlStrcmp(entry->name, CXMLCHARP("Organization"))) {
	    *org = xml_node_get_text(entry);
	    return;
	}

	entry = entry->next;
    }
}


static void
extract_net(xmlNodePtr entry, GList ** mail_addrs)
{
    while (entry) {
	gchar *uri_type = NULL;
	gchar *mail_addr;

	if (!xmlStrcmp(entry->name, CXMLCHARP("Uri"))
	    && (uri_type = xml_node_get_attr(entry, CXMLCHARP("type")))
	    && !strcmp(uri_type, "email")
	    && (mail_addr = xml_node_get_text(entry)))
	    *mail_addrs = g_list_prepend(*mail_addrs, mail_addr);
	g_free(uri_type);

	entry = entry->next;
    }
}


static gchar *
xml_node_get_text(xmlNodePtr node)
{
    g_return_val_if_fail(node != NULL, NULL);
    if ((node = node->children) && node->type == XML_TEXT_NODE
	&& node->content)
	return g_strdup((const gchar *) node->content);
    else
	return NULL;
}


static gchar *
xml_node_get_attr(xmlNodePtr node, const xmlChar * attname)
{
    xmlAttrPtr props;

    g_return_val_if_fail(node != NULL, NULL);
    props = node->properties;
    while (props) {
	if (props->type == XML_ATTRIBUTE_NODE
	    && !xmlStrcmp(props->name, attname) && props->children
	    && props->children->type == XML_TEXT_NODE)
	    return props->children->
		content ? g_strdup((const gchar *) props->children->
				   content) : NULL;
	props = props->next;
    }

    return NULL;
}


#endif				/* HAVE_RUBRICA */
