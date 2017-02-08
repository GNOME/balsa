#!/bin/sh
# $Id$

echo "starting test environment:"

echo "GnuTLS server w/o client checking @ port 65001 as s_server1..."
 -d -m -S s_server1 \
	 -a --x509keyfile=cert_u.pem --x509certfile=cert_u.pem --echo -p 65001
echo "GnuTLS server w/ client checking @ port 65002 as s_server2..."
 -d -m -S s_server2 \
	 -r --verify-client-cert --x509keyfile=cert_u.pem --x509certfile=cert_u.pem \
	--x509cafile=ca_cert.pem --echo -p 65002
 -ls

echo "inetsim (as root)..."
  --config inetsim.conf

echo "shut down GnuTLS servers..."
 -S s_server1 -X quit
 -S s_server2 -X quit
