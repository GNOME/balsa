#ifndef __IMAP_UTIL_H__
#define __IMAP_UTIL_H__ 1

void imap_quote_string(char *dest, size_t dlen, const char *src);
void imap_unquote_string(char *s);
char* imap_next_word(char *s);
char* imap_skip_atom(char *s);

void lit_conv_to_base64(unsigned char *out, const unsigned char *in, 
                        size_t len, size_t olen);
int lit_conv_from_base64(char *out, const char *in);

#endif
