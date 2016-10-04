#ifndef __IMAP_COMPRESS_H__
#define __IMAP_COMPRESS_H__ 1
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <zlib.h>

#include "imap-handle.h"

struct ImapCompressContext {
  z_stream out_stream;
  z_stream in_stream;
  char *in_buffer;
  char *out_buffer;
  unsigned long in_uncompressed, in_compressed;
  unsigned long out_uncompressed, out_compressed;
};

ImapResponse imap_compress(ImapMboxHandle* h);

void imap_compress_init(struct ImapCompressContext *buf);
void imap_compress_release(struct ImapCompressContext *buf);

#endif /* __IMAP_COMPRESS_H__ */
