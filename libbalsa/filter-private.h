/* -*- C -*-
 * filter-private.h
 *
 * private filter defninitions
 */

#ifndef _FILTER_PRIVATE_H
#define _FILTER_PRIVATE_H

/* regex options */
#define FILTER_REGCOMP       (REG_NEWLINE | REG_NOSUB | REG_EXTENDED)
#define FILTER_REGEXEC       0


/* filter flags */
#define FILTER_EMPTY         0        /* for clearing bitfields */
#define FILTER_BUILT         1<<0        /* options have been turned into
                                           regex strings */
#define FILTER_COMPILED      1<<1        /* regex strings have been compiled
                                           with regcomp() */
#define FILTER_MODIFIED      1<<2        /* the filter has been modified and
                                           the regex's are no longer valid */
#define FILTER_ENABLED       1<<3        /* the filter is enabled
                                           a filter can be disabled because
                                           of user selection or an error in the
                                           regex */
#define FILTER_SOUND         1<<4        /* play a sound when matches */
#define FILTER_POPUP         1<<5        /* popup text when matches */
#define FILTER_PLACE         1<<6        /* "Place" disposition */
#define FILTER_NOPLACE       1<<7        /* "Do not Place" disposition */
#define FILTER_STOP          1<<8        /* "Stop" disposition */

/* flag operation macros */
#define FILTER_SETFLAG(x, y) ((((filter*)(x))->flags) |= (y))
#define FILTER_CLRFLAG(x, y) ((((filter*)(x))->flags) &= ~(y))
#define FILTER_CHKFLAG(x, y) ((((filter*)(x))->flags) & (y))

/* FILTER_SIMPLE match flags */
#define FILTER_MATCH_ALL     1<<0        /* match entire message */
#define FILTER_MATCH_HEADER  1<<1        /* match in the header */
#define FILTER_MATCH_BODY    1<<2        /* match in the body */
#define FILTER_MATCH_TO      1<<3        /* match in the To: field */
#define FILTER_MATCH_FROM    1<<4        /* match in the From: field */
#define FILTER_MATCH_SUBJECT 1<<5        /* match in the Subject field */

/* FILTER_SIMPLE macros */
#define FILTER_SETMATCH(x, y) ((((filter*)(x))->matchfields) |= (y))
#define FILTER_CLRMATCH(x, y) ((((filter*)(x))->matchfields) &= ~(y))
#define FILTER_CHKMATCH(x, y) ((((filter*)(x))->matchfields) & (y))

/* regex struct */
typedef struct _filter_regex
  {
    gchar *string;
    regex_t *compiled;
  }
filter_regex;


#endif /* _FILTER_PRIVATE_H */
