/*
 * httperf -- a tool for measuring web server performance
 * Copyright 2000-2007 Hewlett-Packard Company
 *
 * This file is part of httperf, a web server performance measurment
 * tool.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of this work with the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the same
 * license as the "OpenSSL" library), and distribute linked combinations
 * including the two.  You must obey the GNU General Public License in
 * all respects for all of the code used other than "OpenSSL".  If you
 * modify this file, you may extend this exception to your version of the
 * file, but you are not obligated to do so.  If you do not wish to do
 * so, delete this exception statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <generic_types.h>

#include <object.h>
#include <timer.h>
#include <httperf.h>
#include <call.h>
#include <conn.h>
#include <core.h>

static char *srvbase, *srvend, *srvcurrent;

void
conn_add_servers(void)
{
	struct stat st;
	int fd, len;

	fd = open(param.servers, O_RDONLY, 0);
	if (fd == -1)
		panic("%s: can't open %s\n", prog_name, param.servers);

	fstat(fd, &st);
	if (st.st_size == 0)
		panic("%s: file %s is empty\n", prog_name, param.servers);

	srvbase = (char *)mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (srvbase == (char *)-1)
		panic("%s: can't mmap the file: %s\n", prog_name, strerror(errno));

	close(fd);

	srvend = srvbase + st.st_size;
	for (srvcurrent = srvbase; srvcurrent < srvend; srvcurrent += len + 1) {
		len = strlen(srvcurrent);
		core_addr_intern(srvcurrent, len, param.port);
	}
	srvcurrent = srvbase;
}

void
conn_init(Conn *conn)
{
	if (param.servers) {
		int len = strlen(srvcurrent);
		conn->hostname = srvcurrent;
		conn->hostname_len = len;
		conn->fqdname = conn->hostname;
		conn->fqdname_len = conn->hostname_len;

		srvcurrent += len + 1;
		if (srvcurrent >= srvend)
			srvcurrent = srvbase;
	} else if (param.server_name) {
		conn->hostname = param.server;
		conn->hostname_len = strlen(param.server);
		conn->fqdname = param.server_name;
		conn->fqdname_len = strlen(param.server_name);
	} else {
		conn->hostname = param.server;
		conn->hostname_len = strlen(param.server);
		conn->fqdname = conn->hostname;
		conn->fqdname_len = conn->hostname_len;
	}

	conn->port = param.port;
	conn->sd = -1;
	conn->myport = -1;
	conn->line.iov_base = conn->line_buf;
	conn->fqdname_len = strcspn(conn->fqdname,"\r\n"); // Chomp since used in Host header record

#ifdef HAVE_SSL
	if (param.use_ssl) {
		conn->ssl = SSL_new(ssl_ctx);
		if (!conn->ssl) {
			ERR_print_errors_fp(stderr);
			exit(-1);
		}

		if (param.tls_server_name)
		{
			SSL_set_tlsext_host_name(conn->ssl, param.tls_server_name);
		}
		
		if (param.ssl_cipher_list) {
			/* set order of ciphers */
			int ssl_err = SSL_set_cipher_list(conn->ssl, param.ssl_cipher_list);

			if (DBG > 2)
				fprintf(stderr,
					"core_ssl_connect: set_cipher_list returned %d\n",
					ssl_err);
		}
	}
#endif
}

void
conn_deinit(Conn *conn)
{
	assert(conn->sd < 0 && conn->state != S_FREE);
	assert(!conn->sendq);
	assert(!conn->recvq);
	assert(!conn->watchdog);
	conn->state = S_FREE;

#ifdef HAVE_SSL
	if (param.use_ssl)
		SSL_free(conn->ssl);
#endif
}
