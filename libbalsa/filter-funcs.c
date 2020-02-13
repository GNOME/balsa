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
 * filter-funcs.c
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <glib/gi18n.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "missing.h"

#include "filter-funcs.h"
#include "filter-private.h"

/* Conditions */

static gchar*
get_quoted_string(gchar **pstr)
{
    gchar *str = *pstr;
    GString *res = g_string_new("");
    if(*str == '"') {
        while(*++str && *str != '"') {
            if(*str == '\\') str++;
            g_string_append_c(res, *str);
        }
	if(*str == '"')
	  ++str;
    } else {
        while(*++str && !isspace((int)*str))
            g_string_append_c(res, *str);
    }
    *pstr = str;
    return g_string_free(res, FALSE);
}

static void
append_quoted_string(GString *res, const char *str)
{
    g_string_append_c(res, '"');
    while(*str) {
        if(*str == '"' || *str == '\\')
            g_string_append_c(res, '\\');
        g_string_append_c(res, *str++);
    }
    g_string_append_c(res, '"');
}

static LibBalsaCondition *
lbcond_new(ConditionMatchType type, gboolean negated)
{
    LibBalsaCondition *cond;

    cond = g_new(LibBalsaCondition, 1);
    cond->type      = type;
    cond->negate    = !!negated;
    cond->ref_count = 1;

    return cond;
}

#ifndef FIXME
LibBalsaCondition*
libbalsa_condition_new(void)
{
    LibBalsaCondition *cond;

    cond = lbcond_new(CONDITION_STRING, FALSE);
    cond->match.string.fields      = 0;
    cond->match.string.string      = NULL;
    cond->match.string.user_header = NULL;
    return cond;
}
#endif

static LibBalsaCondition*
libbalsa_condition_new_string_parse(gboolean negated, gchar **string)
{
    char *str, *user_header = NULL;
    int i, headers = atoi(*string);
    for(i=0; (*string)[i] && isdigit((int)(*string)[i]); i++)
        ;
    if((*string)[i] != ' ')
        return NULL;
    *string += i+1;
    if( headers & CONDITION_MATCH_US_HEAD) {
        user_header = get_quoted_string(string);
        if(!user_header)
            return NULL;
        if(*(*string)++ != ' ') {
            g_free(user_header); return NULL;
        }
    }
    str = get_quoted_string(string);
    if(str == NULL) {
        g_free(user_header);
        return NULL;
    }
    return libbalsa_condition_new_string(negated, headers, str, user_header);
}

LibBalsaCondition*
libbalsa_condition_new_string(gboolean negated, unsigned headers,
                              gchar *str, gchar *user_header)
{
    LibBalsaCondition *cond;

    cond = lbcond_new(CONDITION_STRING, negated);
    cond->match.string.fields      = headers;
    cond->match.string.string      = str;
    cond->match.string.user_header = user_header;

    return cond;
}
LibBalsaCondition*
libbalsa_condition_new_date(gboolean negated, time_t *from, time_t *to)
{
    LibBalsaCondition *cond;

    cond = lbcond_new(CONDITION_DATE, negated);
    cond->match.date.date_low  = from ? *from : 0;
    cond->match.date.date_high = to   ? *to   : 0;

    return cond;
}

static LibBalsaCondition*
libbalsa_condition_new_date_parse(gboolean negated, gchar **string)
{
    LibBalsaCondition *cond;
    gchar *hi, *lo = get_quoted_string(string);
    time_t tlo, thi;
    struct tm date;

    if(lo == NULL)
        return NULL;
    if(*(*string)++ != ' ') {
        g_free(lo);
        return NULL;
    }
    hi=get_quoted_string(string);
    if(hi == NULL) {
        g_free(lo);
        return NULL;
    }

    /* strptime with our format will not set time, only date */
    memset(&date, 0, sizeof(date));

    if(*lo == '\0')
       tlo = 0;
    else {
        strptime(lo, "%Y-%m-%d", &date);
        tlo = mktime(&date);
    }
    if(*hi == '\0')
       thi =  0;
    else {
        strptime(hi, "%Y-%m-%d", &date);
        thi = mktime(&date) + 24*3600 - 1 /* 24*3600 - 1 = 23:59:59 */;
    }
        
    cond = lbcond_new(CONDITION_DATE, negated);
    cond->match.date.date_low  = tlo;
    cond->match.date.date_high = thi;
    g_free(lo);
    g_free(hi);

    return cond;
}

static LibBalsaCondition*
libbalsa_condition_new_flag(gboolean negated, gchar **string)
{
    int i, flags = atoi(*string);
    for(i=0; (*string)[i] && isdigit((int)(*string)[i]); i++)
        ;
    *string += i;

    return libbalsa_condition_new_flag_enum(negated, flags);
}

LibBalsaCondition*
libbalsa_condition_new_flag_enum(gboolean negated, LibBalsaMessageFlag flags)
{
    LibBalsaCondition *cond;

    cond = lbcond_new(CONDITION_FLAG, negated);
    cond->match.flags = flags;

    return cond;
}

static LibBalsaCondition*
libbalsa_condition_new_bool(gboolean negated, ConditionMatchType cmt,
                            gchar **string)
{
    LibBalsaCondition *left, *right, *retval;

    left = libbalsa_condition_new_from_string(string);
    if(left == NULL)
        return NULL;
    if(*(*string)++ != ' ' || 
       (right = libbalsa_condition_new_from_string(string)) == NULL) {
        libbalsa_condition_unref(left);
        return NULL;
    }

    retval = libbalsa_condition_new_bool_ptr(negated, cmt, left, right);
    libbalsa_condition_unref(left);
    libbalsa_condition_unref(right);

    return retval;
}

/* libbalsa_condition_new_bool_ptr
 *
 * refs the left and right conditions, so the caller must unref
 * after calling.
 */
LibBalsaCondition*
libbalsa_condition_new_bool_ptr(gboolean negated, ConditionMatchType cmt,
                                LibBalsaCondition *left,
                                LibBalsaCondition *right)
{
    LibBalsaCondition *cond;

    g_return_val_if_fail(left != NULL || right != NULL,  NULL);

    if (!left)
        return libbalsa_condition_ref(right);
    if (!right)
        return libbalsa_condition_ref(left);

    cond = lbcond_new(cmt, negated);
    cond->match.andor.left  = libbalsa_condition_ref(left);
    cond->match.andor.right = libbalsa_condition_ref(right);

    return cond;
}

static LibBalsaCondition*
libbalsa_condition_new_and(gboolean negated, gchar **string)
{
    return libbalsa_condition_new_bool(negated, CONDITION_AND, string);
}

static LibBalsaCondition*
libbalsa_condition_new_or(gboolean negated, gchar **string)
{
    return libbalsa_condition_new_bool(negated, CONDITION_OR, string);
}

LibBalsaCondition*
libbalsa_condition_new_from_string(gchar **string)
{
    static const struct {
        const char *key;
        unsigned keylen;
        LibBalsaCondition *(*parser)(gboolean negate, gchar **str);
    } cond_types[] = {
        { "STRING ", 7, libbalsa_condition_new_string_parse },
        { "DATE ",   5, libbalsa_condition_new_date_parse   },
        { "FLAG ",   5, libbalsa_condition_new_flag   },
        { "AND ",    4, libbalsa_condition_new_and    },
        { "OR ",     3, libbalsa_condition_new_or     }
    };
    gboolean negated;
    unsigned i;

    if(!*string)  /* empty string -> no condition */
        return NULL;
    if(strncmp(*string, "NOT ", 4) == 0) {
        negated = TRUE;
        *string += 4;
    } else negated = FALSE;

    for(i=0; i<G_N_ELEMENTS(cond_types); i++)
        if(strncmp(*string, cond_types[i].key, cond_types[i].keylen) == 0) {
            *string += cond_types[i].keylen;
            return cond_types[i].parser(negated, string);
        }
    return NULL;    
}

static void
cond_to_string(LibBalsaCondition * cond, GString *res)
{
    char str[80];
    GDate date;

    if(cond->negate)
        g_string_append(res, "NOT ");

    switch(cond->type) {
    case CONDITION_STRING:
        g_string_append_printf(res, "STRING %u ", cond->match.string.fields);
        if (CONDITION_CHKMATCH(cond, CONDITION_MATCH_US_HEAD)) {
            append_quoted_string(res, cond->match.string.user_header);
            g_string_append_c(res, ' ');
        }
        append_quoted_string(res, cond->match.string.string);
	break;
    case CONDITION_REGEX:
#if 0
        /* FIXME! */
#endif        
	break;
    case CONDITION_DATE:
        g_string_append(res, "DATE ");
	if (cond->match.date.date_low) {
	    g_date_set_time_t(&date, cond->match.date.date_low);
	    g_date_strftime(str, sizeof(str), "%Y-%m-%d", &date);
	} else str[0]='\0';
        append_quoted_string(res, str);
        g_string_append_c(res, ' ');
	if (cond->match.date.date_high) {
	    g_date_set_time_t(&date, cond->match.date.date_high);
	    g_date_strftime(str, sizeof(str), "%Y-%m-%d", &date);
	} else str[0]='\0';
        append_quoted_string(res, str);
	break;
    case CONDITION_FLAG:
        g_string_append_printf(res, "FLAG %u", cond->match.flags);
        break;
    case CONDITION_AND:
    case CONDITION_OR:
        g_string_append(res, cond->type == CONDITION_OR ? "OR " : "AND ");
        cond_to_string(cond->match.andor.left, res);
        g_string_append_c(res, ' ');
        cond_to_string(cond->match.andor.right, res);
	break;
    case CONDITION_NONE:
        break;
    }
}

gchar*
libbalsa_condition_to_string(LibBalsaCondition *cond)
{
    GString *res;
    g_return_val_if_fail(cond, NULL);

    res = g_string_new("");
    cond_to_string(cond, res);
    return g_string_free(res, FALSE);
}

static void
append_header_names(LibBalsaCondition *cond, GString *res)
{
    static const struct {
        unsigned header;
        const gchar *header_name;
    } header_name_map[] = {
        { CONDITION_MATCH_TO, N_("To") },
        { CONDITION_MATCH_FROM, N_("From") },
        { CONDITION_MATCH_SUBJECT, N_("Subject") },
        { CONDITION_MATCH_CC, N_("CC") },
        { CONDITION_MATCH_BODY, N_("Body") }
    };

    unsigned i;
    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_US_HEAD)) {
        g_string_append_printf(res, _("Header:%s"),
                               cond->match.string.user_header);
    }
    for (i=0; i<G_N_ELEMENTS(header_name_map); ++i) {
        if (CONDITION_CHKMATCH(cond, header_name_map[i].header)) {
            if (res->len>0) {
                g_string_append_printf(res, ",%s",
                                       _(header_name_map[i].header_name));
            } else {
                res = g_string_append(res, _(header_name_map[i].header_name));
            }
        }
    }
}

static void
append_flag_names(LibBalsaCondition *cond, GString *res)
{
    static const struct {
        LibBalsaMessageFlag flag;
        const gchar *flag_name;
    } flag_name_map[] = {
        { LIBBALSA_MESSAGE_FLAG_NEW, N_("New") },
        { LIBBALSA_MESSAGE_FLAG_DELETED, N_("Deleted") },
        { LIBBALSA_MESSAGE_FLAG_REPLIED, N_("Replied") },
        { LIBBALSA_MESSAGE_FLAG_FLAGGED, N_("Flagged") },
    };
    unsigned i;
    gsize len = res->len;
    for (i=0; i<G_N_ELEMENTS(flag_name_map); ++i) {
        if (cond->match.flags & flag_name_map[i].flag) {
            if (res->len == len) {
                res = g_string_append(res, _(flag_name_map[i].flag_name));
            } else {
                g_string_printf(res, ",%s",
                                _(flag_name_map[i].flag_name));
            }
        }
    }
}

gchar*
libbalsa_condition_to_string_user(LibBalsaCondition *cond)
{
    GDate date;
    char str[80];
    GString *res = g_string_new("");

    if(cond->negate)
        g_string_append(res, _("Not "));

    switch(cond->type) {
    case CONDITION_STRING:
        append_header_names(cond, res);
        g_string_append_c(res, ' ');
        append_quoted_string(res, cond->match.string.string);
	break;
    case CONDITION_REGEX:
#if 0
        /* FIXME! */
#endif        
	break;
    case CONDITION_DATE:
	if (cond->match.date.date_low) {
	    g_date_set_time_t(&date, cond->match.date.date_low);
	    g_date_strftime(str, sizeof(str), _("From %Y-%m-%d"), &date);
	} else str[0]='\0';
        append_quoted_string(res, str);
        g_string_append_c(res, ' ');
	if (cond->match.date.date_high) {
	    g_date_set_time_t(&date, cond->match.date.date_high);
	    g_date_strftime(str, sizeof(str), _("To %Y-%m-%d"), &date);
	} else str[0]='\0';
        append_quoted_string(res, str);
	break;
    case CONDITION_FLAG:
        append_flag_names(cond, res);
        break;
    case CONDITION_AND:
        g_string_append(res, _("And"));
        break;
    case CONDITION_OR:
        g_string_append(res, _("Or"));
	break;
    case CONDITION_NONE:
        break;
    }
    return g_string_free(res, FALSE);
}

/*
 * condition_delete_regex()
 *
 * Frees the memory for a filter_regex.
 *
 * Arguments:
 *    condition_regex *reg - the filter_regex structure to clear
 *    gpointer throwaway - unused
 */
void
libbalsa_condition_regex_free(LibBalsaConditionRegex* reg, gpointer throwaway)
{
    if (!reg)
	return;

    g_free(reg->string);
    if (reg->compiled) 
        g_regex_unref(reg->compiled);
}				/* end condition_regex_free() */

void 
regexs_free(GSList * regexs)
{
    if (regexs) {
	g_slist_free_full(regexs, (GDestroyNotify) libbalsa_condition_regex_free);
    }
}                               /* end condition_free_regexs() */

void 
libbalsa_condition_unref(LibBalsaCondition* cond)
{
    if(!cond) /* passing NULL is OK */
        return;

    g_return_if_fail(cond->ref_count > 0);

    if (--cond->ref_count > 0)
        return;

    switch (cond->type) {
    case CONDITION_STRING:
	g_free(cond->match.string.string);
	g_free(cond->match.string.user_header);
	break;
    case CONDITION_REGEX:
	/* FIXME: regexs_free(cond->match.regexs); */
    case CONDITION_DATE:
    case CONDITION_FLAG:
	/* nothing to do */
        break;
    case CONDITION_AND:
    case CONDITION_OR:
        libbalsa_condition_unref(cond->match.andor.left);
        libbalsa_condition_unref(cond->match.andor.right);
        break;
    case CONDITION_NONE:
	/* to avoid warnings */
	break;
    }
    g_free(cond);
}	                       /* end libbalsa_condition_unref() */


/* libbalsa_condition_ref(LibBalsaCondition * cond)
 */
LibBalsaCondition*
libbalsa_condition_ref(LibBalsaCondition* cond)
{
    if(!cond) /* passing NULL is OK */
        return cond;

    g_return_val_if_fail(cond->ref_count > 0, NULL);

    ++cond->ref_count;

    return cond;
}

/* Helper to compare regexs */
#if 0
static gboolean
compare_regexs(GSList * c1,GSList * c2)
{
    GSList *l2 = g_slist_copy(c2);
    LibBalsaConditionRegex * r1,* r2;

    for (;c1 && l2;c1 = g_slist_next(c1)) {
	GSList * tmp;
	r1 = c1->data;
	for (tmp = l2;tmp;tmp = g_slist_next(tmp)) {
	    r2 = tmp->data;
	    if (g_ascii_strcasecmp(r1->string,r2->string)==0) {
		l2 = g_slist_remove(l2, r2);
		break;
	    }
	}
	/* Not found */
	if (!tmp)  break;
    }
    if (!l2 && !c1)
	return TRUE;
    g_slist_free(l2);
    return FALSE;
}
#endif
/* Helper to compare conditions, a bit obscure at first glance
   but we have to compare complex structure, so we must check
   all fields.
   NULL conditions are OK, and compare equal only if both are NULL.
*/

static gboolean
lbcond_compare_string_conditions(LibBalsaCondition * c1,
                                 LibBalsaCondition * c2)
{
    if (c1->match.string.fields != c2->match.string.fields
        || (CONDITION_CHKMATCH(c1, CONDITION_MATCH_US_HEAD)
            && g_ascii_strcasecmp(c1->match.string.user_header,
                                  c2->match.string.user_header)))
        return FALSE;

    return (g_ascii_strcasecmp(c1->match.string.string,
                               c2->match.string.string) == 0);
}

gboolean
libbalsa_condition_compare(LibBalsaCondition *c1,LibBalsaCondition *c2)
{
    gboolean res = FALSE;

    if (c1 == c2) 
        return TRUE;

    if (c1 == NULL || c2 == NULL
        || c1->type != c2->type || c1->negate != c2->negate)
        return FALSE;

    switch (c1->type) {
    case CONDITION_STRING:
        res = lbcond_compare_string_conditions(c1, c2);
        break;
    case CONDITION_REGEX:
#if 0
        if ((c2->type == CONDITION_REGEX) && FIXME!
            compare_regexs(c1->match.regexs,c2->match.regexs))
            OK = TRUE;
#endif
        break;
    case CONDITION_DATE:
        res = (c1->match.date.date_low == c2->match.date.date_low &&
               c1->match.date.date_high == c2->match.date.date_high);
        break;
    case CONDITION_FLAG:
        res = (c1->match.flags == c2->match.flags);
        break;
    case CONDITION_AND:
    case CONDITION_OR:
        /* We could declare c1 and c2 equal if (c1->left == c2->right)
         * && (c1->right == c2->left), but we don't; the boolean value
         * would be the same, but c1 and c2 could have different side
         * effects. */
        res = (libbalsa_condition_compare(c1->match.andor.left,
                                          c2->match.andor.left) &&
               libbalsa_condition_compare(c1->match.andor.right,
                                          c2->match.andor.right));
        break;
    case CONDITION_NONE:
        break;
    }
    return res;
}

/* BIG FIXME : result of certain function of regex compilation are useless
 * and we should have a way to tell which regexs we were unable to compile
 * that's for later
 */
/*
 * condition_regcomp()
 *
 * Compiles a regex for a filter (only if compiled field is NULL)
 *
 * Arguments:
 *    condition_regex *cre - the condition_regex struct to compile
 * Returns : TRUE if compilation went well, FALSE else
 * Position filter_errno
 */
#if 0
static gboolean 
condition_regcomp(LibBalsaConditionRegex* cre)
{
    gint rc;

    filter_errno = FILTER_NOERR;
    if (cre->compiled) return TRUE;

    cre->compiled = g_new(regex_t,1);
    rc = regcomp(cre->compiled, cre->string, FILTER_REGCOMP);

    if (rc != 0) {
	gchar errorstring[256];
	regerror(rc, cre->compiled, errorstring, 256);
	filter_errno = FILTER_EREGSYN;
	return FALSE;
    }
    return TRUE;
}				/* end condition_regcomp() */

/*
 * condition_compile_regexs
 *
 * Compiles all the regexs a condition has (if of type CONDITION_REGEX)
 *
 * Arguments:
 *    condition * cond - the condition to compile
 *
 * Position filter_errno (by calling condition_regcomp)
 */
void
libbalsa_condition_compile_regexs(LibBalsaCondition* cond)
{
    GSList * regex;

    if (cond->type==CONDITION_REGEX)
	for(regex=cond->match.regexs;
            regex && condition_regcomp((LibBalsaConditionRegex*)regex->data);
            regex=g_slist_next(regex));
}                       /* end of condition_compile_regexs */
#endif
/* Filters */

/*
 * filter_prepend_condition()
 *
 * Appends a new condition to the filter
 * Position flags according to condition type
 */

static void 
filter_condition_validity(LibBalsaFilter* fil, LibBalsaCondition* cond)
{
#if 0
    /* Test validity of condition */
    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_US_HEAD) && 
	(!cond->user_header || cond->user_header[0]=='\0')) {
	FILTER_CLRFLAG(fil,FILTER_VALID);
	return;
    }
    switch (cond->type) {
    case CONDITION_STRING:
	if (!cond->match.string)
	    FILTER_CLRFLAG(fil,FILTER_VALID);
	break;
    case CONDITION_REGEX:
	if (!cond->match.regexs)
	    FILTER_CLRFLAG(fil,FILTER_VALID);
	FILTER_CLRFLAG(fil,FILTER_COMPILED);
	break;
    case CONDITION_FLAG:
    case CONDITION_DATE:
    case CONDITION_NONE:
	break;
    }
#endif
}

void
libbalsa_filter_prepend_condition(LibBalsaFilter* fil, LibBalsaCondition* cond,
                                  ConditionMatchType op)
{
    LibBalsaCondition *res;

    filter_condition_validity(fil,cond);

    res = libbalsa_condition_new_bool_ptr(FALSE, op, cond, fil->condition);
    libbalsa_condition_unref(cond);
    libbalsa_condition_unref(fil->condition);
    fil->condition = res;
}

/*
 * libbalsa_filter_compile_regexs
 *
 * Compiles all the regexs a filter has
 *
 * Arguments:
 *    filter *fil - the filter to compile
 *
 * Returns:
 *    gboolean - TRUE for success, FALSE otherwise.
 */
gboolean
libbalsa_filter_compile_regexs(LibBalsaFilter* fil)
{
#if 0
    FIXME;
    filter_errno = FILTER_NOERR;

    if (fil->cond_tree.conditions) {
	GSList * lst;
	for (lst = fil->cond_tree.conditions;
             lst && filter_errno == FILTER_NOERR;
             lst = g_slist_next(lst))
	    libbalsa_condition_compile_regexs((LibBalsaCondition*) lst->data);
	if (filter_errno != FILTER_NOERR) {
	    gchar * errorstring =
                g_strdup_printf("Unable to compile filter %s", fil->name);
	    filter_perror(errorstring);
	    g_free(errorstring);
	    FILTER_CLRFLAG(fil, FILTER_VALID);
	    return FALSE;
	}
	
    }
    FILTER_SETFLAG(fil, FILTER_COMPILED);
#endif
    return TRUE;
}                       /* end of filter_compile_regexs */

/*
 * libbalsa_filter_free()
 *
 * Frees the memory for a filter
 *
 * Arguments:
 *    filter *fil - the filter to delete
 *    gpointer free_conditions - <>0 => free conditions also
 */
void
libbalsa_filter_free(LibBalsaFilter* fil, gpointer free_conditions)
{
    if (!fil)
	return;

    g_free(fil->name);
    g_free(fil->sound);
    g_free(fil->popup_text);
    g_free(fil->action_string);

    if (GPOINTER_TO_INT(free_conditions)) 
        libbalsa_condition_unref(fil->condition);

    g_free(fil);
}				/* end filter_free() */


/*
 * filter_clear_filters
 *
 * Clears the entire filter list
 *
 * Arguments:
 *    GSList *filter_list - the filter list to clear
 *    gboolean free_conditions : if TRUE filter conditions are also freed
 */
void
libbalsa_filter_clear_filters(GSList* filter_list,gint free_conditions)
{
    if (!filter_list)
	return;

    g_slist_foreach(filter_list, (GFunc) libbalsa_filter_free, 
                    GINT_TO_POINTER(free_conditions));
    g_slist_free(filter_list);
}				/* end filter_clear_filters() */


/*
 * filter_new()
 *
 * Allocates a new filter, zeros it, and returns a pointer
 * (convienience wrapper around g_malloc())
 *
 * Returns:
 *    filter* - pointer to the new filter
 */
LibBalsaFilter *
libbalsa_filter_new(void)
{
    LibBalsaFilter *newfil;

    newfil = g_new(LibBalsaFilter,1);

    newfil->name=NULL;
    newfil->flags     = FILTER_EMPTY; /* In particular filter is INVALID */
    newfil->condition = NULL;
    newfil->sound     = NULL;
    newfil->popup_text = NULL;
    newfil->action    = FILTER_NOTHING;
    newfil->action_string = NULL;

    filter_errno=FILTER_NOERR;
    return (newfil);
}				/* end filter_new() */

#if 0
static GString*
match_field_decode(LibBalsaCondition* cnd, GString *buffer,gchar *str_format)
{
    GString * str=g_string_new("[");
    gboolean coma=FALSE;

    /* FIXME : what to do with body match ? */

    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_TO)) {
	str=g_string_append(str,"\"To\"");
	coma=TRUE;
    }
    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_FROM)) {
	str=g_string_append(str,coma ? ",\"From\"" : "\"From\"");
	coma=TRUE;
    }
    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_CC)) {
	str=g_string_append(str,coma ? ",\"Cc\"" : "\"Cc\"");
	coma=TRUE;
    }
    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_SUBJECT)) {
	str=g_string_append(str,coma ? ",\"Subject\"" : "\"Subject\"");
	coma=TRUE;
    }
    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_US_HEAD) &&
        cnd->match.string.user_header) {
	if (coma)
	    str=g_string_append_c(str,',');
	str=g_string_append_c(str,'\"');
	str=g_string_append(str,cnd->match.string.user_header);
	str=g_string_append_c(str,'\"');
    }
    g_string_append(str,"] ");
    if (str->len>3) {
	gchar * temp=g_strdup_printf(str_format,"header",str->str);
	buffer=g_string_append(buffer,temp);
	g_free(temp);
    }
    g_string_free(str,TRUE);
    return buffer;
}

static GString*
export_condition(LibBalsaCondition* cnd, GString * buffer)
{
    gchar * str;

    if (cnd->negate)
	buffer=g_string_append(buffer,"not ");

    switch (cnd->type) {
    case CONDITION_STRING:
	str=g_strconcat("%s :contains %s ","\"",cnd->match.string.string,
                        "\"",NULL);
	buffer=match_field_decode(cnd,buffer,str);
	g_free(str);
	break;
    case CONDITION_REGEX:
        /* FIXME: do not generate non-conformant sieve script */
	if (cnd->match.regexs->next) {
	    buffer=g_string_append(buffer,"ANYOF(");
	    parent=TRUE;
	}
	else parent=FALSE;
	for (lst=cnd->match.regexs;lst;) {
	    /* This is not in RFC 3028, consider it as an extension :) */
	    str=g_strconcat("%s :regex %s ",
                            ((LibBalsaConditionRegex*)lst->data)->string,NULL);
	    buffer=match_field_decode(cnd, buffer, str);
	    g_free(str);
	    lst=g_slist_next(lst);
	    if (lst)
		buffer=g_string_append_c(buffer,',');
	}
	if (parent) buffer = g_string_append_c(buffer,')');
	break;
    case CONDITION_FLAG:
	/* FIXME */
	buffer=g_string_append(buffer,"TRUE");
    case CONDITION_DATE:
	/*FIXME */
    case CONDITION_NONE:
    case CONDITION_AND:
    case CONDITION_OR:
        g_warning("%s: FIXME!\n", __func__);
	/* Should not occur */
	buffer=g_string_append(buffer,"TRUE");
    }
    return buffer;
}
#endif

gboolean
libbalsa_filter_export_sieve(LibBalsaFilter* fil, gchar* filename)
{
    FILE * fp;
#if 0
    GString * buffer;
#endif
    gint nb = 0;

    fp=fopen(filename,"w");
    if (!fp) return FALSE;
#if 0
    buffer=g_string_new("# Sieve script automatically generated by Balsa "
                        "(Exporting a Balsa filter)\n");
#endif
    g_warning("%s: FIXME!\n", __func__);
#if 0
    
    if (fil->condition) {
	buffer=g_string_append(buffer,"IF ");
	if (fil->cond_tree.conditions->next) {
	    if (fil->cond_tree.op==FILTER_OP_OR)
		buffer=g_string_append(buffer,"ANYOF(");    
	    else buffer=g_string_append(buffer,"ALLOF(");
	    parent=TRUE;
	}
	else parent=FALSE;
	for (conds=fil->cond_tree.conditions; conds;) {
	    LibBalsaCondition* cnd=(LibBalsaCondition*)conds->data;
	    buffer=export_condition(cnd,buffer);
	    conds=g_slist_next(conds);
	    if (conds)
		buffer=g_string_append(buffer,",\n");
	}
	if (parent) 
	    buffer=g_string_append(buffer,")\n{\n");
	else
	    buffer=g_string_append(buffer,"\n{\n");
	switch (fil->action) {
	case FILTER_COPY:
	    /* FIXME : I translate COPY to a keep;fileinto sequence, don't know if it's OK */
	    buffer=g_string_append(buffer,"keep;\n");
	case FILTER_MOVE:
	    temp=g_strconcat("fileinto \"",fil->action_string,"\";\n",NULL);
	    buffer=g_string_append(buffer,temp);
	    g_free(temp);
	    break;
	case FILTER_TRASH:
	    buffer=g_string_append(buffer,"discard;\n");
	    break;
	    /* FIXME how to code other actions */
        case FILTER_NOTHING: break;
        case FILTER_PRINT:   break;
        case FILTER_RUN:     break;
	}
	buffer=g_string_append(buffer,"}\n");
    }
    nb=fwrite(buffer->str,buffer->len,1,fp);
    g_string_free(buffer,TRUE);
#endif
    if (fclose(fp)!=0) nb=0;
    return nb==1 ? TRUE : FALSE;
}             /* end of filter_export_sieve */

/*
 * compare_filters
 *
 * callback for sorting a GtkTreeView
 */
static gint
compare_filters(GtkTreeModel * model, GtkTreeIter * a, GtkTreeIter * b,
                gpointer user_data)
{
    gchar *stra, *strb;
    gint ret_val;

    gtk_tree_model_get(model, a, 0, &stra, -1);
    gtk_tree_model_get(model, b, 0, &strb, -1);

    ret_val = g_ascii_strcasecmp(stra, strb);

    g_free(stra);
    g_free(strb);

    return ret_val;
}

/*
 * libbalsa_filter_list_new
 *
 * create a GtkTreeView
 *
 * with_data            does the underlying store need a data column?
 * title                for the string column--if NULL, the header is
 *                      invisible
 * mode                 selection mode
 * selection_changed_cb callback for the "changed" signal of the
 *                      associated GtkTreeSelection
 * sorted               does the list need to be sorted?
 */
GtkTreeView *
libbalsa_filter_list_new(gboolean with_data, const gchar * title,
                         GtkSelectionMode mode,
                         GCallback selection_changed_cb, gboolean sorted)
{
    GtkListStore *list_store;
    GtkTreeView *view;
    GtkTreeSelection *selection;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    list_store = with_data ?
        gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER) :
        gtk_list_store_new(1, G_TYPE_STRING);
    view = GTK_TREE_VIEW(gtk_tree_view_new_with_model
                         (GTK_TREE_MODEL(list_store)));
    g_object_unref(list_store);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(title, renderer, "text",
                                                 0, NULL);
    gtk_tree_view_append_column(view, column);
    if (!title)
        gtk_tree_view_set_headers_visible(view, FALSE);

    selection = gtk_tree_view_get_selection(view);
    gtk_tree_selection_set_mode(selection, mode);
    if (selection_changed_cb)
        g_signal_connect(selection, "changed",
                         selection_changed_cb, NULL);

    if (sorted) {
        gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(list_store), 0,
                                        compare_filters, NULL, NULL);
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_store),
                                             0, GTK_SORT_ASCENDING);
    }

    return view;
}                               /* end of libbalsa_filter_list_new */
