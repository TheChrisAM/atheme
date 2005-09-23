/*
 * Copyright (c) 2005 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the CService STATUS function.
 *
 * $Id: status.c 2321 2005-09-23 14:08:56Z jilles $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"nickserv/status", FALSE, _modinit, _moddeinit,
	"$Id: status.c 2321 2005-09-23 14:08:56Z jilles $",
	"Atheme Development Group <http://www.atheme.org>"
);

static void ns_cmd_acc(char *origin);
static void ns_cmd_status(char *origin);

command_t ns_status = {
	"STATUS",
	"Displays session information.",
	AC_NONE,
	ns_cmd_status
};

command_t ns_acc = {
	"ACC",
	"Displays parsable session information",
	AC_NONE,
	ns_cmd_acc
};

list_t *ns_cmdtree;

void _modinit(module_t *m)
{
	ns_cmdtree = module_locate_symbol("nickserv/main", "ns_cmdtree");
	command_add(&ns_acc, ns_cmdtree);
	command_add(&ns_status, ns_cmdtree);
}

void _moddeinit()
{
	command_delete(&ns_acc, ns_cmdtree);
	command_delete(&ns_status, ns_cmdtree);
}

static void ns_cmd_acc(char *origin)
{
	char *targ = strtok(NULL, " ");
	user_t *u;

	if (!targ)
		u = user_find(origin);
	else
		u = user_find_named(targ);

	if (!u)
		return;

	if (!u->myuser)
	{
		notice(nicksvs.nick, origin, "%s ACC 0", u->nick);
		return;
	}
	else
		notice(nicksvs.nick, origin, "%s ACC 3", u->nick);
}

static void ns_cmd_status(char *origin)
{
	user_t *u = user_find(origin);

	if (!u->myuser)
	{
		notice(nicksvs.nick, origin, "You are not logged in.");
		return;
	}

	notice(nicksvs.nick, origin, "You are logged in as \2%s\2.", u->myuser->name);

	if (is_sra(u->myuser))
		notice(nicksvs.nick, origin, "You are a services root administrator.");

	if (is_admin(u))
		notice(nicksvs.nick, origin, "You are a server administrator.");

	if (is_ircop(u))
		notice(nicksvs.nick, origin, "You are an IRC operator.");
}

