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
 * filter-funcs.c
 *
 * Functions for filters
 */

#include "filter-funcs.h"

/* Conditions */

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
	regfree(reg->compiled);
}				/* end condition_regex_free() */

void 
regexs_free(GSList * regexs)
{
    if (regexs) {
	g_slist_foreach(regexs, (GFunc) libbalsa_condition_regex_free, NULL);
	g_slist_free(regexs);
    }
}                               /* end condition_free_regexs() */

void 
libbalsa_condition_free(LibBalsaCondition* cond)
{
    switch (cond->type) {
    case CONDITION_SIMPLE:
	g_free(cond->match.string);
	break;
    case CONDITION_REGEX:
	regexs_free(cond->match.regexs);
    case CONDITION_DATE:
    case CONDITION_FLAG:
	/* nothing to do */
    case CONDITION_NONE:
	/* to avoid warnings */
	break;
    }
    if (cond->user_header)
	g_free(cond->user_header);
    g_free(cond);
}	                       /* end libbalsa_condition_free() */

void 
libbalsa_conditions_free(GSList * cnds)
{
    if (cnds) {
	g_slist_foreach(cnds, (GFunc) libbalsa_condition_free, NULL);
	g_slist_free(cnds);
    }
}                              /* end libbalsa_conditions_free() */

LibBalsaCondition*
libbalsa_condition_new(void)
{
    LibBalsaCondition *newc;

    newc = g_new(LibBalsaCondition,1);

    newc->type = CONDITION_NONE;
    newc->match_fields = CONDITION_EMPTY;
    newc->condition_not = FALSE;
    newc->match.string = NULL;
    newc->user_header = NULL;
    filter_errno=FILTER_NOERR;

    return newc;
}                      /* end libbalsa_condition_new() */

LibBalsaConditionRegex*
libbalsa_condition_regex_new(void)
{
    LibBalsaConditionRegex * new_reg;

    new_reg = g_new(LibBalsaConditionRegex,1);
    new_reg->string=NULL;
    new_reg->compiled=NULL;
    filter_errno=FILTER_NOERR;

    return new_reg;
}

/* libbalsa_condition_clone(LibBalsaCondition * cnd)
 * Position filter_errno
 */
LibBalsaCondition*
libbalsa_condition_clone(LibBalsaCondition* cnd)
{
    GSList * regex;
    LibBalsaCondition * new_cnd;
    LibBalsaConditionRegex* new_reg;

    filter_errno = FILTER_NOERR;
    new_cnd = libbalsa_condition_new();

    new_cnd->condition_not = cnd->condition_not;
    new_cnd->match_fields  = cnd->match_fields;
    new_cnd->type          = cnd->type;
    new_cnd->user_header   = g_strdup(cnd->user_header);
    switch (new_cnd->type) {
    case CONDITION_SIMPLE:
        new_cnd->match.string=g_strdup(cnd->match.string);
        break;
    case CONDITION_REGEX:
        for (regex=cnd->match.regexs;regex && (filter_errno==FILTER_NOERR);
             regex=g_slist_next(regex)) {
            new_reg = libbalsa_condition_regex_new();
            new_reg->string = 
                g_strdup(((LibBalsaConditionRegex*)regex->data)->string);
            new_cnd->match.regexs = 
                g_slist_prepend(new_cnd->match.regexs, new_reg);
        }
        new_cnd->match.regexs = g_slist_reverse(new_cnd->match.regexs);
        break;
    case CONDITION_DATE:
        new_cnd->match.interval.date_low  = cnd->match.interval.date_low;
        new_cnd->match.interval.date_high = cnd->match.interval.date_high;
        break;
    case CONDITION_FLAG:
        new_cnd->match.flags=cnd->match.flags;
    case CONDITION_NONE:
        /* to avoid warnings */
        break;
    default:
        g_assert_not_reached();
    }

    return new_cnd;
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

/* Filters */

/*
 * filter_append_condition()
 *
 * Appends a new condition to the filter
 * Position flags according to condition type
 */

static void 
filter_condition_validity(LibBalsaFilter* fil, LibBalsaCondition* cond)
{
    /* Test validity of condition */
    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_US_HEAD) && (!cond->user_header || cond->user_header[0]=='\0'))
	FILTER_CLRFLAG(fil,FILTER_VALID);
    switch (cond->type) {
    case CONDITION_SIMPLE:
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
}

void
libbalsa_filter_append_condition(LibBalsaFilter* fil, LibBalsaCondition* cond)
{
    filter_condition_validity(fil,cond);
    fil->conditions=g_slist_append(fil->conditions,cond);
}

void
libbalsa_filter_prepend_condition(LibBalsaFilter* fil, LibBalsaCondition* cond)
{
    filter_condition_validity(fil,cond);
    fil->conditions=g_slist_prepend(fil->conditions,cond);
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
    filter_errno = FILTER_NOERR;

    if (fil->conditions) {
	GSList * lst;
	for (lst=fil->conditions;lst && filter_errno==FILTER_NOERR;lst=g_slist_next(lst))
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
        libbalsa_conditions_free(fil->conditions);

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
    newfil->flags = FILTER_EMPTY;    /* In particular filter is INVALID */
    newfil->conditions = NULL;
    newfil->sound = NULL;
    newfil->popup_text = NULL;
    newfil->action = FILTER_NOTHING;
    newfil->action_string = NULL;
    newfil->matching_messages=NULL;

    filter_errno=FILTER_NOERR;
    return (newfil);
}				/* end filter_new() */

static GString*
match_field_decode(LibBalsaCondition* cnd,GString * buffer,gchar * str_format)
{
    GString * str=g_string_new("[");
    gboolean coma=FALSE;

    /* FIXME : what to do with body match ? */

    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_TO)) {
	str=g_string_append(str,"\"To\"");
	coma=TRUE;
    }
    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_FROM)) {
	if (coma)
	    str=g_string_append(str,",\"From\"");
	else 
	    str=g_string_append(str,"\"From\"");
	coma=TRUE;
    }
    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_CC)) {
	if (coma)
	    str=g_string_append(str,",\"Cc\"");
	else 
	    str=g_string_append(str,"\"Cc\"");
	coma=TRUE;
    }
    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_SUBJECT)) {
	if (coma)
	    str=g_string_append(str,",\"Subject\"");
	else 
	    str=g_string_append(str,"\"Subject\"");
	coma=TRUE;
    }
    /* FIXME : see how to export conditions matching user headers */
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
    GSList * lst;
    gchar * str;
    gboolean parent;

    if (cnd->condition_not)
	buffer=g_string_append(buffer,"not ");

    switch (cnd->type) {
    case CONDITION_SIMPLE:
	str=g_strconcat("%s :contains %s ","\"",cnd->match.string,"\"",NULL);
	buffer=match_field_decode(cnd,buffer,str);
	g_free(str);
	break;
    case CONDITION_REGEX:
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
	/* Should not occur */
	buffer=g_string_append(buffer,"TRUE");
    }
    return buffer;
}

gboolean
libbalsa_filter_export_sieve(LibBalsaFilter* fil, gchar* filename)
{
    FILE * fp;
    GString * buffer;
    gint nb;
    GSList * conds;
    gchar * temp;
    gboolean parent;

    fp=fopen(filename,"w");
    if (!fp) return FALSE;
    buffer=g_string_new("# Sieve script automatically generated by Balsa (Exporting a Balsa filter)\n");
    if (fil->conditions) {
	buffer=g_string_append(buffer,"IF ");
	if (fil->conditions->next) {
	    if (fil->conditions_op==FILTER_OP_OR)
		buffer=g_string_append(buffer,"ANYOF(");    
	    else buffer=g_string_append(buffer,"ALLOF(");
	    parent=TRUE;
	}
	else parent=FALSE;
	for (conds=fil->conditions;conds;) {
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
        case FILTER_NOTHING:
        case FILTER_PRINT:
        case FILTER_RUN:
	}
	buffer=g_string_append(buffer,"}\n");
    }
    nb=fwrite(buffer->str,buffer->len,1,fp);
    g_string_free(buffer,TRUE);
    if (fclose(fp)!=0) nb=0;
    return nb==1 ? TRUE : FALSE;
}             /* end of filter_export_sieve */
