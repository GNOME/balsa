/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
 * This file includes two message threading functions.
 * The first is the implementation of jwz's algorithm describled at
 * http://www.jwz.org/doc/threading.html . The another is very simple and 
 * trivial one. If you confirm that your mailbox includes every threaded 
 * messages, the later will be enough. Those functions are selectable on
 * each mailbox by setting the 'type' member in BalsaIndex. If you don't need
 * message threading functionality, just specify 'BALSA_INDEX_THREADING_FLAT'. 
 *
 * ymnk@jcraft.com
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <glib.h>
#include "balsa-index.h"
#include "balsa-index-threading.h"
#include "main-window.h"

#define MESSAGE(node) ((LibBalsaMessage *)(GTK_CTREE_ROW(node)->row.data))

static void threading_jwz(BalsaIndex* bindex, int type);
static gboolean is_cyclic_tree(GNode* root, GNode* child);
static void gen_container(GtkCTree *ctree, 
			  GtkCTreeNode *node, 
			  GHashTable* id_table);
static GNode* get_container(LibBalsaMessage* message,
			    GHashTable* id_table);
static GNode* check_references(LibBalsaMessage* message,
			       GHashTable *id_table);
static void adopt_child(GNode* parent, GNode* container);
static void find_root_set(gpointer key, GNode* value, GSList ** root_set);
static gboolean prune(GNode *node, GSList *root_set);
static gboolean node_equal(GNode *node1, GNode **node2);
static gboolean construct(GNode *node, GtkCTree *ctree);
static void subject_gather(GNode *node, GHashTable* subject_table);
static void subject_merge(GNode *node, GHashTable* subject_table, 
			  GSList* root_set, GSList** save_node);
static void reparent(GNode* node, GNode* children);
static void free_node(gpointer key, GNode* value, gpointer data);
static gchar* chop_re(gchar* str);

static void threading_simple(BalsaIndex* bindex);
static void add_message(GtkCTree *ctree, 
			GtkCTreeNode *node, 
			GHashTable* msg_table);

static void dump(GNode *node, int indent);

void
balsa_index_threading(BalsaIndex* bindex)
{
    int type=bindex->threading_type;
    switch (type){
    case BALSA_INDEX_THREADING_SIMPLE:
	threading_simple(bindex);
	break;
    case BALSA_INDEX_THREADING_JWZ:
	threading_jwz(bindex, type);
	break;
    case BALSA_INDEX_THREADING_FLAT:
	break;
    default:
    }
}

static void
threading_jwz(BalsaIndex* bindex, int type)
{
    GHashTable *id_table;
    GHashTable *subject_table;
    GSList *root_set=NULL;
    GSList *save_node=NULL;
    GSList *foo=NULL;
    GtkCTree* ctree=GTK_CTREE(bindex->ctree);

    id_table=g_hash_table_new(g_str_hash, g_str_equal);
    gtk_ctree_pre_recursive(ctree, 
			    (GtkCTreeNode *)NULL, 
			    (GtkCTreeFunc)gen_container, 
			    (gpointer)id_table);

    g_hash_table_foreach(id_table, 
			 (GHFunc)find_root_set,
			 &root_set);

    balsa_window_setup_progress(BALSA_WINDOW(bindex->window), 
                                g_slist_length(root_set)*3);
    
    foo=root_set;
    while(foo) {
	g_node_traverse(foo->data,
			/*G_PRE_ORDER,*/
			G_POST_ORDER,
			G_TRAVERSE_ALL,
			-1,
			(GNodeTraverseFunc)prune,
			root_set);
	foo=g_slist_next(foo);
        balsa_window_increment_progress(BALSA_WINDOW(bindex->window));
    }

    subject_table = g_hash_table_new(g_str_hash, g_str_equal);
    g_slist_foreach(root_set, (GFunc)subject_gather, subject_table);


    foo = root_set;
    while(foo) {
	if(foo->data!=NULL)
	    subject_merge(foo->data, subject_table, root_set, &save_node);
	foo=g_slist_next(foo);
        balsa_window_increment_progress(BALSA_WINDOW(bindex->window));
    }

    foo = root_set;
    while(foo) {
	if(foo->data!=NULL)
	    g_node_traverse(foo->data,
			    G_PRE_ORDER,   
			    G_TRAVERSE_ALL,
			    -1,
			    (GNodeTraverseFunc)construct,
			    ctree);
	foo=g_slist_next(foo);
        balsa_window_increment_progress(BALSA_WINDOW(bindex->window));
    }

    foo=save_node;
    while(foo) {
	if(foo->data!=NULL) {
	    ((GNode*)(foo->data))->children=NULL;
	    g_node_destroy((GNode*)(foo->data));
	}
	foo=g_slist_next(foo);
        balsa_window_increment_progress(BALSA_WINDOW(bindex->window));
    }
    if(save_node!=NULL)
	g_slist_free(save_node);

    balsa_window_clear_progress(BALSA_WINDOW(bindex->window));
    g_hash_table_destroy(subject_table);
    g_hash_table_foreach(id_table, (GHFunc)free_node, NULL);
    g_hash_table_destroy(id_table);

    if(root_set!=NULL)
	g_slist_free(root_set);
}

static void
gen_container(GtkCTree *ctree, GtkCTreeNode *node, GHashTable *id_table)
{
    LibBalsaMessage* message=MESSAGE(node);

    GNode* container=NULL;
    GNode* parent=NULL;

    container=get_container(message, id_table);
    if(container==NULL) return;

    parent=check_references(message, id_table);
    if(parent==NULL) return;

    if(parent!=NULL
#if 0
       && parent->children==NULL
#endif
	) {
	adopt_child(parent, container);
    }
    /* dump(container, 0); */
}

static GNode*
check_references(LibBalsaMessage* message, GHashTable *id_table)
{
    /*
     * For each element in the message's References field:
     *   + Find a Container object for the given Message-ID:
     *     + If there's one in id_table use that;
     *     + Otherwise, make (and index) one with a null Message.
     *   + Link the References field's Containers together in the order
     *     implied by the References header.
     *     + If they are already linked, don't change the existing links.
     *     + Do not add a link if adding that link would introduce a loop:
     *       that is, before asserting A->B, search down the children of B
     *       to see if A is reachable.
     */

    GNode* parent=NULL;
    GList *reference=message->references_for_threading;

    while(reference) {
	char *id=(char *)(reference->data);
	GNode* foo=g_hash_table_lookup(id_table, id);
	if(foo==NULL) {
	    foo=g_node_new(NULL);
	    g_hash_table_insert(id_table, id, foo);
	}

	if(foo==parent) {
	    /* printf("duplicate!!\n"); */
	    reference=g_list_next(reference);
	    continue;
	}

	if(parent==NULL) {
	    parent=foo;
	    reference=g_list_next(reference);
	    continue;
	}

	if(is_cyclic_tree(foo, parent)) {
	    /* hmm.... */
	    /*printf("cyclic_tree 1#\n");*/
	    return NULL;
	}

	/*
	  printf("parent->children=%x, foo->parent=%x\n",
	  (int)(parent->children), (int)(foo->parent));
	*/

	if(parent->children==NULL && foo->parent==NULL) {
	    parent->children=foo;
	    foo->parent=parent;
	}
	else {
	    if(foo->parent!=NULL) {
		if(parent->children!=NULL) {
		    if(foo->parent==parent) {
			parent=foo;
			reference=g_list_next(reference);
			continue;
		    }

#if 1
		    /* This part is not defined in jwz's algorithm. */
		    parent=foo;
		    reference=g_list_next(reference);
		    continue;
#else
		    return NULL;
#endif
		}

		if(foo->children!=NULL) {  
		    if(g_list_next(reference)!=NULL &&
		       g_list_next(reference)->data!=NULL) {
			GNode *bar=g_hash_table_lookup(id_table,
						       g_list_next(reference)->data);
			if(bar==foo->children) {
			    parent=foo;
			    reference=g_list_next(reference);
			    continue;
			}
		    }
		}
		return NULL;
	    }

	    if(parent->children!=NULL) {
#if 1                                     
		/* This part is not defined in jwz's algorithm. */
		GNode* p=parent->children;
		while(p->next) {
		    if(p==foo) { break; }
		    p=p->next;
		}
		if(p!=foo) {            /* foo is not a children of parent. */
		    p->next=foo;
		    foo->prev=p;
		}
		if(foo->next) {           
		    p=foo->next;           /* foo has siblings. */
		    while(p) {
			p->parent=parent;
			p=p->next;
		    }
		}
		foo->parent=parent;
		parent=foo;
		reference=g_list_next(reference);
		continue;
	    }
#else
	    /* printf("return 2#\n"); */
	    return;
#endif
	}
	parent=foo;
	reference=g_list_next(reference);
    }
    return parent;
}

static void
adopt_child(GNode* parent, GNode* container)
{

    /*
     * Set the parent of this message to be the last element in References.
     * Note that this message may have a parent already: this can happen
     * because we saw this ID in a References field, and presumed a
     * parent based on the other entries in that field. Now that we have
     * the actual message, we can be more definitive, so throw away the
     * old parent and use this new one. Find this Container in the
     * parent's children list, and unlink it.
     *
     * Note that this could cause this message to now have no parent, if
     * it has no references field, but some message referred to it as the
     * non-first element of its references. (Which would have been some
     * kind of lie...)
     *
     * Note that at all times, the various ``parent'' and ``child''
     * fields must be kept inter-consistent.
     */

    if(container->parent==parent) return;

    if(is_cyclic_tree(container, parent)) {
	/*printf("cyclic 2#\n");*/
	return;
    }

    if(container->parent!=NULL) {
	GNode* foo=container->parent->children;
	if(foo==container) {
	    container->parent->children=container->next;
	    if(container->next!=NULL) {
		container->next->prev=NULL;
	    }
	}
	else{
	    container->prev->next=container->next;
	    if(container->next!=NULL) container->next->prev=container->prev;
	    if(container->prev!=NULL) container->prev->next=container->next;
	}
	if(container->next!=NULL)	container->next=NULL;
	if(container->prev!=NULL)	container->prev=NULL;
    }

    if(parent->children!=NULL) {
	parent->children->prev=container;
	container->next=parent->children;
    }
    parent->children=container;
    container->parent=parent;
}

static gboolean
is_cyclic_tree(GNode* root, GNode* child)
{
    gboolean find=FALSE;
    GNode* foo=NULL;
#if 0
    /* This case will be enough for jwz's rule */
    foo=root;
    while(foo ) {
	if(foo->children==child) {find=TRUE; break;}
	foo=foo->children;
    }
#else
    foo=child;
    g_node_traverse(root,
		    G_PRE_ORDER,   
		    G_TRAVERSE_ALL,
		    -1,
		    (GNodeTraverseFunc)node_equal,
		    &foo);
    if(foo==NULL) find=TRUE;
#endif
    return find;
}

static GNode*
get_container(LibBalsaMessage* message, GHashTable* id_table)
{
    GNode* container=NULL;

    /*
     * If id_table contains a Container for this ID:
     *   + Store this message in the Container's message slot.
     * else
     *   + Create a new Container object holding this message;
     *   + Index the Container by Message-ID in id_table.
     */
    
    if(!message->message_id) {
	return NULL;
    }

    container=g_hash_table_lookup(id_table, message->message_id);
    if(container!=NULL) {
	container->data=message;
    }
    else{
	container=g_node_new(message);
	g_hash_table_insert(id_table, message->message_id, container);
    }
    return container;
}

static void
find_root_set(gpointer key, GNode* value, GSList ** root_set)
{
    /*
     * Walk over the elements of id_table, and gather a list of the 
     * Container objects that have no parents. 
     */

    if(value->parent!=NULL) return;
    *root_set=g_slist_append(*root_set, value);
}

static gboolean 
prune(GNode *node, GSList *root_set)
{
    /*
     * Recursively walk all containers under the root set. For each container: 
     *
     * + If it is an empty container with no children, nuke it. 
     * + If the Container has no Message, but does have children, 
     *   remove this container but promote its children to this level 
     *  (that is, splice them in to the current child list.) 
     *
     * Do not promote the children if doing so would promote them to 
     * the root set -- unless there is only one child, in which case, do. 
     */

    if(node->data==NULL && node->children==NULL) {
	/*
	  printf("node! %x, %x\n", (int)node, (int)(node->parent));
	*/
	if(node->parent!=NULL) {
	    if(node->prev==NULL) {
		node->parent->children=node->next;
		if(node->next!=NULL)
		    node->next->prev=NULL;
	    }
	    else {
		node->prev->next=node->next;
		if(node->next!=NULL)
		    node->next->prev=node->prev;
	    }
	}
	else {
	    GSList* foo=root_set;
	    while(foo) {
		if(foo->data==node) {
		    foo->data=NULL;
		    break;
		}
		foo=g_slist_next(foo);
	    }
	}
	return FALSE;
    }

    if(node->children!=NULL && node->data==NULL && 
       (node->parent!=NULL || node->children->next==NULL)) {
	if(node->parent==NULL) {
	    while(root_set) {
		if(root_set->data==node) {root_set->data=node->children; break;}
		root_set=g_slist_next(root_set);
	    }
	    node->children->parent=NULL;
	}
	else {
	    GNode* foo=node->parent->children;
	    if(node->prev==NULL) {
		node->parent->children=node->children;
	    }
	    else {
		node->prev->next=node->children;
		node->children->prev=node->prev;
	    }

	    foo=node->children;
	    while(foo->next!=NULL) {
		foo->parent=node->parent;
		foo=foo->next;
	    }

	    if(foo==node->children) {
		foo->parent=node->parent;
	    }
	    foo->next=node->next;
	    if(node->next!=NULL) 
		node->next->prev=foo;
	    {
		GNode* bar=node->parent->children;
		while(bar) {
		    if(bar==node) {printf("find!!!");break;}
		    bar=bar->next;
		}
	    }
	}
	return FALSE;
    }

    return FALSE;
}

static gboolean 
node_equal(GNode *node, GNode** parent)
{
    if(node==*parent) {
	*parent=NULL;
	return TRUE;
    }
    return FALSE;
}


static void
dump(GNode *node, int indent)
{
    int i=indent;
    GNode *children=node->children;
    while(i) {printf(" "); i--;}
    printf("%x, %x(%s)%d\n", 
	   (int)(node), 
	   (int)(node->data),
	   ((node->data!=NULL) ?
	    /* ((LibBalsaMessage *)(((GNode*)(node)->data)))->message_id : */
	    ((LibBalsaMessage *)(((GNode*)(node)->data)))->subject :
	    "empty"),
	   ((node->data!=NULL) ?
	    (int)(((LibBalsaMessage *)(((GNode*)(node)->data)))->msgno+1) :
	    -1)
	);
    while(children) {
	dump(children, indent+2);
	children=children->next;
    }
}

static gboolean 
construct(GNode *node, GtkCTree *ctree)
{
    GtkCTreeNode *ctreenode=NULL;
    GtkCTreeNode *ctreeparent=NULL;
    GtkCTreeNode *sibling=NULL;
    LibBalsaMessage *message=NULL;

    /*
      printf("construct: %x, %x, %x, %x\n", (int)node,(int)(node->parent),
      (int)(node->next),(int)(node->children));
    */

    if(node->parent!=NULL && node->parent->data!=NULL) {
	message=(LibBalsaMessage *)(node->parent->data);
	ctreeparent=gtk_ctree_find_by_row_data(ctree, NULL, (gpointer) message);
    }

    if(node->data!=NULL) {
	message=(LibBalsaMessage *)(node->data);
	ctreenode=gtk_ctree_find_by_row_data(ctree, NULL, (gpointer) message);
	if(ctreenode==NULL)
	    printf("null!! 1\n");
	gtk_ctree_move(ctree, ctreenode, ctreeparent, NULL);
    }

    if(node->next!=NULL) {
	message=(LibBalsaMessage *)(node->next->data);
	sibling=gtk_ctree_find_by_row_data(ctree, NULL, (gpointer) message);
	if(sibling==NULL)
	    printf("null!! 2 message=%x\n", (int)message); 
	gtk_ctree_move(ctree, sibling, ctreeparent, ctreenode);
    }

    if(node->children!=NULL) {
	GNode *children=node->children;
	GtkCTreeNode *foo=NULL;
	while(children) {
	    message=(LibBalsaMessage *)(children->data);
	    foo=gtk_ctree_find_by_row_data(ctree, NULL, (gpointer) message);
	    if(foo!=NULL) {
		gtk_ctree_move(ctree, foo, ctreenode, NULL);
	    }
	    children=children->next;
	}
    }

    return FALSE;
}

static void
subject_gather(GNode *node, GHashTable* subject_table)
{
    LibBalsaMessage *message=NULL;
    gchar *subject=NULL;
    gchar *chopped_subject=NULL;
    GNode* old;

    /*
     * If any two members of the root set have the same subject, merge them. 
     * This is so that messages which don't have References headers at all 
     * still get threaded (to the extent possible, at least.) 
     *
     * + Construct a new hash table, subject_table, which associates subject 
     *   strings with Container objects. 
     *
     * + For each Container in the root set: 
     *
     *   Find the subject of that sub-tree: 
     *   + If there is a message in the Container, the subject is the subject of
     *     that message. 
     *   + If there is no message in the Container, then the Container will have
     *     at least one child Container, and that Container will have a message.
     *     Use the subject of that message instead. 
     *   + Strip ``Re:'', ``RE:'', ``RE[5]:'', ``Re: Re[4]: Re:'' and so on. 
     *   + If the subject is now "", give up on this 
     *   + Add this Container to the subject_table if: Container. 
     *     + There is no container in the table with this subject, or 
     *     + This one is an empty container and the old one is not: the empty 
     *       one is more interesting as a root, so put it in the table instead. 
     *     + The container in the table has a ``Re:'' version of this subject,
     *       and this container has a non-``Re:'' version of this subject.
     *       The non-re version is the more interesting of the two. 
     *
     * + Now the subject_table is populated with one entry for each subject 
     *   which occurs in the root set. Now iterate over the root set, 
     *   and gather together the difference. 
     * 
     *   For each Container in the root set: 
     * 
     *   Find the subject of this Container (as above.) 
     *   Look up the Container of that subject in the table. 
     *   If it is null, or if it is this container, continue. 
     *   Otherwise, we want to group together this Container and the one 
     *   in the table. There are a few possibilities: 
     *     + If both are dummies, append one's children to the other, and 
     *       remove the now-empty container. 
     * 
     *     + If one container is a empty and the other is not, make the 
     *       non-empty one be a child of the empty, and a sibling of the 
     *       other ``real'' messages with the
     *       same subject (the empty's children.) 
     *     + If that container is a non-empty, and that message's subject 
     *       does not begin with ``Re:'', but this message's subject does,
     *       then make this be a child of the other. 
     *     + If that container is a non-empty, and that message's subject 
     *       begins with ``Re:'', but this message's subject does not, 
     *       then make that be a child of this one -- they were misordered. 
     *       (This happens somewhat implicitly, since if there are two 
     *       messages, one with Re: and one without, the one without
     *       will be in the hash table, regardless of the order in which 
     *       they were seen.) 
     * 
     *     + Otherwise, make a new empty container and make both msgs be
     *       a child of it. This catches the both-are-replies and 
     *       neither-are-replies cases, and makes them be siblings instead of
     *       asserting a hierarchical relationship which might not be true. 
     * 
     *     (People who reply to messages without using ``Re:'' and without
     *     using a References line will break this slightly. Those people suck.) 
     * 
     *     (It has occurred to me that taking the date or message number into
     *     account would be one way of resolving some of the ambiguous cases,
     *     but that's not altogether straightforward either.) 
     */

    if(node==NULL) return;

    message=(LibBalsaMessage *)(node->data!=NULL ? 
				node->data : 
				node->children->data);
    
    g_return_if_fail(message!=NULL);

    subject=message->subject;
    if(subject==NULL)return;
    chopped_subject=chop_re(subject);
    if(chopped_subject==NULL) {
	return;
    }

    /*
      printf("subject_gather: subject %s, %s\n", subject, chopped_subject);
    */

    old=g_hash_table_lookup(subject_table, chopped_subject);
    if(old==NULL ||
       (node->data==NULL&&old->data!=NULL)) {
	g_hash_table_insert(subject_table, chopped_subject, node);
	return;
    }

    {
	LibBalsaMessage *old_message=(LibBalsaMessage *)(old->data!=NULL ?
							 old->data :
							 old->children->data);
	g_return_if_fail(old_message!=NULL);

	if(old_message->subject!=chop_re(old_message->subject) &&
	   subject==chopped_subject) {
	    g_hash_table_insert(subject_table, chopped_subject, node);
	}
    }
    return;
}

static void
subject_merge(GNode *node, GHashTable* subject_table, 
	      GSList* root_set, GSList** save_node)
{
    LibBalsaMessage *message=NULL;
    gchar *subject=NULL;
    gchar *chopped_subject=NULL;
    GNode* node2;

    if(node==NULL) return;

    message=(LibBalsaMessage *)(node->data!=NULL ? 
				node->data : 
				node->children->data);

    g_return_if_fail(message!=NULL);

    subject=message->subject;
    if(subject==NULL)return;
    chopped_subject=chop_re(subject);
    if(chopped_subject==NULL) {
	return;
    }

    node2=g_hash_table_lookup(subject_table, chopped_subject);
    if(node2==NULL || node2==node) {
	return;
    }

    if(node->data==NULL && node2->data==NULL) {
	reparent(node2, node->children);
	node->children=NULL;
	return;
    }

    if(node->data==NULL && node2->data!=NULL) {
	reparent(node, node2->children);
	node2->children=NULL;
	return;
    }
    if(node2->data==NULL && node->data!=NULL) {
	reparent(node2, node->children);
	node->children=NULL;
	return;
    }

    if(node2->data!=NULL) {
	LibBalsaMessage *message2=(LibBalsaMessage *)(node2->data);
	gchar* chopped_subject2=chop_re(message2->subject);
	if((message2->subject==chopped_subject2) &&
	   subject!=chopped_subject) {
	    GSList* foo=root_set;
	    reparent(node2, node);

	    while(foo) {
		if(foo->data==node)
		    foo->data=NULL;
		foo=g_slist_next(foo);
	    }
	    return;
	}
	if((message2->subject!=chopped_subject2) &&
	   subject==chopped_subject) {
	    GSList* foo=root_set;
	    reparent(node, node2);
	    g_hash_table_insert(subject_table, chopped_subject, node);
	    while(foo) {
		if(foo->data==node2)
		    foo->data=NULL;
		foo=g_slist_next(foo);
	    }
	    return;
	}
    }

    {
	GNode *new_node=g_node_new(NULL);
	GSList* foo=root_set;
	while(foo) {
	    if(foo->data==node)
		foo->data=new_node;
	    else if(foo->data==node2)
		foo->data=NULL;
	    foo=g_slist_next(foo);
	}
	reparent(new_node, node);
	reparent(new_node, node2);
      
	*save_node=g_slist_append(*save_node, new_node);
    }
    return;
}

static void
reparent(GNode* node, GNode* children)
{
    GNode* p=children;
    while(p) {
	p->parent=node;
	p=p->next;
    }
    p=node->children;
    if(p!=NULL) {
	while(p->next)p=p->next;
	p->next=children;
	if(children!=NULL)children->prev=p;
    }
    else
	node->children=children;
}

/* The more heuristics should be added. */
static gchar *
chop_re(gchar* str)
{
    gchar *p=str;
    while(*p) {
	while(*p && *p==' ') p++;
	if(*p && g_strncasecmp(p, "RE:", 3)==0) {
	    p+=3;
	    continue;
	}
	break;
    }
    return p;
}

static void
free_node(gpointer key, GNode* node, gpointer data)
{
    node->data=NULL;
    node->children=NULL;
    g_node_destroy(node);
}

/* yet another message threading function */
static void
threading_simple(BalsaIndex* bindex)
{
    LibBalsaMessage* message;
    GtkCTreeNode* sibling;
    GtkCTreeNode* parent;
    GList *root_children=NULL;
    GList *p=NULL;
    GtkCTree* ctree=GTK_CTREE(bindex->ctree);
    GHashTable *msg_table;

    msg_table=g_hash_table_new(g_str_hash, g_str_equal);

    gtk_ctree_pre_recursive(ctree, 
			    (GtkCTreeNode *)NULL, 
			    (GtkCTreeFunc)add_message,
			    (gpointer)msg_table);
    sibling=gtk_ctree_node_nth(ctree, 0);
    while(sibling!=NULL) {
	root_children=g_list_append(root_children, sibling);
	sibling=GTK_CTREE_ROW(sibling)->sibling;
    }
    p=root_children;
    balsa_window_setup_progress(BALSA_WINDOW(bindex->window),
                                g_list_length(p));
    while(p) {
	parent=NULL;
	message=MESSAGE(p->data);
	if(message->references_for_threading!=NULL) {
	    parent= g_hash_table_lookup(
		msg_table, 
		g_list_last(message->references_for_threading)->data);
	    if(parent!=NULL) {
		if(p->data==NULL)
		    printf("null!! 4\n"); 
		else
		    gtk_ctree_move(ctree, p->data, parent, NULL);
	    }
	}
	p=g_list_next(p);
        balsa_window_increment_progress(BALSA_WINDOW(bindex->window));
    }
    balsa_window_clear_progress(BALSA_WINDOW(bindex->window));
    g_list_free(root_children);
    g_hash_table_destroy(msg_table);
}

static void
add_message(GtkCTree *ctree, GtkCTreeNode *node, GHashTable *msg_table)
{
    LibBalsaMessage* message=MESSAGE(node);
    GtkCTreeNode *foo=NULL;
    if(!message->message_id)
	return;
    foo=g_hash_table_lookup(msg_table, message->message_id);
    if(foo==NULL)
	g_hash_table_insert(msg_table, message->message_id, node);
}
