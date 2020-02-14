/*
 * siobuf-nc.h
 * libnetclient <--> libbalsa/imap glue layer
 * Written by (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2018
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

#ifndef LIBBALSA_IMAP_SIOBUF_NC_H_
#define LIBBALSA_IMAP_SIOBUF_NC_H_

#include "net-client-siobuf.h"


#define sio_read(sio, bufp, buflen)			net_client_siobuf_read(sio, bufp, buflen, NULL)
#define sio_getc(sio)						net_client_siobuf_getc(sio, NULL)
#define sio_ungetc(sio)						net_client_siobuf_ungetc(sio)
#define sio_gets(sio, buf, buflen)			net_client_siobuf_gets(sio, buf, buflen, NULL)
#define sio_write(sio, buf, buflen)			net_client_siobuf_write(sio, buf, buflen)
#define sio_printf(sio, format, ...)		net_client_siobuf_printf(sio, format, ##__VA_ARGS__)


#endif /* LIBBALSA_IMAP_SIOBUF_NC_H_ */
