/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2017
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sput.h>
#include "net-client.h"
#include "net-client-smtp.h"
#include "net-client-pop.h"
#include "net-client-siobuf.h"
#include "net-client-utils.h"


#define SUPPRESS_CRITICALS		1


static void test_basic(void);
static void test_basic_crypt(void);
static void test_smtp(void);
static void test_pop3(void);
static void test_siobuf(void);
static void test_utils(void);


static void
log_dummy(const gchar G_GNUC_UNUSED *log_domain, GLogLevelFlags G_GNUC_UNUSED log_level,
          const gchar G_GNUC_UNUSED *message, gpointer G_GNUC_UNUSED user_data)
{
}


int
main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char **argv)
{
	g_log_set_handler("libnetclient", G_LOG_LEVEL_MASK, log_dummy, NULL);

	sput_start_testing();

	sput_enter_suite("test basic (plain)");
	sput_run_test(test_basic);

	sput_enter_suite("test basic (encrypted)");
	sput_run_test(test_basic_crypt);

	sput_enter_suite("test SMTP");
	sput_run_test(test_smtp);

	sput_enter_suite("test POP3");
	sput_run_test(test_pop3);

	sput_enter_suite("test SIOBUF (libbalsa/imap compatibility layer)");
	sput_run_test(test_siobuf);

	sput_enter_suite("test utility functions");
	sput_run_test(test_utils);

	sput_finish_testing();

	return sput_get_return_value();
}


static void
test_basic(void)
{
	NetClient *basic;
	GError *error = NULL;
	gboolean op_res;
	gchar *read_res;
	struct pollfd fds[1];

	sput_fail_unless(net_client_new(NULL, 65000, 42) == NULL, "missing host");

	sput_fail_unless((basic = net_client_new("localhost", 64999, 42)) != NULL, "localhost; port 64999");
	sput_fail_unless(net_client_get_host(NULL) == NULL, "get host w/o client");
	sput_fail_unless(net_client_get_socket(NULL) == NULL, "get host w/o client");
	sput_fail_unless(strcmp(net_client_get_host(basic), "localhost") == 0, "read host ok");
	sput_fail_unless(net_client_get_socket(basic) == NULL, "get socket w/o connection");
	sput_fail_unless(net_client_can_read(NULL) == FALSE, "check can read w/o client");
	sput_fail_unless(net_client_can_read(basic) == FALSE, "check can read w/o connection");
	sput_fail_unless(net_client_connect(basic, NULL) == FALSE, "connect failed");
	op_res = net_client_start_compression(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_NOT_CONNECTED), "start compression: not connected");
	g_clear_error(&error);
	g_object_unref(basic);

	sput_fail_unless((basic = net_client_new("www.google.com", 80, 1)) != NULL, "www.google.com:80; port 0");
	sput_fail_unless(net_client_configure(NULL, "localhost", 65000, 42, NULL) == FALSE, "configure w/o client");
	sput_fail_unless(net_client_configure(basic, NULL, 65000, 42, NULL) == FALSE, "configure w/o host");
	sput_fail_unless(net_client_configure(basic, "localhost", 65000, 42, NULL) == TRUE, "configure localhost:65000 ok");

	sput_fail_unless(net_client_set_timeout(NULL, 3) == FALSE, "set timeout w/o client");
	sput_fail_unless(net_client_set_timeout(basic, 10) == TRUE, "set timeout");

	op_res =  net_client_write_line(basic, "Hi There", &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_NOT_CONNECTED), "write w/o connection");
	g_clear_error(&error);
	op_res =  net_client_read_line(basic, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_NOT_CONNECTED), "read line w/o connection");
	g_clear_error(&error);

	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect succeeded");
	op_res = net_client_connect(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_CONNECTED), "cannot connect already connected");
	g_clear_error(&error);
	op_res = net_client_configure(basic, "localhost", 65000, 42, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_CONNECTED), "cannot configure already connected");
	g_clear_error(&error);
	sput_fail_unless(net_client_get_socket(basic) != NULL, "get socket ok");

	sput_fail_unless(net_client_write_buffer(NULL, "xxx", 3U, NULL) == FALSE, "write buffer w/o client");
	sput_fail_unless(net_client_write_buffer(basic, NULL, 3U, NULL) == FALSE, "write buffer w/o buffer");
	sput_fail_unless(net_client_write_buffer(basic, "xxx", 0U, NULL) == FALSE, "write buffer w/o count");

	sput_fail_unless(net_client_write_line(NULL, "%100s", NULL, "x") == FALSE, "write line w/o client");
	sput_fail_unless(net_client_write_line(basic, NULL, NULL) == FALSE, "write line w/o format string");
	op_res = net_client_write_line(basic, "%100s", &error, "x");
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_LINE_TOO_LONG), "write w/ line too long");
	g_clear_error(&error);
	sput_fail_unless(net_client_write_line(basic, "%s", NULL, "x") == TRUE, "write ok");

	sput_fail_unless(net_client_read_line(NULL, NULL, NULL) == FALSE, "read line w/o client");
	sput_fail_unless(net_client_read_line(basic, NULL, NULL) == TRUE, "read line, data discarded");
	op_res = net_client_read_line(basic, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == G_IO_ERROR_TIMED_OUT), "read line timeout");
	g_clear_error(&error);

	sput_fail_unless(net_client_execute(NULL, NULL, "Hi There", NULL) == FALSE, "execute w/o client");
	sput_fail_unless(net_client_execute(basic, NULL, NULL, NULL) == FALSE, "execute w/o format string");
	sput_fail_unless(net_client_execute(basic, NULL, "%100s", NULL, "x") == FALSE, "execute w/ xmit line too long");
	op_res = net_client_execute(basic, &read_res, "Hi There", NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("Hi There", read_res) == 0), "execute ok");
	g_free(read_res);

	sput_fail_unless(net_client_write_buffer(basic, "H", 1U, NULL) == TRUE, "write buffer part 1");
	sput_fail_unless(net_client_write_buffer(basic, "i Y", 3U, NULL) == TRUE, "write buffer part 2");
	sput_fail_unless(net_client_write_buffer(basic, "ou\r\n", 4U, NULL) == TRUE, "write buffer part 3");
	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("Hi You", read_res) == 0), "read back ok");
	g_free(read_res);

	op_res = net_client_execute(basic, &read_res, "xxxxxxxxxx", &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_LINE_TOO_LONG), "read line too long");
	g_clear_error(&error);

	sput_fail_unless(net_client_can_read(basic) == FALSE, "no data");
	sput_fail_unless(net_client_write_buffer(basic, "line1\r\nline2\r\n", 14U, NULL) == TRUE, "write lines");
	sput_fail_unless(net_client_can_read(basic) == TRUE, "data ready");
	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("line1", read_res) == 0), "read line 1 ok");
	g_free(read_res);
	sput_fail_unless(net_client_can_read(basic) == TRUE, "data ready");
	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("line2", read_res) == 0), "read line 2 ok");
	g_free(read_res);
	sput_fail_unless(net_client_can_read(basic) == FALSE, "no data");

	sput_fail_unless(net_client_start_compression(NULL, NULL) == FALSE, "start compression w/o client");
	op_res = net_client_execute(basic, &read_res, "COMPRESS", NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("COMPRESS", read_res) == 0), "execute 'COMPRESS' ok");
	g_free(read_res);
	sput_fail_unless(net_client_start_compression(basic, NULL) == TRUE, "start compression");
	op_res = net_client_start_compression(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_COMP_ACTIVE), "compression already enabled");
	g_clear_error(&error);

	sput_fail_unless(net_client_can_read(basic) == FALSE, "no data");
	sput_fail_unless(net_client_write_buffer(basic, "line1\r\nline2\r\n", 14U, NULL) == TRUE, "write lines");
	sput_fail_unless(net_client_can_read(basic) == TRUE, "data ready");
	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("line1", read_res) == 0), "read line 1 ok");
	g_free(read_res);
	sput_fail_unless(net_client_can_read(basic) == TRUE, "data ready");
	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("line2", read_res) == 0), "read line 2 ok");
	g_free(read_res);
	sput_fail_unless(net_client_can_read(basic) == FALSE, "no data");

	fds[0].fd = g_socket_get_fd(net_client_get_socket(basic));
	fds[0].events = POLLIN;
	poll(fds, 1, 0);
	sput_fail_unless((fds[0].revents & POLLIN) == 0, "no data for reading");

	sput_fail_unless(net_client_write_line(basic, "Hi There", NULL) == TRUE, "send data");

	poll(fds, 1, 1000);
	sput_fail_unless((fds[0].revents & POLLIN) == POLLIN, "data ready for reading");

	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("Hi There", read_res) == 0), "receive data ok");
	g_free(read_res);

	sput_fail_unless(net_client_write_buffer(basic, "DISCONNECT\r\n", 12U, NULL) == TRUE, "disconnect");
	op_res = net_client_read_line(basic, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code = NET_CLIENT_ERROR_CONNECTION_LOST), "read line, client lost");
	g_clear_error(&error);

	g_object_unref(basic);
}


static gboolean
check_cert(NetClient *client, GTlsCertificate *peer_cert, GTlsCertificateFlags errors, gpointer user_data)
{
	gchar *hash;
	GByteArray *cert_der = NULL;

	g_object_get(G_OBJECT(peer_cert), "certificate", &cert_der, NULL);
	hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, cert_der->data, cert_der->len);
	g_byte_array_unref(cert_der);
	g_message("%s(%p, %p, %x, %p) -> fp(sha256) = %s", __func__, client, peer_cert, errors, user_data, hash);
	g_free(hash);
	return TRUE;
}


static gchar *
get_cert_passwd(NetClient *client, const GByteArray *cert_der, gpointer user_data)
{
	g_message("%s(%p, %p, %p)", __func__, client, cert_der, user_data);
	return g_strdup("test-server");
}


static void
test_basic_crypt(void)
{
	NetClient *basic;
	gboolean op_res;
	gchar *read_res;
	GError *error = NULL;
	struct pollfd fds[1];

	/* tests without client cert check */
	sput_fail_unless((basic = net_client_new("localhost", 65001, 42)) != NULL, "localhost; port 65001");
	sput_fail_unless(net_client_is_encrypted(NULL) == FALSE, "NULL client is unencrypted");
	sput_fail_unless(net_client_is_encrypted(basic) == FALSE, "unconnected client is unencrypted");
	sput_fail_unless(net_client_start_tls(NULL, NULL) == FALSE, "start tls: no client");
	op_res = net_client_start_tls(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_NOT_CONNECTED), "start tls: not connected");
	g_clear_error(&error);
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_is_encrypted(basic) == FALSE, "still unencrypted");
	op_res = net_client_start_tls(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error != NULL), "start tls: bad server cert");
	g_clear_error(&error);

	net_client_shutdown(basic);

	g_signal_connect(G_OBJECT(basic), "cert-check", G_CALLBACK(check_cert), NULL);
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_start_tls(basic, NULL) == TRUE, "start tls: success");
	sput_fail_unless(net_client_is_encrypted(basic) == TRUE, "is encrypted");
	op_res = net_client_start_tls(basic, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_TLS_ACTIVE), "start tls: already started");
	g_clear_error(&error);

	fds[0].fd = g_socket_get_fd(net_client_get_socket(basic));
	fds[0].events = POLLIN;
	poll(fds, 1, 0);
	sput_fail_unless((fds[0].revents & POLLIN) == 0, "no data for reading");

	sput_fail_unless(net_client_write_line(basic, "Hi There", NULL) == TRUE, "send data");

	poll(fds, 1, 1000);
	sput_fail_unless((fds[0].revents & POLLIN) == POLLIN, "data ready for reading");

	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("Hi There", read_res) == 0), "read data ok");
	g_free(read_res);

	sput_fail_unless(net_client_can_read(basic) == FALSE, "no data");
	sput_fail_unless(net_client_write_buffer(basic, "line1\r\nline2\r\n", 14U, NULL) == TRUE, "write lines");
	sput_fail_unless(net_client_can_read(basic) == TRUE, "data ready");
	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("line1", read_res) == 0), "read line 1 ok");
	g_free(read_res);
	sput_fail_unless(net_client_can_read(basic) == TRUE, "data ready");
	op_res = net_client_read_line(basic, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp("line2", read_res) == 0), "read line 2 ok");
	g_free(read_res);
	sput_fail_unless(net_client_can_read(basic) == FALSE, "no data");

	/* compression on top of tls */
	sput_fail_unless(net_client_start_compression(basic, NULL) == TRUE, "start compression");
	// TODO - how can we check exchanging compressed data over tls?
	g_object_unref(basic);

	/* tests with client cert check */
	sput_fail_unless((basic = net_client_new("localhost", 65002, 42)) != NULL, "localhost; port 65002");
	g_signal_connect(G_OBJECT(basic), "cert-check", G_CALLBACK(check_cert), NULL);
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_start_tls(basic, NULL) == FALSE, "start tls: fails");
	sput_fail_unless(net_client_is_encrypted(basic) == FALSE, "not encrypted");
	g_object_unref(basic);

	sput_fail_unless((basic = net_client_new("localhost", 65002, 42)) != NULL, "localhost; port 65002");
	g_signal_connect(G_OBJECT(basic), "cert-check", G_CALLBACK(check_cert), NULL);
	sput_fail_unless(net_client_set_cert_from_file(NULL, "cert_u.pem", NULL) == FALSE, "load cert file w/o client");
	sput_fail_unless(net_client_set_cert_from_file(basic, NULL, NULL) == FALSE, "load cert file w/o file");
	sput_fail_unless(net_client_set_cert_from_file(basic, "no_such_file.crt", NULL) == FALSE, "load cert file, file missing");
	sput_fail_unless(net_client_set_cert_from_pem(NULL, "This is no cert", NULL) == FALSE, "load cert buffer w/o client");
	sput_fail_unless(net_client_set_cert_from_pem(basic, NULL, NULL) == FALSE, "load cert buffer w/o buffer");
	op_res = net_client_set_cert_from_pem(basic, "This is no cert", &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_GNUTLS), "load cert buffer w/ broken pem data");
	g_clear_error(&error);
	sput_fail_unless(net_client_set_cert_from_file(basic, "cert_u.pem", NULL) == TRUE, "load cert file w/ plain key ok");
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_start_tls(basic, NULL) == TRUE, "start tls: ok");
	sput_fail_unless(net_client_is_encrypted(basic) == TRUE, "encrypted");
	g_object_unref(basic);

	sput_fail_unless((basic = net_client_new("localhost", 65002, 42)) != NULL, "localhost; port 65002");
	g_signal_connect(G_OBJECT(basic), "cert-check", G_CALLBACK(check_cert), NULL);
	sput_fail_unless(net_client_set_cert_from_file(basic, "cert_u.pem", NULL) == TRUE, "load cert file w/ plain key ok");
	sput_fail_unless(net_client_set_cert_from_file(basic, "cert.pem", NULL) == FALSE, "load cert file w/ crypt key fails");
	g_signal_connect(G_OBJECT(basic), "cert-pass", G_CALLBACK(get_cert_passwd), NULL);
	sput_fail_unless(net_client_set_cert_from_file(basic, "cert.pem", NULL) == TRUE, "load cert file w/ crypt key ok");
	sput_fail_unless(net_client_connect(basic, NULL) == TRUE, "connect ok");
	sput_fail_unless(net_client_start_tls(basic, NULL) == TRUE, "start tls: ok");
	sput_fail_unless(net_client_is_encrypted(basic) == TRUE, "encrypted");
	g_object_unref(basic);
}


typedef struct {
	gchar *msg_text;
	gchar *read_ptr;
	gboolean sim_error;
} msg_data_t;


static gssize
msg_data_cb(gchar *buffer, gsize count, gpointer user_data, GError **error)
{
	msg_data_t *msg_data = (msg_data_t *) user_data;
	size_t msg_len;
	gssize result;

	g_message("%s(%p, %lu, %p, %p)", __func__, buffer, count, user_data, error);
	if (msg_data->sim_error) {
		result = -1;
	} else {
		msg_len = strlen(msg_data->read_ptr);
		if (msg_len > 0) {
			if (msg_len > count) {
				result = count;
			} else {
				result = msg_len;
			}
			memcpy(buffer, msg_data->read_ptr, result);
			msg_data->read_ptr = &msg_data->read_ptr[result];
		} else {
			result = 0;
		}
	}
	g_message("%s: return %ld", __func__, result);
	return result;
}


#define MSG_TEXT										\
	"From: \"Sender Name\" <me@here.com>\r\n" 			\
	"To: \"Recipient Name\" <you@there.com>\r\n" 		\
	"Cc: \"Other Recipient\" <other@there.com>\r\n" 	\
	"Subject: This is just a test message\r\n" 			\
	"Date: Fri, 13 Jan 2017 21:27:11 +0100\r\n" 		\
	"Message-ID: <20170113212711.19833@here.com>\r\n" 	\
	"\r\n" 												\
	"This is the body of the test message.\r\n"


static gchar **
get_auth(NetClient *client, gboolean need_pwd, gpointer user_data)
{
	gchar ** result;

	g_message("%s(%p, %d, %p)", __func__, client, need_pwd, user_data);
	result = g_new0(gchar *, 3U);
	result[0] = g_strdup("john.doe");
	if (need_pwd && (user_data != NULL)) {
		result[1] = g_strdup("@ C0mplex P@sswd");
	}
	return result;
}


static void
test_smtp(void)
{
	msg_data_t msg_buf;
	NetClientSmtp *smtp;
	NetClientSmtpMessage *msg;
	GError *error = NULL;
	gboolean op_res;
	gchar *read_res;

	// message creation
	msg_buf.msg_text = msg_buf.read_ptr = MSG_TEXT;
	msg_buf.sim_error = FALSE;
	sput_fail_unless(net_client_smtp_msg_new(NULL, NULL) == NULL, "create msg: no callback");
	sput_fail_unless((msg = net_client_smtp_msg_new(msg_data_cb, &msg_buf)) != NULL, "create msg: ok");

	sput_fail_unless(net_client_smtp_msg_set_sender(NULL, "some@sender.com") == FALSE, "set sender, no message");
	sput_fail_unless(net_client_smtp_msg_set_sender(msg, NULL) == FALSE, "set sender, no address");

	sput_fail_unless(net_client_smtp_msg_add_recipient(NULL, "you@there.com", NET_CLIENT_SMTP_DSN_NEVER)  == FALSE,
		"add recipient, no message");
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, NULL, NET_CLIENT_SMTP_DSN_NEVER)  == FALSE,
		"add recipient, no address");

	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(NULL, NULL, FALSE) == FALSE, "set dsn opts, no message");
	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(msg, NULL, FALSE) == TRUE, "set dsn opts ok");


	// smtp stuff - test various failures
	sput_fail_unless(net_client_smtp_new(NULL, 0, NET_CLIENT_CRYPT_NONE) == NULL, "new, missing host");
	sput_fail_unless(net_client_smtp_new("localhost", 0, 0) == NULL, "new, bad crypt mode");
	sput_fail_unless(net_client_smtp_new("localhost", 0, 42) == NULL, "new, bad crypt mode");

	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65024, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost; port 65024");
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == FALSE, "no server");
	g_object_unref(smtp);

	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost:65025");
	op_res = net_client_smtp_connect(smtp, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strcmp(read_res, "mail.inetsim.org INetSim Mail Service ready.") == 0),
		"connect: success");
	g_free(read_res);
	sput_fail_unless(net_client_is_encrypted(NET_CLIENT(smtp)) == FALSE, "not encrypted");
	op_res = net_client_smtp_connect(smtp, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_CONNECTED), "cannot reconnect");
	g_clear_error(&error);
	sput_fail_unless(net_client_smtp_can_dsn(NULL) == FALSE, "NULL client: no dsn");
	sput_fail_unless(net_client_smtp_can_dsn(smtp) == TRUE, "inetsim: can dsn");

	sput_fail_unless(net_client_smtp_send_msg(NULL, msg, NULL) == FALSE, "send msg, NULL client");
	sput_fail_unless(net_client_smtp_send_msg(smtp, NULL, NULL) == FALSE, "send msg, NULL message");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == FALSE, "send msg: error, no sender");
	sput_fail_unless(net_client_smtp_msg_set_sender(msg, "some@sender.com") == TRUE, "set sender ok");
	sput_fail_unless(net_client_smtp_msg_set_sender(msg, "me@here.com") == TRUE, "replace sender ok");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == FALSE, "send msg: error, no recipient");
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "you@there.com", NET_CLIENT_SMTP_DSN_NEVER)  == TRUE,
		"add recipient ok (no dsn)");
	op_res = net_client_smtp_send_msg(smtp, msg, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_SMTP_PERMANENT), "send failed: not authenticated");
	g_clear_error(&error);
	g_object_unref(smtp);

	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_STARTTLS)) != NULL,
		"localhost:65025, starttls");
	op_res = net_client_smtp_connect(smtp, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error != NULL), "connect: fails (untrusted cert)");
	g_clear_error(&error);
	g_object_unref(smtp);

	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost:65025");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, FALSE, 0U) == TRUE, "force no auth mech available");
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	op_res = net_client_smtp_connect(smtp, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_SMTP_NO_AUTH), "connect: fails");
	g_clear_error(&error);
	g_object_unref(smtp);

	// no password: anonymous
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost:65025");
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), NULL);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == FALSE, "connect: password required");
	g_object_unref(smtp);

	// unencrypted, PLAIN auth
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost:65025");
	sput_fail_unless(net_client_smtp_allow_auth(NULL, FALSE, NET_CLIENT_SMTP_AUTH_PLAIN) == FALSE, "set auth meths, no client");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, FALSE, NET_CLIENT_SMTP_AUTH_PLAIN) == TRUE, "force auth meth PLAIN");
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	msg_buf.sim_error = TRUE;
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == FALSE, "send msg: error in callback");
	msg_buf.sim_error = FALSE;
	g_object_unref(smtp);

	// unencrypted, PLAIN auth
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_NONE)) != NULL, "localhost:65025");
	sput_fail_unless(net_client_smtp_allow_auth(NULL, FALSE, NET_CLIENT_SMTP_AUTH_PLAIN) == FALSE, "set auth meths, no client");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, FALSE, NET_CLIENT_SMTP_AUTH_PLAIN) == TRUE, "force auth meth PLAIN");
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	// STARTTLS required, LOGIN auth
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "other1@there.com", NET_CLIENT_SMTP_DSN_SUCCESS) == TRUE,
		"add recipient ok (dsn)");
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_STARTTLS)) != NULL,
		"localhost:65025, starttls");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, TRUE, NET_CLIENT_SMTP_AUTH_LOGIN) == TRUE, "force auth meth LOGIN");
	g_signal_connect(G_OBJECT(smtp), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	// STARTTLS optional, CRAM-MD5 auth
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "other2@there.com", NET_CLIENT_SMTP_DSN_FAILURE) == TRUE,
		"add recipient ok (dsn)");
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_STARTTLS_OPT)) != NULL,
		"localhost:65025, starttls");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, TRUE, NET_CLIENT_SMTP_AUTH_CRAM_MD5) == TRUE, "force auth meth CRAM-MD5");
	g_signal_connect(G_OBJECT(smtp), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(msg, "20170113212711.19833@here.com", FALSE) == TRUE, "dsn: envid, headers");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	// STARTTLS, CRAM-SHA1 auth
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "other3@there.com", NET_CLIENT_SMTP_DSN_DELAY) == TRUE,
		"add recipient ok (dsn)");
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_STARTTLS)) != NULL, "localhost:65025");
	sput_fail_unless(net_client_smtp_allow_auth(smtp, TRUE, NET_CLIENT_SMTP_AUTH_CRAM_SHA1) == TRUE, "force auth meth CRAM-SHA1");
	g_signal_connect(G_OBJECT(smtp), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(msg, NULL, TRUE) == TRUE, "dsn: no envid, message");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	// STARTTLS, auto select auth
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "other4@there.com",
		NET_CLIENT_SMTP_DSN_SUCCESS + NET_CLIENT_SMTP_DSN_FAILURE + NET_CLIENT_SMTP_DSN_DELAY) == TRUE, "add recipient ok (dsn)");
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65025, NET_CLIENT_CRYPT_STARTTLS)) != NULL, "localhost:65025");
	g_signal_connect(G_OBJECT(smtp), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(msg, "20170113212711.19833@here.com", TRUE) == TRUE,
		"dsn: envid, message");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	// SSL, auto select auth
	sput_fail_unless(net_client_smtp_msg_add_recipient(msg, "other4@there.com",
		NET_CLIENT_SMTP_DSN_SUCCESS + NET_CLIENT_SMTP_DSN_FAILURE + NET_CLIENT_SMTP_DSN_DELAY) == TRUE, "add recipient ok (dsn)");
	sput_fail_unless((smtp = net_client_smtp_new("localhost", 65465, NET_CLIENT_CRYPT_ENCRYPTED)) != NULL, "localhost:65465, ssl");
	g_signal_connect(G_OBJECT(smtp), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(smtp), "auth", G_CALLBACK(get_auth), smtp);
	sput_fail_unless(net_client_smtp_connect(smtp, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_smtp_msg_set_dsn_opts(msg, "20170113212711.19833@here.com", TRUE) == TRUE,
		"dsn: envid, message");
	sput_fail_unless(net_client_smtp_send_msg(smtp, msg, NULL) == TRUE, "send msg: success");
	g_object_unref(smtp);

	net_client_smtp_msg_free(NULL);
	net_client_smtp_msg_free(msg);
}

static gboolean
msg_cb(const gchar *buffer, gssize count, gsize lines, const NetClientPopMessageInfo *info, gpointer user_data, GError **error)
{
	g_message("%s(%p, %ld, %lu, %p, %p, %p)", __func__, buffer, count, lines, info, user_data, error);
	if (((GPOINTER_TO_INT(user_data) == 1) && (count > 0)) ||
		((GPOINTER_TO_INT(user_data) == 2) && (count == 0))) {
		return FALSE;
	} else {
		return TRUE;
	}
}

static void
test_pop3(void)
{
	NetClientPop *pop;
	GError *error = NULL;
	gboolean op_res;
	gchar *read_res;
	gsize msg_count;
	gsize mbox_size;
	GList *msg_list;

	// some error cases
	sput_fail_unless(net_client_pop_new(NULL, 0, NET_CLIENT_CRYPT_NONE, TRUE) == NULL, "new, missing host");
	sput_fail_unless(net_client_pop_new("localhost", 0, 0, TRUE) == NULL, "new, bad crypt mode");
	sput_fail_unless(net_client_pop_new("localhost", 0, 42, TRUE) == NULL, "new, bad crypt mode");
	sput_fail_unless(net_client_pop_allow_auth(NULL, TRUE, 0U) == FALSE, "allow auth, no client");
	sput_fail_unless(net_client_pop_connect(NULL, NULL, NULL) == FALSE, "connect, no client");
	sput_fail_unless(net_client_pop_stat(NULL, NULL, NULL, NULL) == FALSE, "stat, no client");
	sput_fail_unless(net_client_pop_list(NULL, NULL, FALSE, NULL) == FALSE, "list, no client");
	sput_fail_unless(net_client_pop_retr(NULL, NULL, NULL, NULL, NULL) == FALSE, "retr, no client");
	sput_fail_unless(net_client_pop_dele(NULL, NULL, NULL) == FALSE, "dele, no client");
	net_client_pop_msg_info_free(NULL);		// just for checking

	// some basic stuff
	sput_fail_unless((pop = net_client_pop_new("localhost", 65109, NET_CLIENT_CRYPT_NONE, TRUE)) != NULL, "localhost; port 65109");
	sput_fail_unless(net_client_pop_connect(pop, NULL, NULL) == FALSE, "no server");
	g_object_unref(pop);

	sput_fail_unless((pop = net_client_pop_new("localhost", 64110, NET_CLIENT_CRYPT_NONE, TRUE)) != NULL, "localhost:64110");
	op_res = net_client_pop_connect(pop, &read_res, NULL);
	sput_fail_unless((op_res == TRUE) && (strncmp(read_res, "INetSim POP3 Server ready <", 27U) == 0),
		"connect: success");
	g_free(read_res);
	sput_fail_unless(net_client_is_encrypted(NET_CLIENT(pop)) == FALSE, "not encrypted");
	op_res = net_client_pop_connect(pop, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_CONNECTED), "cannot reconnect");
	g_clear_error(&error);

	// not allowed if unauthenticated
	op_res = net_client_pop_stat(pop, NULL, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_POP_SERVER_ERR), "STAT not allowed w/o AUTH");
	g_clear_error(&error);
	sput_fail_unless(net_client_pop_list(pop, NULL, TRUE, NULL) == FALSE, "list w/ empty target list");
	op_res = net_client_pop_list(pop, &msg_list, TRUE, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_POP_SERVER_ERR), "LIST not allowed w/o AUTH");
	g_clear_error(&error);
	g_object_unref(pop);

	sput_fail_unless((pop = net_client_pop_new("localhost", 64110, NET_CLIENT_CRYPT_NONE, TRUE)) != NULL, "localhost:64110");
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), NULL);
	sput_fail_unless(net_client_pop_connect(pop, NULL, NULL) == FALSE, "connect: password required");
	g_object_unref(pop);

	// unencrypted, force USER auth
	sput_fail_unless((pop = net_client_pop_new("localhost", 64110, NET_CLIENT_CRYPT_NONE, TRUE)) != NULL, "localhost:64110");
	sput_fail_unless(net_client_pop_allow_auth(pop, FALSE, 0U), "no AUTH mechanism");
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), pop);
	op_res = net_client_pop_connect(pop, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error->code == NET_CLIENT_ERROR_POP_NO_AUTH), "no suitable AUTH mechanism");
	g_clear_error(&error);
	g_object_unref(pop);

	// STARTTLS
	sput_fail_unless((pop = net_client_pop_new("localhost", 64110, NET_CLIENT_CRYPT_STARTTLS, TRUE)) != NULL,
		"localhost:64110, starttls");
	op_res = net_client_pop_connect(pop, NULL, &error);
	sput_fail_unless((op_res == FALSE) && (error != NULL), "connect: fails (untrusted cert)");
	g_clear_error(&error);
	g_object_unref(pop);

	sput_fail_unless((pop = net_client_pop_new("localhost", 64110, NET_CLIENT_CRYPT_STARTTLS_OPT, TRUE)) != NULL,
		"localhost:64110, starttls (opt)");
	op_res = net_client_pop_connect(pop, NULL, &error);
	sput_fail_unless((op_res == TRUE) && (error == NULL), "connect: ok, but...");
	sput_fail_unless(net_client_is_encrypted(NET_CLIENT(pop)) == FALSE, "...not encrypted");
	g_object_unref(pop);

	// STARTTLS required, USER/PASS auth
	sput_fail_unless((pop = net_client_pop_new("localhost", 64110, NET_CLIENT_CRYPT_STARTTLS, FALSE)) != NULL,
		"localhost:64110, starttls, no pipelining");
	sput_fail_unless(net_client_pop_allow_auth(pop, TRUE, NET_CLIENT_POP_AUTH_USER_PASS) == TRUE, "force auth meth USER/PASS");
	g_signal_connect(G_OBJECT(pop), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), pop);
	sput_fail_unless(net_client_pop_connect(pop, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_pop_stat(pop, &msg_count, NULL, NULL) == TRUE, "STAT: success");
	sput_fail_unless(net_client_pop_list(pop, &msg_list, FALSE, NULL) == TRUE, "LIST: success");
	g_message("message count: %u", g_list_length(msg_list));
	if (msg_list != NULL) {
		sput_fail_unless(net_client_pop_retr(pop, NULL, msg_cb, NULL, NULL) == FALSE, "retr w/o message list");
		sput_fail_unless(net_client_pop_retr(pop, msg_list, NULL, NULL, NULL) == FALSE, "retr w/o callback");
		sput_fail_unless(net_client_pop_retr(pop, msg_list, msg_cb, GINT_TO_POINTER(1), NULL) == FALSE, "retr error");
		g_list_free_full(msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
	}
	g_object_unref(pop);

	// STARTTLS optional, APOP auth
	sput_fail_unless((pop = net_client_pop_new("localhost", 64110, NET_CLIENT_CRYPT_STARTTLS_OPT, TRUE)) != NULL,
		"localhost:64110, starttls opt, pipelining");
	sput_fail_unless(net_client_pop_allow_auth(pop, TRUE, NET_CLIENT_POP_AUTH_APOP) == TRUE, "force auth meth APOP");
	g_signal_connect(G_OBJECT(pop), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), pop);
	sput_fail_unless(net_client_pop_connect(pop, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_pop_stat(pop, NULL, &mbox_size, NULL) == TRUE, "STAT: success");
	sput_fail_unless(net_client_pop_list(pop, &msg_list, FALSE, NULL) == TRUE, "LIST: success");
	g_message("message count: %u", g_list_length(msg_list));
	if (msg_list != NULL) {
		sput_fail_unless(net_client_pop_retr(pop, msg_list, msg_cb, GINT_TO_POINTER(2), NULL) == FALSE, "retr error");
		g_list_free_full(msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
	}
	g_object_unref(pop);

	// STARTTLS required, PLAIN auth
	sput_fail_unless((pop = net_client_pop_new("localhost", 64110, NET_CLIENT_CRYPT_STARTTLS, TRUE)) != NULL,
		"localhost:64110, starttls, pipelining");
	sput_fail_unless(net_client_pop_allow_auth(pop, TRUE, NET_CLIENT_POP_AUTH_PLAIN) == TRUE, "force auth meth PLAIN");
	g_signal_connect(G_OBJECT(pop), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), pop);
	sput_fail_unless(net_client_pop_connect(pop, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_pop_list(pop, &msg_list, FALSE, NULL) == TRUE, "LIST: success");
	g_message("message count: %u", g_list_length(msg_list));
	if (msg_list != NULL) {
		sput_fail_unless(net_client_pop_retr(pop, msg_list, msg_cb, NULL, NULL) == TRUE, "retr ok");
		g_list_free_full(msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
	}
	g_object_unref(pop);

	// STARTTLS optional, PLAIN auth
	sput_fail_unless((pop = net_client_pop_new("localhost", 64110, NET_CLIENT_CRYPT_STARTTLS_OPT, FALSE)) != NULL,
		"localhost:64110, starttls opt, no pipelining");
	sput_fail_unless(net_client_pop_allow_auth(pop, TRUE, NET_CLIENT_POP_AUTH_PLAIN) == TRUE, "force auth meth PLAIN");
	g_signal_connect(G_OBJECT(pop), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), pop);
	sput_fail_unless(net_client_pop_connect(pop, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_pop_list(pop, &msg_list, FALSE, NULL) == TRUE, "LIST: success");
	g_message("message count: %u", g_list_length(msg_list));
	if (msg_list != NULL) {
		sput_fail_unless(net_client_pop_retr(pop, msg_list, msg_cb, NULL, NULL) == TRUE, "retr ok");
		g_list_free_full(msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
	}
	g_object_unref(pop);

	// SSL, LOGIN auth
	sput_fail_unless((pop = net_client_pop_new("localhost", 64995, NET_CLIENT_CRYPT_ENCRYPTED, FALSE)) != NULL,
		"localhost:64995, pop3s, no pipelining");
	sput_fail_unless(net_client_pop_allow_auth(pop, TRUE, NET_CLIENT_POP_AUTH_LOGIN) == TRUE, "force auth meth LOGIN");
	g_signal_connect(G_OBJECT(pop), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), pop);
	sput_fail_unless(net_client_pop_connect(pop, NULL, &error) == TRUE, "connect: success");
	sput_fail_unless(net_client_pop_stat(pop, &msg_count, &mbox_size, NULL) == TRUE, "STAT: success");
	sput_fail_unless(net_client_pop_list(pop, &msg_list, FALSE, NULL) == TRUE, "LIST: success");
	g_message("message count: %u", g_list_length(msg_list));
	if (msg_list != NULL) {
		sput_fail_unless(net_client_pop_retr(pop, msg_list, msg_cb, GINT_TO_POINTER(1), NULL) == FALSE, "retr error");
		g_list_free_full(msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
	}
	g_object_unref(pop);

	// SSL, CRAM-MD5 auth
	sput_fail_unless((pop = net_client_pop_new("localhost", 64995, NET_CLIENT_CRYPT_ENCRYPTED, TRUE)) != NULL,
		"localhost:64995, pop3s, pipelining");
	sput_fail_unless(net_client_pop_allow_auth(pop, TRUE, NET_CLIENT_POP_AUTH_CRAM_MD5) == TRUE, "force auth meth CRAM-MD5");
	g_signal_connect(G_OBJECT(pop), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), pop);
	sput_fail_unless(net_client_pop_connect(pop, NULL, NULL) == TRUE, "connect: success");
	sput_fail_unless(net_client_pop_list(pop, &msg_list, TRUE, NULL) == TRUE, "LIST: success");
	g_message("message count: %u", g_list_length(msg_list));
	if (msg_list != NULL) {
		sput_fail_unless(net_client_pop_retr(pop, msg_list, msg_cb, GINT_TO_POINTER(2), NULL) == FALSE, "retr error");
		g_list_free_full(msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
	}
	g_object_unref(pop);

	// SSL, CRAM-SHA1 auth
	sput_fail_unless((pop = net_client_pop_new("localhost", 64995, NET_CLIENT_CRYPT_ENCRYPTED, FALSE)) != NULL,
		"localhost:64995, pop3s, no pipelining");
	sput_fail_unless(net_client_pop_allow_auth(pop, TRUE, NET_CLIENT_POP_AUTH_CRAM_SHA1) == TRUE, "force auth meth CRAM-SHA1");
	g_signal_connect(G_OBJECT(pop), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), pop);
	sput_fail_unless(net_client_pop_connect(pop, NULL, &error) == TRUE, "connect: success");
	sput_fail_unless(net_client_pop_list(pop, &msg_list, TRUE, NULL) == TRUE, "LIST: success");
	g_message("message count: %u", g_list_length(msg_list));
	if (msg_list != NULL) {
		sput_fail_unless(net_client_pop_retr(pop, msg_list, msg_cb, NULL, NULL) == TRUE, "retr ok");
		g_list_free_full(msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
	}
	g_object_unref(pop);

	// SSL, CRAM-SHA1 auth
	sput_fail_unless((pop = net_client_pop_new("localhost", 64995, NET_CLIENT_CRYPT_ENCRYPTED, TRUE)) != NULL,
		"localhost:64995, pop3s, pipelining");
	sput_fail_unless(net_client_pop_allow_auth(pop, TRUE, NET_CLIENT_POP_AUTH_CRAM_SHA1) == TRUE, "force auth meth CRAM-SHA1");
	g_signal_connect(G_OBJECT(pop), "cert-check", G_CALLBACK(check_cert), NULL);
	g_signal_connect(G_OBJECT(pop), "auth", G_CALLBACK(get_auth), pop);
	sput_fail_unless(net_client_pop_connect(pop, NULL, &error) == TRUE, "connect: success");
	sput_fail_unless(net_client_pop_list(pop, &msg_list, TRUE, NULL) == TRUE, "LIST: success");
	g_message("message count: %u", g_list_length(msg_list));
	if (msg_list != NULL) {
		sput_fail_unless(net_client_pop_retr(pop, msg_list, msg_cb, NULL, NULL) == TRUE, "retr ok");
		sput_fail_unless(net_client_pop_dele(pop, NULL,NULL) == FALSE, "dele w/o message list");
		sput_fail_unless(net_client_pop_dele(pop, msg_list, NULL) == TRUE, "dele ok");
		g_list_free_full(msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
	}
	op_res = net_client_pop_list(pop, &msg_list, TRUE, NULL);
	sput_fail_unless((op_res == TRUE) && (msg_list == NULL), "LIST: success, empty");
	g_object_unref(pop);

}


static void
test_siobuf(void)
{
	NetClientSioBuf *siobuf;
	gchar buffer[64];
	GError *error = NULL;
	gint read_res;
	gboolean op_res;
	gchar *recv_data;

	sput_fail_unless(net_client_siobuf_new(NULL, 65000) == NULL, "missing host");
	sput_fail_unless((siobuf = net_client_siobuf_new("localhost", 65000)) != NULL, "localhost; port 65000");

	sput_fail_unless(net_client_siobuf_getc(NULL, NULL)  == -1, "getc w/o client");
	sput_fail_unless(net_client_siobuf_getc(siobuf, NULL) == -1, "getc fails, not connected");

	sput_fail_unless(net_client_siobuf_gets(NULL, buffer, 1024U, NULL) == NULL, "gets w/o client");
	sput_fail_unless(net_client_siobuf_gets(siobuf, NULL, 1024U, NULL) == NULL, "gets w/o buffer");
	sput_fail_unless(net_client_siobuf_gets(siobuf, buffer, 0U, NULL) == NULL, "gets w/o buffer size");
	sput_fail_unless(net_client_siobuf_gets(siobuf, buffer, 32U, NULL) == NULL, "gets fails, not connected");

	sput_fail_unless(net_client_siobuf_read(NULL, buffer, 1024U, NULL) == -1, "read w/o client");
	sput_fail_unless(net_client_siobuf_read(siobuf, NULL, 1024U, NULL) == -1, "read w/o buffer");
	sput_fail_unless(net_client_siobuf_read(siobuf, buffer, 0U, NULL) == -1, "read w/o buffer size");
	sput_fail_unless(net_client_siobuf_read(siobuf, buffer, 32U, NULL) == -1, "read fails, not connected");

	sput_fail_unless(net_client_siobuf_ungetc(NULL) == -1, "ungetc w/o client");
	sput_fail_unless(net_client_siobuf_ungetc(siobuf) == -1, "ungetc at beginning of buffer");

	sput_fail_unless(net_client_set_timeout(NET_CLIENT(siobuf), 10) == TRUE, "set timeout");
	sput_fail_unless(net_client_connect(NET_CLIENT(siobuf), NULL) == TRUE, "connect");
	sput_fail_unless(net_client_write_buffer(NET_CLIENT(siobuf), "line1\r\nLINE2\r\nABCD3\r\n", 21U, NULL) == TRUE, "write data");

	sput_fail_unless(net_client_siobuf_getc(siobuf, NULL) == 'l', "getc ok");
	sput_fail_unless(net_client_siobuf_getc(siobuf, NULL) == 'i', "getc ok");
	sput_fail_unless(net_client_siobuf_ungetc(siobuf) == 0, "ungetc ok");
	sput_fail_unless(net_client_siobuf_ungetc(siobuf) == 0, "ungetc ok");
	sput_fail_unless(net_client_siobuf_ungetc(siobuf) == -1, "ungetc at beginning of buffer");

	sput_fail_unless((net_client_siobuf_gets(siobuf, buffer, 3U, NULL) == buffer) && (strcmp(buffer, "li") == 0), "gets ok");
	sput_fail_unless((net_client_siobuf_gets(siobuf, buffer, 32U, NULL) == buffer) && (strcmp(buffer, "ne1\r\n") == 0), "gets ok");

	memset(buffer, 0, sizeof(buffer));
	sput_fail_unless((net_client_siobuf_read(siobuf, buffer, 10U, NULL) == 10) && (strcmp(buffer, "LINE2\r\nABC") == 0), "read ok");
	memset(buffer, 0, sizeof(buffer));
	read_res = net_client_siobuf_read(siobuf, buffer, 10U, &error);
	sput_fail_unless((read_res == 4) && (strcmp(buffer, "D3\r\n") == 0) && (error->code == G_IO_ERROR_TIMED_OUT), "short read ok");
	g_clear_error(&error);

	net_client_siobuf_write(NULL, "abcd", 4U);
	net_client_siobuf_write(siobuf, NULL, 4U);
	net_client_siobuf_write(siobuf, "abcd", 0U);
	net_client_siobuf_write(siobuf, "abcd", 4U);

	net_client_siobuf_printf(NULL, "%d", 4711);
	net_client_siobuf_printf(siobuf, NULL);
	net_client_siobuf_printf(siobuf, "%d", 4711);

	sput_fail_unless(net_client_siobuf_flush(NULL, NULL) == FALSE, "flush write w/o client");
	sput_fail_unless(net_client_siobuf_flush(siobuf, NULL) == TRUE, "flush successful");
	sput_fail_unless(net_client_siobuf_flush(siobuf, NULL) == FALSE, "flush write w/o data");

	op_res = net_client_read_line(NET_CLIENT(siobuf), &recv_data, &error);
	sput_fail_unless(op_res && (strcmp(recv_data, "abcd4711") == 0), "buffered write: read back ok");
	g_free(recv_data);
	sput_fail_unless(net_client_can_read(NET_CLIENT(siobuf)) == FALSE, "no data left");

	net_client_siobuf_write(siobuf, "abcd\r\n1234\r\nqrst\r\n9876", 22U);
	sput_fail_unless(net_client_siobuf_flush(siobuf, NULL) == TRUE, "flush successful");

	sput_fail_unless(net_client_siobuf_get_line(NULL, NULL) == NULL, "get line w/o client");
	sput_fail_unless(net_client_siobuf_getc(siobuf, NULL) == 'a', "getc ok");
	recv_data = net_client_siobuf_get_line(siobuf, NULL);
	sput_fail_unless(strcmp(recv_data, "bcd") == 0, "get line #1 ok");
	g_free(recv_data);
	recv_data = net_client_siobuf_get_line(siobuf, NULL);
	sput_fail_unless(strcmp(recv_data, "1234") == 0, "get line #2 ok");
	g_free(recv_data);
	sput_fail_unless((net_client_siobuf_gets(siobuf, buffer, 5U, NULL) == buffer) && (strcmp(buffer, "qrst") == 0), "gets ok");
	recv_data = net_client_siobuf_get_line(siobuf, NULL);
	sput_fail_unless(strcmp(recv_data, "") == 0, "get line #3 ok");
	g_free(recv_data);

	sput_fail_unless(net_client_siobuf_getc(siobuf, NULL) == '9', "getc ok");
	sput_fail_unless(net_client_siobuf_discard_line(NULL, NULL) == -1, "discard line w/o client");
	sput_fail_unless(net_client_siobuf_discard_line(siobuf, NULL) == '\n', "discard line ok");
	sput_fail_unless(net_client_siobuf_discard_line(siobuf, NULL) == -1, "discard line w/o data");
	sput_fail_unless(net_client_siobuf_get_line(siobuf, NULL) == NULL, "get line w/o data");

	g_object_unref(siobuf);
}

static void
test_utils(void)
{
	gchar *authstr;

	sput_fail_unless(strcmp(net_client_chksum_to_str(G_CHECKSUM_MD5), "MD5") == 0, "checksum string for MD5");
	sput_fail_unless(strcmp(net_client_chksum_to_str(G_CHECKSUM_SHA1), "SHA1") == 0, "checksum string for SHA1");
	sput_fail_unless(strcmp(net_client_chksum_to_str(G_CHECKSUM_SHA256), "SHA256") == 0, "checksum string for SHA256");
	sput_fail_unless(strcmp(net_client_chksum_to_str(G_CHECKSUM_SHA512), "SHA512") == 0, "checksum string for SHA512");
	sput_fail_unless(strcmp(net_client_chksum_to_str((GChecksumType) -1), "_UNKNOWN_") == 0, "checksum string for unknown");

	/* note: test the md5 example from rfc2195, sect. 2; other hashes calculated from
	 *       http://www.freeformatter.com/hmac-generator.html */
#define CRAM_MD5_CHAL	"PDE4OTYuNjk3MTcwOTUyQHBvc3RvZmZpY2UucmVzdG9uLm1jaS5uZXQ+"
#define CRAM_MD5_USER	"tim"
#define CRAM_MD5_PASS	"tanstaaftanstaaf"
#define CRAM_MD5_RES	"dGltIGI5MTNhNjAyYzdlZGE3YTQ5NWI0ZTZlNzMzNGQzODkw"
#define CRAM_SHA1_RES	"dGltIDhjYjEwZWQwYThiNzY0YWIwZTA1MTI1ZGQ2ZWFhMjk1ZjE5YjU5NDM="
#define CRAM_SHA256_RES "dGltIGYwMDExYmJmN2MxNjE4ZWY5MGUzZjE4MTQ4ZTVlZGE0ZTE1NjMxYzljMzhlYmVmMDYyYmY3MTQzZmY3MmU5NDQ="
	sput_fail_unless(net_client_cram_calc(NULL, G_CHECKSUM_MD5, CRAM_MD5_USER, CRAM_MD5_PASS) == NULL, "cram-md5: no challenge");
	sput_fail_unless(net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_MD5, NULL, CRAM_MD5_PASS) == NULL, "cram-md5: no user");
	sput_fail_unless(net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_MD5, CRAM_MD5_USER, NULL) == NULL, "cram-md5: no password");
	authstr = net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_MD5, CRAM_MD5_USER, CRAM_MD5_PASS);
	sput_fail_unless(strcmp(authstr, CRAM_MD5_RES) == 0, "cram-md5: auth string ok");
	g_free(authstr);

	authstr = net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_SHA1, CRAM_MD5_USER, CRAM_MD5_PASS);
	sput_fail_unless(strcmp(authstr, CRAM_SHA1_RES) == 0, "cram-sha1: auth string ok");
	g_free(authstr);

	authstr = net_client_cram_calc(CRAM_MD5_CHAL, G_CHECKSUM_SHA256, CRAM_MD5_USER, CRAM_MD5_PASS);
	sput_fail_unless(strcmp(authstr, CRAM_SHA256_RES) == 0, "cram-sha256: auth string ok");
	g_free(authstr);

#define AUTH_PLAIN_RES	"dGltAHRpbQB0YW5zdGFhZnRhbnN0YWFm"
	sput_fail_unless(net_client_auth_plain_calc(NULL, CRAM_MD5_PASS) == NULL, "auth plain: no user");
	sput_fail_unless(net_client_auth_plain_calc(CRAM_MD5_USER, NULL) == NULL, "auth plain: no password");
	authstr = net_client_auth_plain_calc(CRAM_MD5_USER, CRAM_MD5_PASS);
	sput_fail_unless(strcmp(authstr, AUTH_PLAIN_RES) == 0, "auth plain: auth string ok");
	g_free(authstr);
}
