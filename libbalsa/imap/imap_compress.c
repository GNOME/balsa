/* libimap library.
 * Copyright (C) 2003-2010 Pawel Salek.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "siobuf.h"
#include "imap-handle.h"
#include "imap_private.h"

/** Arbitrary. Current choice is as good as any other. */
static const unsigned IMAP_COMPRESS_BUFFER_SIZE = 8192;

static int
imap_compress_cb(char **dstbuf, int *dstlen,
                 const char *srcbuf, int srclen, void *arg)
{
  struct ImapCompressContext *icb = &((ImapMboxHandle*)arg)->compress;
  int err;

  *dstbuf = icb->out_buffer;

  if (*dstlen == 0) { /* first call */
    icb->out_stream.next_in  = (Bytef*)srcbuf;
    icb->out_stream.avail_in = srclen;
    icb->out_uncompressed += srclen;
  }

  icb->out_stream.next_out = (Byte*)*dstbuf;
  icb->out_stream.avail_out = IMAP_COMPRESS_BUFFER_SIZE;

  err = deflate(&icb->out_stream, srclen ? Z_NO_FLUSH : Z_SYNC_FLUSH);
  if ( !(err == Z_OK || err == Z_STREAM_END || err == Z_BUF_ERROR) ) {
    fprintf(stderr, "deflate error1 %d\n", err);
    *dstlen = -1;
  } else {
    *dstlen = IMAP_COMPRESS_BUFFER_SIZE - icb->out_stream.avail_out;
    /* printf("imap_compress_cb %d bytes to %d\n", srclen, *dstlen); */
    icb->out_compressed += *dstlen;
  }

  return *dstlen;
}

static int
imap_decompress_cb(char **dstbuf, int *dstlen,
                   const char *srcbuf, int srclen, void *arg)
{
  struct ImapCompressContext *icb = &((ImapMboxHandle*)arg)->compress;
  int err;

  *dstbuf = icb->in_buffer;

  if (srclen) {
    icb->in_stream.next_in  = (Bytef*)srcbuf;
    icb->in_stream.avail_in = srclen;
    icb->in_compressed += srclen;
  }

  icb->in_stream.next_out = (Byte*)*dstbuf;
  icb->in_stream.avail_out =  IMAP_COMPRESS_BUFFER_SIZE;
  err = inflate(&icb->in_stream, Z_SYNC_FLUSH);
  
  if (!(err == Z_OK || err == Z_BUF_ERROR || err == Z_STREAM_END)) {
    fprintf(stderr, "inflate error %d\n", err);
    *dstlen = -1;
  } else {
    *dstlen = IMAP_COMPRESS_BUFFER_SIZE - icb->in_stream.avail_out;
    /* printf("imap_decompress_cb %d bytes to %d\n", srclen, *dstlen); */
    icb->in_uncompressed += *dstlen;
  }
  return *dstlen;
}

/** Enables COMPRESS extension if available. Assumes that the handle
    is already locked. */
ImapResponse
imap_compress(ImapMboxHandle *handle)
{
  struct ImapCompressContext *icb;
  int err;

  if (!handle->enable_compress ||
      !imap_mbox_handle_can_do(handle, IMCAP_COMPRESS_DEFLATE))
    return IMR_NO;

  if (imap_cmd_exec(handle, "COMPRESS DEFLATE") != IMR_OK)
    return IMR_NO;

  icb = &handle->compress;
  icb->in_buffer = malloc(IMAP_COMPRESS_BUFFER_SIZE);
  icb->out_buffer = malloc(IMAP_COMPRESS_BUFFER_SIZE);

  if (!icb->in_buffer || !icb->out_buffer)
    return IMR_NO;

  /* Compression enabled. Everything that we send and receive now must
     go through compression/decompression routines enabled in sio. */

  icb->out_stream.zalloc = Z_NULL;
  icb->out_stream.zfree = Z_NULL;
  icb->out_stream.opaque = (voidpf)0;

  /* amazingly enough, deflateInit() won't do */
  err = deflateInit2(&icb->out_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     -15, 8, Z_DEFAULT_STRATEGY);
  if (err != Z_OK) {
    fprintf(stderr, "deflateInit error: %d\n", err);
    return IMR_NO;
  }

  icb->in_stream.next_in = Z_NULL;
  icb->in_stream.avail_in = 0;
  icb->in_stream.zalloc = Z_NULL;
  icb->in_stream.zfree = Z_NULL;
  icb->in_stream.opaque = (voidpf)0;

  /* amazingly enough, inflateInit() won't do */
  err = inflateInit2(&icb->in_stream, -15);
  if (err != Z_OK) {
    fprintf(stderr, "inflateInit error: %d\n", err);
    return IMR_NO;
  }

  if (handle->sio) {
    sio_set_securitycb(handle->sio, imap_compress_cb, imap_decompress_cb,
                       handle);
    return IMR_OK;
  } else {
    fprintf(stderr, "SIO not set!\n");
    return IMR_NO;
  }
}

void
imap_compress_init(struct ImapCompressContext *buf)
{
  memset(buf, 0, sizeof(buf));
}

/** releases any data that might have been allocated by compression routines. */

void
imap_compress_release(struct ImapCompressContext *buf)
{

  if (buf->in_buffer) {
    inflateEnd(&buf->in_stream);
    free(buf->in_buffer);
  }

  if (buf->out_buffer) {
    deflateEnd(&buf->out_stream);
    free(buf->out_buffer);
  }
  if (buf->in_uncompressed) {
    printf("IMAP server compression %lu -> %lu sent %5.1f %% over wire\n",
           buf->in_compressed, buf->in_uncompressed,  
           100.0*(buf->in_compressed/(float)buf->in_uncompressed));
  }
  if (buf->in_uncompressed) {
    printf("IMAP client compression %lu -> %lu sent %5.1f %% over wire\n",
           buf->out_uncompressed, buf->out_compressed,  
           100.0*(buf->out_compressed/(float)buf->out_uncompressed));
  }
}


