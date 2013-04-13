/* ircd-micro, conn.c -- line-based connection management
   Copyright (C) 2013 Alex Iadicicco

   This file is protected under the terms contained
   in the COPYING file in the project root */

#include "ircd.h"

static void origin_rdns();
static void origin_recv();
static int toplev_post();

void u_conn_init(conn) u_conn *conn;
{
	conn->flags = 0;
	conn->ctx = CTX_UNREG;

	conn->sock = NULL;
	conn->ip[0] = '\0';
	conn->host[0] = '\0';
	conn->dns_id = 0;

	u_linebuf_init(&conn->ibuf);

	conn->obuf = malloc(U_CONN_OBUFSIZE);
	conn->obuflen = 0;
	conn->obufsize = U_CONN_OBUFSIZE;
	u_cookie_reset(&conn->ck_sendto);

	if (conn->obuf == NULL)
		abort();

	conn->event = NULL;
	conn->error = NULL;

	conn->priv = NULL;
	conn->pass = NULL;
}

void u_conn_cleanup(conn) u_conn *conn;
{
	if (conn->dns_id)
		u_dns_cancel(conn->dns_id, origin_rdns, conn);
	if (conn->error)
		free(conn->error);
	free(conn->obuf);
}

/* sadfaec */
void u_conn_obufsize(conn, obufsize) u_conn *conn;
{
	char *buf;

	if (conn->obufsize == obufsize)
		return;

	if (conn->obuflen > obufsize)
		conn->obuflen = obufsize;

	buf = malloc(obufsize);
	memcpy(buf, conn->obuf, conn->obuflen);
	free(conn->obuf);
	conn->obuf = buf;
	conn->obufsize = obufsize;
}

void conn_out_clear(conn) u_conn *conn;
{
	char *s;

	s = (char*)memchr(conn->obuf, '\r', conn->obuflen);
	if (!s || *++s != '\n')
		s = conn->obuf;
	else
		s++;

	conn->obuflen = s - conn->obuf;
}

void u_conn_vf(conn, fmt, va) u_conn *conn; char *fmt; va_list va;
{
	int type;
	char buf[4096];
	char *p, *s, *end;

	if (conn->error)
		conn_out_clear(conn);

	p = conn->obuf + conn->obuflen;
	end = conn->obuf + conn->obufsize - 2; /* -2 for \r\n */

	type = FMT_USER;
	if (conn->ctx == CTX_SERVER || conn->ctx == CTX_SREG)
		type = FMT_SERVER;

	vsnf(type, buf, 4096, fmt, va);
	buf[512] = '\0'; /* i guess it works... */

	u_log(LG_FINE, "[%p] <- %s", conn, buf);

	for (s=buf; p<end && *s;)
		*p++ = *s++;

	if (p >= end) {
		u_conn_error(conn, "Sendq full");
		return;
	}

	*p++ = '\r';
	*p++ = '\n';

	conn->obuflen = p - conn->obuf;
}

#ifdef STDARG
void u_conn_f(u_conn *conn, char *fmt, ...)
#else
void u_conn_f(conn, fmt, va_alist) u_conn *conn; char *fmt; va_dcl
#endif
{
	va_list va;
	u_va_start(va, fmt);
	u_conn_vf(conn, fmt, va);
	va_end(va);
}

void u_conn_vnum(conn, nick, num, va) u_conn *conn; char *nick; va_list va;
{
	char buf[4096];
	char *fmt;

	fmt = u_numeric_fmt[num];

	if (fmt == NULL) {
		u_log(LG_SEVERE, "Attempted to use NULL numeric %d", num);
		return;
	}

	/* numerics are ALWAYS FMT_USER */
	vsnf(FMT_USER, buf, 4096, fmt, va);

	u_conn_f(conn, ":%s %03d %s %s", me.name, num, nick, buf);
}

#ifdef STDARG
void u_conn_num(u_conn *conn, int num, ...)
#else
void u_conn_num(conn, num, va_alist) u_conn *conn; va_dcl
#endif
{
	va_list va;

	u_va_start(va, num);
	switch (conn->ctx) {
	case CTX_UNREG:
		u_conn_vnum(conn, "*", num, va);
		break;
	case CTX_UREG:
	case CTX_USER:
		u_user_vnum(conn->priv, num, va);
		break;
	default:
		u_log(LG_SEVERE, "Can't use u_conn_num on context %d!", conn->ctx);
	}
	va_end(va);
}

void u_conn_error(conn, error) u_conn *conn; char *error;
{
	if (conn->flags & U_CONN_CLOSING)
		return;
	u_log(LG_FINE, "CONN:ERR: [%p] ERR=\"%s\"", conn, error);
	if (conn->error != NULL)
		free(conn->error);
	conn->error = u_strdup(error);
}

u_conn_origin *u_conn_origin_create(io, addr, port)
u_io *io; u_long addr; u_short port;
{
	struct sockaddr_in sa;
	u_conn_origin *orig;
	int fd;

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		goto out;

	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = addr;

	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0)
		goto out;

	if (listen(fd, 5) < 0)
		goto out;

	/* TODO: setsockopt */

	if (!(orig = malloc(sizeof(*orig))))
		goto out_close;

	if (!(orig->sock = u_io_add_fd(io, fd)))
		goto out_free;

	orig->sock->recv = origin_recv;
	orig->sock->send = NULL;
	orig->sock->post = NULL;
	orig->sock->priv = orig;

	return orig;

out_free:
	free(orig);
out_close:
	close(fd);
out:
	return NULL;
}

static void dispatch_lines(conn) u_conn *conn;
{
	char buf[BUFSIZE];
	u_msg msg;
	int sz;

	while ((sz = u_linebuf_line(&conn->ibuf, buf, BUFSIZE)) != 0) {
		if (sz > 0)
			buf[sz] = '\0';
		if (sz < 0 || strlen(buf) != sz) {
			u_conn_error(conn, "Read error");
			break;
		}
		u_log(LG_FINE, "[%p] -> %s", conn, buf);
		u_msg_parse(&msg, buf);
		u_cmd_invoke(conn, &msg);
	}
}

static void origin_rdns(status, name, priv) char *name; void *priv;
{
	u_conn *conn = priv;
	int len;

	conn->dns_id = 0;

	switch (status) {
	case DNS_OKAY:
		len = strlen(name);
		u_strlcpy(conn->host, name, U_CONN_HOSTSIZE);
		conn->host[len-1] = '\0'; /* TODO: move to dns? */
		u_conn_f(conn, ":%s NOTICE * :*** Found your hostname. Hi there %s",
		         me.name, conn->host);
		break;

	default:
		u_strlcpy(conn->host, conn->ip, U_CONN_HOSTSIZE);
		u_conn_f(conn, ":%s NOTICE * :*** Couldn't find your hostname. Using your ip %s", me.name, conn->host);
	}

	dispatch_lines(conn);
}

static void origin_recv(sock) u_io_fd *sock;
{
	u_io_fd *iofd;
	u_conn *conn;
	struct sockaddr_in addr;
	uint addrlen = sizeof(addr);
	int fd;

	if ((fd = accept(sock->fd, (struct sockaddr*)&addr, &addrlen)) < 0) {
		perror("origin_recv");
		return;
	}

	if (!(iofd = u_io_add_fd(sock->io, fd)))
		return; /* XXX */

	iofd->recv = NULL;
	iofd->send = NULL;
	iofd->post = toplev_post;

	conn = malloc(sizeof(*conn));
	u_conn_init(conn);
	conn->sock = iofd;
	iofd->priv = conn;

	u_ntop(&addr.sin_addr, conn->ip);

	u_conn_f(conn, ":%s NOTICE * :*** Looking up your hostname", me.name);
	conn->dns_id = u_rdns(conn->ip, origin_rdns, conn);

	u_log(LG_VERBOSE, "Connection from %s", conn->ip);
}

static void toplev_cleanup(iofd) u_io_fd *iofd;
{
	u_conn *conn = iofd->priv;
	u_log(LG_VERBOSE, "%s disconnecting", conn->host);
	if (conn->event != NULL)
		conn->event(conn, EV_DESTROYING);
	u_conn_cleanup(conn);
	close(iofd->fd);
	free(conn);
}

static void toplev_recv(iofd) u_io_fd *iofd;
{
	u_conn *conn = iofd->priv;
	char buf[1024];
	int sz;

	sz = recv(iofd->fd, buf, 1024-conn->ibuf.pos, 0);

	if (sz <= 0) {
		u_conn_error(conn, sz == 0 ? "End of stream" : "Read error");
		return;
	}

	if (u_linebuf_data(&conn->ibuf, buf, sz) < 0) {
		u_conn_error(conn, "Excess flood");
		return;
	}

	if (conn->host[0])
		dispatch_lines(conn);
}

static void toplev_send(iofd) u_io_fd *iofd;
{
	u_conn *conn = iofd->priv;
	int sz;

	sz = send(iofd->fd, conn->obuf, conn->obuflen, 0);

	if (sz < 0) {
		u_conn_error(conn, "Write error");
		conn->obuflen = 0;
		return;
	}

	if (sz > 0) {
		u_memmove(conn->obuf, conn->obuf + sz, conn->obufsize - sz);
		conn->obuflen -= sz;
	}
}

static int toplev_post(iofd) u_io_fd *iofd;
{
	u_conn *conn = iofd->priv;

	if (conn->error) {
		if (conn->event)
			conn->event(conn, EV_ERROR);
		free(conn->error);
		conn->error = NULL;
		conn->flags |= U_CONN_CLOSING;
	}

	iofd->recv = iofd->send = NULL;

	if (!(conn->flags & U_CONN_CLOSING))
		iofd->recv = toplev_recv;
	if (conn->obuflen > 0)
		iofd->send = toplev_send;

	if (!iofd->recv && !iofd->send) {
		toplev_cleanup(iofd);
		iofd->priv = NULL;
		return -1;
	}

	return 0;
}
