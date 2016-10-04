#ifndef _siobuf_h
#define _siobuf_h
/*
 *  This file is part of libESMTP, a library for submission of RFC 2822
 *  formatted electronic mail messages using the SMTP protocol described
 *  in RFC 2821.
 *
 *  Copyright (C) 2001,2002  Brian Stafford  <brian@stafford.uklinux.net>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see
 *  <http://www.gnu.org/licenses/>.
 */

#include <openssl/ssl.h>

typedef struct siobuf *siobuf_t;

#define SIO_BUFSIZE	2048 /* arbitrary, not too short, not too long */
#define SIO_READ	1
#define SIO_WRITE	2

typedef int (*recodecb_t) (char **dstbuf, int *dstlen,
                           const char *srcbuf, int srclen, void *arg);
typedef void (*monitorcb_t) (const char *buffer, int length, int direction,
			     void *arg);
typedef int (*timeoutcb_t) (void *arg);

/* Redefine external symbols to avoid conflicts with libsmtp */
#define sio_attach bnio_attach
#define sio_detach bnio_detach
#define sio_set_monitorcb bnio_set_monitorcb
#define sio_set_timeout bnio_set_timeout
#define sio_set_securitycb bnio_set_securitycb
#define sio_set_timeoutcb bnio_set_timeoutcb
#define sio_poll bnio_poll
#define sio_write bnio_write
#define sio_flush bnio_flush
#define sio_mark bnio_mark
#define sio_fill bnio_fill
#define sio_read bnio_read
#define sio_getc bnio_getc
#define sio_ungetc bnio_ungetc

#define sio_gets bnio_gets
#define sio_printf bnio_printf
#define sio_set_userdata bnio_set_userdata  
#define sio_get_userdata bnio_get_userdata  

#define sio_set_tlsclient_ssl bnio_set_tlsclient_ssl  
#define sio_set_tlsserver_ssl bnio_set_tlsserver_ssl  


struct siobuf *sio_attach(int sdr, int sdw, int buffer_size);
void sio_detach(struct siobuf *sio);
void sio_set_monitorcb(struct siobuf *sio, monitorcb_t cb, void *arg);
void sio_set_timeout(struct siobuf *sio, int milliseconds);
void sio_set_securitycb(struct siobuf *sio, recodecb_t encode_cb,
		        recodecb_t decode_cb, void *arg);
void sio_set_timeoutcb(struct siobuf *sio, timeoutcb_t timeout_cb, void *arg);
int sio_poll(struct siobuf *sio,int want_read, int want_write, int fast);
void sio_write(struct siobuf *sio, const void *bufp, int buflen);
void sio_flush(struct siobuf *sio);
void sio_mark(struct siobuf *sio);
int sio_fill(struct siobuf *sio);
int sio_read(struct siobuf *sio, void *bufp, int buflen);
int sio_getc(struct siobuf *sio);
int sio_ungetc(struct siobuf *sio);

char *sio_gets(struct siobuf *sio, char buf[], int buflen);
int sio_printf(struct siobuf *sio, const char *format, ...)
	       __attribute__ ((format (printf, 2, 3))) ;
void *sio_set_userdata (struct siobuf *sio, void *user_data);
void *sio_get_userdata (struct siobuf *io);

int sio_set_tlsclient_ssl (struct siobuf *sio, SSL *ssl);
int sio_set_tlsserver_ssl (struct siobuf *sio, SSL *ssl);
#endif
