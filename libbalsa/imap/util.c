/* libimap library.
 * Copyright (C) 2003-2016 Pawel Salek.
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
/* imap_quote_string: quote string according to IMAP rules:
 *   surround string with quotes, escape " and \ with \ */

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "util.h"

#define SKIPWS(c) while (*(c) && isspace ((unsigned char) *(c))) c++;

gchar *
imap_quote_string(const gchar *src)
{
	g_return_val_if_fail(src != NULL, NULL);

	if ((strchr(src, '"') == NULL) && (strchr(src, '\\') == NULL)) {
		/* no need to quote... */
		return g_strdup(src);
	} else {
		GString *buf;

		buf = g_string_sized_new(strlen(src + 3U));
		g_string_append_c(buf, '"');
		for (; *src != '\0'; src++) {
			if ((*src == '"') || (*src == '\\')) {
				g_string_append_c(buf, '\\');
			}
			g_string_append_c(buf, *src);
		}
		g_string_append_c(buf, '"');
		return g_string_free(buf, FALSE);
	}
}

/* imap_skip_atom: return index into string where next IMAP atom begins.
 * see RFC for a definition of the atom. */
#define LIST_SPECIALS   "%*"
#define QUOTED_SPECIALS "\"\\"
#define RESP_SPECIALS   "]"
char*
imap_skip_atom(char *s)
{
  static const char ATOM_SPECIALS[] = 
    "(){ " LIST_SPECIALS QUOTED_SPECIALS RESP_SPECIALS;
  while (*s && !strchr(ATOM_SPECIALS,*s))
      s++;
  SKIPWS (s);
  return s;
}

char*
imap_next_word(char *s)
{
  int quoted = 0;

  while (*s) {
    if (*s == '\\') {
      s++;
      if (*s)
        s++;
      continue;
    }
    if (*s == '\"')
      quoted = quoted ? 0 : 1;
    if (!quoted && isspace(*s))
      break;
    s++;
  }

  SKIPWS (s);
  return s;
}
  

/* ===================================================================
 * UTF-7 conversion routines as in RFC 2192
 * =================================================================== */

/* see RFC 3501, Section 5.1.3. Mailbox International Naming Convention:
 * In modified UTF-7, printable US-ASCII characters, except for "&", represent themselves; that is, characters with octet values
 * 0x20-0x25 and 0x27-0x7e. */
#define IS_VALID_ASCII(c)	((((c) >= '\x20') && ((c) <= '\x25')) || (((c) >= '\x27') && ((c) <= '\x7e')))

gchar *
imap_utf8_to_mailbox(const gchar *mbox)
{
	GString *buffer;
	const gchar *next_in;

	buffer = g_string_sized_new(strlen(mbox));	/* sufficient size for ASCII only */
	next_in = mbox;
	while (*next_in != '\0') {
		if (IS_VALID_ASCII(*next_in)) {
			g_string_append_c(buffer, *next_in++);
		} else if (*next_in == '&') {
			g_string_append(buffer, "&-");		/* see RFC 3501, Section 5.1.3 */
			next_in++;
		} else {
			const gchar *next_ascii;
			gchar *utf7;
			gsize utf7len;

			next_ascii = g_utf8_next_char(next_in);
			while ((*next_ascii != '\0') && !IS_VALID_ASCII(*next_ascii)) {
				 next_ascii = g_utf8_next_char(next_ascii);
			}
			utf7 = g_convert(next_in, next_ascii - next_in, "utf7", "utf8", NULL, &utf7len, NULL);
			if (utf7 != NULL) {
				gsize n;
				utf7[0] = '&';					/* see RFC 3501, Section 5.1.3 */

				for (n = 1U; n < utf7len; n++) {
					if (utf7[n] == '/') {		/* see RFC 3501, Section 5.1.3 */
						utf7[n] = ',';
					}
				}
				g_string_append_len(buffer, utf7, utf7len);
				g_free(utf7);
			}
			next_in = next_ascii;
		}
	}

	return g_string_free(buffer, FALSE);
}

gchar *
imap_mailbox_to_utf8(const gchar *mbox)
{
	GString *buffer;
	const gchar *next_in;

	buffer = g_string_sized_new(strlen(mbox));		/* always sufficiently long */
	next_in = mbox;
	while (*next_in != '\0') {
		if (*next_in == '&') {
			if (next_in[1] == '-') {				/* see RFC 3501, Section 5.1.3 */
				g_string_append_c(buffer, '&');
				next_in = &next_in[2];
			} else {
				gchar *utf7buf;
				gchar *next_utf7;
				gchar *utf8;
				gsize utf8len;

				utf7buf = g_malloc0(strlen(next_in) + 1U);
				utf7buf[0] = '+';					/* RFC 2152 shift character */
				next_in++;
				for (next_utf7 = &utf7buf[1]; (*next_in != '\0') && (*next_in != '-'); next_in++) {
					if (*next_in == ',') {			/* see RFC 3501, Section 5.1.3 */
						*next_utf7++ = '/';
					} else {
						*next_utf7++ = *next_in;
					}
				}
				*next_utf7 = *next_in;
				if (*next_in == '-') {
					next_in++;
				}
				utf8 = g_convert(utf7buf, -1, "utf8", "utf7", NULL, &utf8len, NULL);
				if (utf8 != NULL) {
					g_string_append_len(buffer, utf8, utf8len);
					g_free(utf8);
				}
				g_free(utf7buf);
			}
		} else {
			g_string_append_c(buffer, *next_in++);
		}
	}

	return g_string_free(buffer, FALSE);
}

#if 0
int main(int argc, char *argv[])
{
  int i;
  for(i=1; i<argc; i++) {
    char *mbx = imap_utf8_to_mailbox(argv[i]);
    char *utf8 = imap_mailbox_to_utf8(mbx);
    if (!mbx || !utf8)
      continue;
    printf("orig='%s' mbx='%s' back='%s'\n", argv[i], mbx, utf8);
    free(mbx); free(utf8);
  }
  return 0;
}
#endif
