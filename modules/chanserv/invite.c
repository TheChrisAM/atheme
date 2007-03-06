/*
 * Copyright (c) 2005 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the CService INVITE functions.
 *
 * $Id: invite.c 7905 2007-03-06 23:02:59Z jilles $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"chanserv/invite", FALSE, _modinit, _moddeinit,
	"$Id: invite.c 7905 2007-03-06 23:02:59Z jilles $",
	"Atheme Development Group <http://www.atheme.org>"
);

static void cs_cmd_invite(sourceinfo_t *si, int parc, char *parv[]);

command_t cs_invite = { "INVITE", N_("Invites you to a channel."),
                        AC_NONE, 1, cs_cmd_invite };
                                                                                   
list_t *cs_cmdtree;
list_t *cs_helptree;

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(cs_cmdtree, "chanserv/main", "cs_cmdtree");
	MODULE_USE_SYMBOL(cs_helptree, "chanserv/main", "cs_helptree");

        command_add(&cs_invite, cs_cmdtree);
	help_addentry(cs_helptree, "INVITE", "help/cservice/invite", NULL);
}

void _moddeinit()
{
	command_delete(&cs_invite, cs_cmdtree);
	help_delentry(cs_helptree, "INVITE");
}

static void cs_cmd_invite(sourceinfo_t *si, int parc, char *parv[])
{
	char *chan = parv[0];
	mychan_t *mc;

	if (si->su == NULL)
	{
		command_fail(si, fault_noprivs, _("\2%s\2 can only be executed via IRC."), "INVITE");
		return;
	}

	if (!chan)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "INVITE");
		command_fail(si, fault_needmoreparams, _("Syntax: INVITE <#channel>"));
		return;
	}

	mc = mychan_find(chan);
	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), chan);
		return;
	}

	if (metadata_find(mc, METADATA_CHANNEL, "private:close:closer"))
	{
		command_fail(si, fault_noprivs, _("\2%s\2 is closed."), chan);
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_INVITE))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
		return;
	}

	if (chanuser_find(mc->chan, si->su))
	{
		command_fail(si, fault_noprivs, _("You're already on \2%s\2."), mc->name);
		return;
	}

	if (!mc->chan)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is currently empty."), mc->name);
		return;
	}

	invite_sts(si->service->me, si->su, mc->chan);
	logcommand(si, CMDLOG_SET, "%s INVITE", mc->name);
	command_success_nodata(si, _("You have been invited to \2%s\2."), mc->name);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
