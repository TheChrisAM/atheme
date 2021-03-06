/*
 * Copyright (c) 2005 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the CService QUIET/UNQUIET function.
 *
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"chanserv/quiet", false, _modinit, _moddeinit,
	PACKAGE_STRING,
	"Atheme Development Group <http://www.atheme.org>"
);

static void cs_cmd_quiet(sourceinfo_t *si, int parc, char *parv[]);
static void cs_cmd_unquiet(sourceinfo_t *si, int parc, char *parv[]);

command_t cs_quiet = { "QUIET", N_("Sets a quiet on a channel."),
                        AC_AUTHENTICATED, 2, cs_cmd_quiet, { .path = "cservice/quiet" } };
command_t cs_unquiet = { "UNQUIET", N_("Removes a quiet on a channel."),
			AC_AUTHENTICATED, 2, cs_cmd_unquiet, { .path = "cservice/unquiet" } };

void _modinit(module_t *m)
{
        service_named_bind_command("chanserv", &cs_quiet);
	service_named_bind_command("chanserv", &cs_unquiet);
}

void _moddeinit(module_unload_intent_t intent)
{
	service_named_unbind_command("chanserv", &cs_quiet);
	service_named_unbind_command("chanserv", &cs_unquiet);
}

static chanban_t *place_quietmask(channel_t *c, int dir, const char *hostbuf)
{
	char rhostbuf[BUFSIZE];
	chanban_t *cb = NULL;

	switch (ircd->type)
	{
	case PROTOCOL_INSPIRCD:
		mowgli_strlcpy(rhostbuf, "m:", sizeof rhostbuf);
		mowgli_strlcat(rhostbuf, hostbuf, sizeof rhostbuf);
		modestack_mode_param(chansvs.nick, c, MTYPE_ADD, 'b', rhostbuf);
		cb = chanban_add(c, rhostbuf, 'b');
		break;
	case PROTOCOL_UNREAL:
		mowgli_strlcpy(rhostbuf, "~q:", sizeof rhostbuf);
		mowgli_strlcat(rhostbuf, hostbuf, sizeof rhostbuf);
		modestack_mode_param(chansvs.nick, c, MTYPE_ADD, 'b', rhostbuf);
		cb = chanban_add(c, rhostbuf, 'b');
		break;
	default:
		modestack_mode_param(chansvs.nick, c, MTYPE_ADD, 'q', hostbuf);
		cb = chanban_add(c, hostbuf, 'q');
	}

	return cb;
}

static void make_extban(char *buf, size_t size, user_t *tu)
{
	return_if_fail(buf != NULL);
	return_if_fail(tu != NULL);

	switch (ircd->type)
	{
	case PROTOCOL_INSPIRCD:
		mowgli_strlcpy(buf, "m:", size);
		break;
	case PROTOCOL_UNREAL:
		mowgli_strlcpy(buf, "~q:", size);
		break;
	default:
		*buf = '\0';
		break;
	}

	mowgli_strlcat(buf, tu->nick, size);
	mowgli_strlcat(buf, "!", size);
	mowgli_strlcat(buf, tu->user, size);
	mowgli_strlcat(buf, "@", size);
	mowgli_strlcat(buf, tu->vhost, size);
}

static bool devoice_user(sourceinfo_t *si, mychan_t *mc, channel_t *c, user_t *tu)
{
	chanuser_t *cu;
	unsigned int flag;

	cu = chanuser_find(c, tu);
	if (cu == NULL)
		return true;
	if (cu->modes & CSTATUS_OP)
		flag = CA_OP;
	else if (cu->modes & CSTATUS_VOICE)
		flag = CA_VOICE;
	else
		flag = 0;
	if (flag != 0 && !chanacs_source_has_flag(mc, si, flag))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
		return false;
	}
	if (cu->modes & CSTATUS_OP)
		channel_mode_va(chansvs.me->me, c, 2, "-o", tu->nick);
	if (cu->modes & CSTATUS_VOICE)
		channel_mode_va(chansvs.me->me, c, 2, "-v", tu->nick);
	return true;
}

/* Notify at most this many users in private notices, otherwise channel */
#define MAX_SINGLE_NOTIFY 3

static void notify_one_victim(sourceinfo_t *si, channel_t *c, user_t *u, int dir)
{
	return_if_fail(dir == MTYPE_ADD || dir == MTYPE_DEL);

	/* fantasy command, they can see it */
	if (si->c != NULL)
		return;
	/* self */
	if (si->su == u)
		return;

	if (dir == MTYPE_ADD)
		change_notify(chansvs.nick, u,
				"You have been quieted on %s by %s",
				c->name, get_source_name(si));
	else if (dir == MTYPE_DEL)
		change_notify(chansvs.nick, u,
				"You have been unquieted on %s by %s",
				c->name, get_source_name(si));
}

static void notify_victims(sourceinfo_t *si, channel_t *c, chanban_t *cb, int dir)
{
	mowgli_node_t *n;
	chanuser_t *cu;
	mowgli_list_t ban_l = { NULL, NULL, 0 };
	mowgli_node_t ban_n;
	user_t *to_notify[MAX_SINGLE_NOTIFY];
	unsigned int to_notify_count = 0, i;

	return_if_fail(dir == MTYPE_ADD || dir == MTYPE_DEL);

	if (cb == NULL)
		return;

	/* fantasy command, they can see it */
	if (si->c != NULL)
		return;

	/* only check the newly added/removed quiet */
	mowgli_node_add(cb, &ban_n, &ban_l);

	MOWGLI_ITER_FOREACH(n, c->members.head)
	{
		cu = n->data;
		if (cu->modes & (CSTATUS_OP | CSTATUS_VOICE))
			continue;
		if (is_internal_client(cu->user))
			continue;
		if (cu->user == si->su)
			continue;
		if (next_matching_ban(c, cu->user, 'q', &ban_n))
		{
			to_notify[to_notify_count++] = cu->user;
			if (to_notify_count >= MAX_SINGLE_NOTIFY)
				break;
		}
	}

	if (to_notify_count >= MAX_SINGLE_NOTIFY)
	{
		if (dir == MTYPE_ADD)
			notice(chansvs.nick, c->name,
					"\2%s\2 quieted \2%s\2",
					get_source_name(si), cb->mask);
		else if (dir == MTYPE_DEL)
			notice(chansvs.nick, c->name,
					"\2%s\2 unquieted \2%s\2",
					get_source_name(si), cb->mask);
	}
	else
		for (i = 0; i < to_notify_count; i++)
			notify_one_victim(si, c, to_notify[i], dir);
}

static void cs_cmd_quiet(sourceinfo_t *si, int parc, char *parv[])
{
	char *channel = parv[0];
	char *target = parv[1];
	char *newtarget;
	channel_t *c = channel_find(channel);
	mychan_t *mc = mychan_find(channel);
	user_t *tu;
	chanban_t *cb;
	int n;

	if (!channel || !target)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "QUIET");
		command_fail(si, fault_needmoreparams, _("Syntax: QUIET <#channel> <nickname|hostmask>"));
		return;
	}

	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 is not registered."), channel);
		return;
	}

	if (!c)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is currently empty."), channel);
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_REMOVE))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
		return;
	}
	
	if (metadata_find(mc, "private:close:closer"))
	{
		command_fail(si, fault_noprivs, _("\2%s\2 is closed."), channel);
		return;
	}

	if ((tu = user_find_named(target)))
	{
		char hostbuf[BUFSIZE];

		if (!devoice_user(si, mc, c, tu))
			return;

		hostbuf[0] = '\0';

		mowgli_strlcat(hostbuf, "*!*@", BUFSIZE);
		mowgli_strlcat(hostbuf, tu->vhost, BUFSIZE);

		cb = place_quietmask(c, MTYPE_ADD, hostbuf);
		n = remove_ban_exceptions(si->service->me, c, tu);
		if (n > 0)
			command_success_nodata(si, _("To ensure the quiet takes effect, %d ban exception(s) matching \2%s\2 have been removed from \2%s\2."), n, tu->nick, c->name);
		notify_victims(si, c, cb, MTYPE_ADD);
		logcommand(si, CMDLOG_DO, "QUIET: \2%s\2 on \2%s\2 (for user \2%s!%s@%s\2)", hostbuf, mc->name, tu->nick, tu->user, tu->vhost);
		if (si->su == NULL || !chanuser_find(mc->chan, si->su))
			command_success_nodata(si, _("Quieted \2%s\2 on \2%s\2."), target, channel);
		return;
	}
	else if ((newtarget = pretty_mask(target)) && validhostmask(newtarget))
	{
		modestack_mode_param(chansvs.nick, c, MTYPE_ADD, 'q', newtarget);
		cb = chanban_add(c, newtarget, 'q');
		notify_victims(si, c, cb, MTYPE_ADD);
		logcommand(si, CMDLOG_DO, "QUIET: \2%s\2 on \2%s\2", newtarget, mc->name);
		if (si->su == NULL || !chanuser_find(mc->chan, si->su))
			command_success_nodata(si, _("Quieted \2%s\2 on \2%s\2."), newtarget, channel);
		return;
	}
	else
	{
		command_fail(si, fault_badparams, _("Invalid nickname/hostmask provided: \2%s\2"), target);
		command_fail(si, fault_badparams, _("Syntax: QUIET <#channel> <nickname|hostmask>"));
		return;
	}
}

static void cs_cmd_unquiet(sourceinfo_t *si, int parc, char *parv[])
{
        char *channel = parv[0];
        char *target = parv[1];
        channel_t *c = channel_find(channel);
	mychan_t *mc = mychan_find(channel);
	user_t *tu;
	chanban_t *cb;

	if (!channel)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "UNQUIET");
		command_fail(si, fault_needmoreparams, _("Syntax: UNQUIET <#channel> <nickname|hostmask>"));
		return;
	}

	if (!target)
	{
		if (si->su == NULL)
		{
			command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "UNQUIET");
			command_fail(si, fault_needmoreparams, _("Syntax: UNQUIET <#channel> <nickname|hostmask>"));
			return;
		}
		target = si->su->nick;
	}

	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 is not registered."), channel);
		return;
	}

	if (!c)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is currently empty."), channel);
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_REMOVE))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
		return;
	}

	if ((tu = user_find_named(target)))
	{
		mowgli_node_t *n, *tn;
		char hostbuf2[BUFSIZE];
		int count = 0;

		make_extban(hostbuf2, sizeof hostbuf2, tu);
		for (n = next_matching_ban(c, tu, 'q', c->bans.head); n != NULL; n = next_matching_ban(c, tu, 'q', tn))
		{
			tn = n->next;
			cb = n->data;

			logcommand(si, CMDLOG_DO, "UNQUIET: \2%s\2 on \2%s\2 (for user \2%s\2)", cb->mask, mc->name, hostbuf2);
			modestack_mode_param(chansvs.nick, c, MTYPE_DEL, cb->type, cb->mask);
			chanban_delete(cb);
			count++;
		}
		if (count > 0)
		{
			/* one notification only */
			if (chanuser_find(c, tu))
				notify_one_victim(si, c, tu, MTYPE_DEL);
			command_success_nodata(si, _("Unquieted \2%s\2 on \2%s\2 (%d ban%s removed)."),
				target, channel, count, (count != 1 ? "s" : ""));
		}
		else
			command_success_nodata(si, _("No quiets found matching \2%s\2 on \2%s\2."), target, channel);
		return;
	}
#warning support UNQUIET against extbans
	else if ((cb = chanban_find(c, target, 'q')) != NULL || validhostmask(target))
	{
		if (cb)
		{
			modestack_mode_param(chansvs.nick, c, MTYPE_DEL, 'q', target);
			notify_victims(si, c, cb, MTYPE_DEL);
			chanban_delete(cb);
			logcommand(si, CMDLOG_DO, "UNQUIET: \2%s\2 on \2%s\2", target, mc->name);
			if (si->su == NULL || !chanuser_find(mc->chan, si->su))
				command_success_nodata(si, _("Unquieted \2%s\2 on \2%s\2."), target, channel);
		}
		else
			command_fail(si, fault_nosuch_key, _("No such quiet \2%s\2 on \2%s\2."), target, channel);

		return;
	}
        else
        {
		command_fail(si, fault_badparams, _("Invalid nickname/hostmask provided: \2%s\2"), target);
		command_fail(si, fault_badparams, _("Syntax: UNQUIET <#channel> [nickname|hostmask]"));
		return;
        }
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
