/* ircd-micro, server.c -- server data
   Copyright (C) 2013 Alex Iadicicco

   This file is protected under the terms contained
   in the COPYING file in the project root */

#include "ircd.h"

u_trie *servers_by_sid;
u_trie *servers_by_name;

u_server me;
u_list my_motd;
char my_net_name[MAXNETNAME+1];
char my_admin_loc1[MAXADMIN+1] = "-";
char my_admin_loc2[MAXADMIN+1] = "-";
char my_admin_email[MAXADMIN+1] = "-";

void server_conf(key, val) char *key, *val;
{
	if (strlen(key) < 3 || memcmp(key, "me.", 3)!=0) {
		u_log(LG_WARN, "server_conf: Can't use %s", key);
		return;
	}
	key += 3;

	if (streq(key, "name")) {
		u_strlcpy(me.name, val, MAXSERVNAME+1);
		u_log(LG_DEBUG, "server_conf: me.name=%s", me.name);
	} else if (streq(key, "net")) {
		u_strlcpy(my_net_name, val, MAXNETNAME+1);
		u_log(LG_DEBUG, "server_conf: me.net=%s", my_net_name);
	} else if (streq(key, "sid")) {
		u_strlcpy(me.sid, val, 4);
		u_log(LG_DEBUG, "server_conf: me.sid=%s", me.sid);
	} else if (streq(key, "desc")) {
		u_strlcpy(me.desc, val, MAXSERVDESC+1);
		u_log(LG_DEBUG, "server_conf: me.desc=%s", me.desc);
	} else {
		u_log(LG_WARN, "server_conf: Can't use %s", key-3);
	}
}

void load_motd(key, val) char *key, *val;
{
	char *s, *p, buf[BUFSIZE];
	FILE *f;

	u_log(LG_INFO, "Loading MOTD from %s", val);

	f = fopen(val, "r");
	if (f == NULL) {
		u_log(LG_ERROR, "Could not open MOTD file %s", val);
		return;
	}

	while (!feof(f)) {
		s = fgets(buf, BUFSIZE, f);
		if (s == NULL)
			break;
		p = strchr(s, '\n');
		if (p) *p = '\0';
		u_log(LG_DEBUG, ":- %s", s);
		u_list_add(&my_motd, u_strdup(s));
	}

	fclose(f);
}

void admin_conf(key, val) char *key, *val;
{
	char *dest;

	if (strlen(key) < 6 || memcmp(key, "admin.", 6) != 0) {
		u_log(LG_WARN, "admin_conf: Can't use %s", key);
		return;
	}
	key += 6;

	if (streq(key, "loc1")) {
		dest = my_admin_loc1;
	} else if (streq(key, "loc2")) {
		dest = my_admin_loc2;
	} else if (streq(key, "email")) {
		dest = my_admin_email;
	} else {
		u_log(LG_WARN, "admin_conf: Can't use %s", key-6);
		return;
	}

	u_strlcpy(dest, val, MAXADMIN);
}

struct capab_info {
	char capab[16];
	uint mask;
} capabs[] = {
	{ "QS",       CAPAB_QS       },
	{ "EX",       CAPAB_EX       },
	{ "CHW",      CAPAB_CHW      },
	{ "IE",       CAPAB_IE       },
	{ "EOB",      CAPAB_EOB      },
	{ "KLN",      CAPAB_KLN      },
	{ "UNKLN",    CAPAB_UNKLN    },
	{ "KNOCK",    CAPAB_KNOCK    },
	{ "TB",       CAPAB_TB       },
	{ "ENCAP",    CAPAB_ENCAP    },
	{ "SERVICES", CAPAB_SERVICES },
	{ "SAVE",     CAPAB_SAVE     },
	{ "RSFNC",    CAPAB_RSFNC    },
	{ "EUID",     CAPAB_EUID     },
	{ "CLUSTER",  CAPAB_CLUSTER  },
	{ "", 0 }
};

static void capab_to_str(capab, buf) uint capab; char *buf;
{
	struct capab_info *info = capabs;
	char *s = buf;

	for (; info->capab[0]; info++) {
		if (!(capab & info->mask))
			continue;
		s += sprintf(s, "%s%s", s == buf ? "" : " ", info->capab);
	}
}

static uint str_to_capab(buf) char *buf;
{
	struct capab_info *info;
	char *s;
	uint capab = 0;

	while ((s = cut(&buf, " ")) != NULL) {
		for (info=capabs; info->capab[0]; info++) {
			if (streq(info->capab, s))
				break;
		}
		if (!info->capab[0])
			continue;
		capab |= info->mask;
	}

	return capab;
}

void u_server_add_capabs(sv, s) u_server *sv; char *s;
{
	sv->capab |= str_to_capab(s);
}

void u_my_capabs(buf) char *buf;
{
	capab_to_str(me.capab, buf);
}

void server_local_die(conn, msg) u_conn *conn; char *msg;
{
	u_server *sv = conn->priv;
	u_server_unlink(sv, msg);
}

void server_local_event(conn, event) u_conn *conn;
{
	switch (event) {
	case EV_ERROR:
		server_local_die(conn, conn->error);
		break;
	default:
		server_local_die(conn, "unknown error");
		break;

	case EV_DESTROYING:
		free(conn->priv);
	}
}

void u_server_make_sreg(conn, sid) u_conn *conn; char *sid;
{
	u_server *sv;

	if (conn->ctx != CTX_UNREG && conn->ctx != CTX_SREG)
		return;

	conn->ctx = CTX_SREG;

	if (conn->priv != NULL)
		return;

	conn->priv = sv = malloc(sizeof(*sv));
	sv->conn = conn;

	strcpy(sv->sid, sid);
	u_trie_set(servers_by_sid, sv->sid, sv);

	sv->name[0] = '\0';
	sv->desc[0] = '\0';
	sv->conn = conn;
	sv->capab = 0;

	conn->event = server_local_event;

	u_log(LG_INFO, "New server sid=%s", sv->sid);
}

void u_server_unlink(sv, msg) u_server *sv; char *msg;
{
	u_conn *conn = sv->conn;

	if (sv == &me) {
		u_log(LG_ERROR, "Can't unlink self!");
		return;
	}

	u_log(LG_INFO, "Unlinking server sid=%s (%s)", sv->sid, msg);

	if (conn->ctx == CTX_SERVER) {
		/* ... */
	} else {
		u_conn_f(conn, "ERROR :%s", msg);
	}

	if (sv->name[0])
		u_trie_del(servers_by_name, sv->name);
	u_trie_del(servers_by_sid, sv->sid);
}

void u_server_burst(sv, link) u_server *sv; u_link *link;
{
	u_conn *conn = sv->conn;
	char buf[512];

	if (conn == NULL) {
		u_log(LG_ERROR, "Attempted to burst to %S, which has no conn!", sv);
		return;
	}

	conn->ctx = CTX_SBURST;

	u_conn_f(conn, "PASS %s TS 6 :%s", link->sendpass, me.sid);
	u_my_capabs(buf);
	u_conn_f(conn, "CAPAB :%s", buf);
	u_conn_f(conn, "SERVER %s 1 :%s", me.name, me.desc);

	/* TODO: "SID and SERVER messages for all known servers" */

	/* TODO: "BAN messages for all propagated bans" */

	/* TODO: "EUID for all known users (possibly followed by ENCAP
	   REALHOST, ENCAP LOGIN, and/or AWAY)" */

	/* TODO: "and SJOIN messages for all known channels (possibly followed
	   by BMASK and/or TB)" */
}

int init_server()
{
	servers_by_sid = u_trie_new(ascii_canonize);
	servers_by_name = u_trie_new(ascii_canonize);

	/* default settings! */
	me.conn = NULL;
	strcpy(me.sid, "22U");
	u_strlcpy(me.name, "micro.irc", MAXSERVNAME+1);
	u_strlcpy(me.desc, "The Tiny IRC Server", MAXSERVDESC+1);
	me.capab = CAPAB_QS | CAPAB_EX | CAPAB_CHW | CAPAB_IE
	         | CAPAB_EOB | CAPAB_KLN | CAPAB_UNKLN | CAPAB_KNOCK
	         | CAPAB_TB | CAPAB_ENCAP | CAPAB_SERVICES
	         | CAPAB_SAVE | CAPAB_EUID;

	u_list_init(&my_motd);

	u_strlcpy(my_net_name, "MicroIRC", MAXNETNAME+1);

	u_trie_set(u_conf_handlers, "me.name", server_conf);
	u_trie_set(u_conf_handlers, "me.net", server_conf);
	u_trie_set(u_conf_handlers, "me.sid", server_conf);
	u_trie_set(u_conf_handlers, "me.desc", server_conf);
	u_trie_set(u_conf_handlers, "me.motd", load_motd);

	u_trie_set(u_conf_handlers, "admin.loc1", admin_conf);
	u_trie_set(u_conf_handlers, "admin.loc2", admin_conf);
	u_trie_set(u_conf_handlers, "admin.email", admin_conf);

	return 1;
}
