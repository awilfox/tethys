#include "ircd.h"

static void m_pass(conn, msg)
struct u_conn *conn;
struct u_msg *msg;
{
	if (conn->pass != NULL)
		free(conn->pass);
	conn->pass = (char*)malloc(strlen(msg->argv[0])+1);
	strcpy(conn->pass, msg->argv[0]);
}

static void make_ureg(conn)
struct u_conn *conn;
{
	conn->ctx = CTX_UREG;

	if (conn->priv == NULL) {
		/* TODO: heap! */
		conn->priv = malloc(sizeof(struct u_user_local));
		u_user_local_init(conn->priv, conn);
	}
}

static void try_reg(conn)
struct u_conn *conn;
{
	struct u_user *u = conn->priv;

	if (!u->nick[0] || !u->ident[0] || !u->gecos[0])
		return;

	u_conn_f(conn, "Welcome!");
	u_conn_f(conn, "nick=%s ident=%s gecos=%s", u->nick, u->ident, u->gecos);

	conn->ctx = CTX_USER;
}

static void m_nick(conn, msg)
struct u_conn *conn;
struct u_msg *msg;
{
	struct u_user_local *u;

	make_ureg(conn);
	u = conn->priv;

	u_strlcpy(USER(u)->nick, msg->argv[0], MAXNICKLEN+1);

	/* TODO: verify */

	try_reg(conn);
}

static void m_user(conn, msg)
struct u_conn *conn;
struct u_msg *msg;
{
	struct u_user_local *u;

	make_ureg(conn);
	u = conn->priv;

	u_strlcpy(USER(u)->ident, msg->argv[0], MAXIDENT+1);
	u_strlcpy(USER(u)->gecos, msg->argv[3], MAXGECOS+1);

	try_reg(conn);
}

struct u_cmd c_ureg[] = {
	{ "PASS", CTX_UNREG,  m_pass, 1 },
	{ "PASS", CTX_UREG,   m_pass, 1 },
	{ "NICK", CTX_UNREG,  m_nick, 1 },
	{ "NICK", CTX_UREG,   m_nick, 1 },
	{ "USER", CTX_UNREG,  m_user, 4 },
	{ "USER", CTX_UREG,   m_user, 4 },
	{ "" }
};
