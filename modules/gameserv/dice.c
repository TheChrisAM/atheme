/*
 * Copyright (c) 2005-2007 William Pitcock <nenolod@nenolod.net>
 * Copyright (c) 2006-2007 Jilles Tjoelker <jilles@stack.nl>
 * 
 * Rights to this code are documented in doc/LICENSE.
 *
 * Dice generator.
 *
 */

#include "atheme.h"
#include "gameserv_common.h"

#include <math.h>

DECLARE_MODULE_V1("gameserv/dice", false, _modinit, _moddeinit, PACKAGE_STRING, "Atheme Development Group <http://www.atheme.org>");

static void command_dice(sourceinfo_t *si, int parc, char *parv[]);
static void command_calc(sourceinfo_t *si, int parc, char *parv[]);

command_t cmd_dice = { "ROLL", N_("Rolls one or more dice."), AC_NONE, 3, command_dice, {.path = "gameserv/roll"} };
command_t cmd_calc = { "CALC", N_("Calculate stuff."), AC_NONE, 3, command_calc, {.path = "gameserv/calc"} };

void _modinit(module_t * m)
{
	service_named_bind_command("gameserv", &cmd_dice);
	service_named_bind_command("gameserv", &cmd_calc);

	service_named_bind_command("chanserv", &cmd_dice);
	service_named_bind_command("chanserv", &cmd_calc);
}

void _moddeinit(module_unload_intent_t intent)
{
	service_named_unbind_command("gameserv", &cmd_dice);
	service_named_unbind_command("gameserv", &cmd_calc);

	service_named_unbind_command("chanserv", &cmd_dice);
	service_named_unbind_command("chanserv", &cmd_calc);
}

#define CALC_MAX_STACK		(128)

#define DICE_MAX_DICE		(100)
#define DICE_MAX_SIDES		(100)

int do_calc_expr(sourceinfo_t *si, char *expr, char *errmsg, long double *presult);
int do_calc_eval(sourceinfo_t *si, long double lhs, char oper, long double rhs, long double *out, char *errmsg);
int is_calcoper(char oper);

//
// <expr>
//   expr = {<op>(<expr>)}|{<op><expr>}
//   op   = { ~ ! d * / % \ ^ + - & $ | }
//
// [Rank 1]                        |  [Rank 4]
// ~   = One's compliment          |  + - = Add / Subtract
// !   = Logical NOT               |
// d   = Dice Generator, LOL.      |  [Rank 5]
//                                 |  &   = Bitwise AND
// [Rank 2]                        |  
// ^   = Power                     |  [Rank 6]
//                                 |  $   = Bitwise XOR (eXclusive OR)
// [Rank 3]                        |  
// * / = Multiply / Divide         |  [Rank 7]
// % \ = Modulus / Integer-divide  |  |   = Bitwise inclusive OR
//
static void eval_calc(sourceinfo_t *si, char *s_input)
{
	static char buffer[1024];

	char *ci = s_input;

	int err, braces = 0;
	long double expr;


	if (s_input == NULL)
	{
		command_fail(si, fault_badparams, _("Error: You typed an invalid expression."));
		return;
	}

	// Skip leading whitespace
	while (*ci && isspace(*ci))
		ci++;

	if (!*ci)
	{
		command_fail(si, fault_badparams, _("Error: You typed an invalid expression."));
		return;
	}

	// Validate braces
	while (*ci)
	{
		if (*ci == '(')
		{
			braces++;
		}
		else if (*ci == ')')
		{
			if (--braces < 0)
				break;	// mismatched!
		}
		else if (!isspace(*ci) && !isdigit(*ci) && *ci != '.' && !is_calcoper(*ci))
		{
			command_fail(si, fault_badparams, _("Error: You typed an invalid expression."));
			return;
		}
		ci++;
	}

	if (braces != 0)
	{
		command_fail(si, fault_badparams, _("Error: Mismatched braces '( )' in expression."));
		return;
	}

	err = do_calc_expr(si, s_input, buffer, &expr);

	if (!err)
	{
		if (strlen(s_input) > 250)
		{
			strncpy(buffer, s_input, 150);
			sprintf(&buffer[150], "...%s = %.8Lg", &s_input[strlen(s_input) - 10], expr);
		}
		else
		{
			sprintf(buffer, "%s = %.8Lg", s_input, expr);
		}
	}
	else
		return;

	gs_command_report(si, "%s", buffer);
}


//////////////////////////////////////////////////////////////////////////

enum
{
	CALCEXPR_VALUE,
	CALCEXPR_OPER
};

typedef struct _tagCalcStack
{
	long double _value;
	char _oper;
	int _rank;
	int _brace;
} CalcStack;

int do_calc_expr(sourceinfo_t *si, char *expr, char *errmsg, long double *presult)
{
	int expect = CALCEXPR_VALUE, lastrank, currank;
	char *cur = expr, *endptr, lastop, curop = ' ';
	long double total, curval = 0;
	static int nStack = 0;	// Current stack position
	static CalcStack pStack[CALC_MAX_STACK];	// Value stack

	nStack = 0;

	total = 0;
	lastrank = is_calcoper(lastop = '+');

	while (*cur)
	{
		switch (expect)
		{
		  case CALCEXPR_VALUE:
		  {
			  curval = strtol(cur, &endptr, 10);
			  if (*endptr == '.' || (curval < -2000000000 || curval > 2000000000))
				  curval = strtod(cur, &endptr);
			  if (cur == endptr)
			  {	// didn't go anywhere! o.O
				  if (*cur == '(')
				  {
					  if (curop != ' ')
					  {
						  command_fail(si, fault_badparams, _("Error: Missing expected value in expression."));
						  return 1;
					  }
					  cur++;	// skip it
					  pStack[nStack]._value = total;
					  pStack[nStack]._oper = lastop;
					  pStack[nStack]._rank = lastrank;
					  pStack[nStack++]._brace = 1;
					  total = 0;
					  lastrank = is_calcoper(lastop = '+');
				  }
				  else if ((currank = is_calcoper(curop = *cur)))
				  {
					  if (currank != 1)
					  {	// Unary: ~!
						  command_fail(si, fault_badparams, _("Error: Missing expected value in expression."));
						  return 1;
					  }
					  cur++;	// skip unary op
				  }
				  else
				  {
					  command_fail(si, fault_badparams, _("Error: You typed an invalid expression."));
					  return 1;
				  }
			  }
			  else
			  {
				  // got value... grab next operator
				  if (curop != ' ')
				  {
					  // LHS ignored -- unary ops only have RHS :)
					  if (do_calc_eval(si, 0, curop, curval, &curval, errmsg))
					  {
						  return 1;	// error
					  }
				  }
				  cur = endptr;
				  expect = CALCEXPR_OPER;
			  }
			  break;
		  }
		  case CALCEXPR_OPER:
		  {
			  currank = is_calcoper(curop = *cur);
			  if (curop == '(')
			  {
				  cur--;
				  currank = is_calcoper(curop = '*');
			  }
			  if (currank == 0)
			  {
				  if (curop == ')')
				  {
					  do
					  {
						  if (do_calc_eval(si, total, lastop, curval, &curval, errmsg))
						  {
							  return 1;	// error
						  }
						  total = pStack[--nStack]._value;
						  lastop = pStack[nStack]._oper;
						  lastrank = pStack[nStack]._rank;
					  } while (!pStack[nStack]._brace);
					  expect = CALCEXPR_OPER;
				  }
				  else
				  {
					  command_fail(si, fault_badparams, _("Error: Missing expected operator in expression."));
					  return 1;
				  }
			  }
			  else if (currank < lastrank)
			  {	// operator precedence (new over old!)
				  pStack[nStack]._value = total;
				  pStack[nStack]._oper = lastop;
				  pStack[nStack]._rank = lastrank;
				  pStack[nStack++]._brace = 0;
				  total = curval;
				  lastop = curop;
				  lastrank = currank;
				  currank = is_calcoper(curop = ' ');
				  expect = CALCEXPR_VALUE;
			  }
			  else
			  {	// previous opers have precedence -- work backwards (until start/brace)
				  while (currank >= lastrank)
				  {
					  if (do_calc_eval(si, total, lastop, curval, &curval, errmsg))
					  {
						  return 1;	// error
					  }
					  if (nStack == 0)
						  break;
					  if (pStack[nStack - 1]._brace || currank < pStack[nStack - 1]._rank)
						  break;
					  total = pStack[--nStack]._value;
					  lastop = pStack[nStack]._oper;
					  lastrank = pStack[nStack]._rank;
				  }
				  total = curval;
				  lastop = curop;
				  lastrank = currank;
				  currank = is_calcoper(curop = ' ');
				  expect = CALCEXPR_VALUE;
			  }
			  cur++;
			  break;
		  }
		}
		if (nStack >= CALC_MAX_STACK)
		{
			command_fail(si, fault_badparams, _("Error: Expression is too deeply nested."));
			return 1;
		}
		// skip whitespace
		while (*cur && isspace(*cur))
			cur++;
	}

	// Did we end on an operator instead of a value?
	if (expect == CALCEXPR_VALUE)
	{
		command_fail(si, fault_badparams, _("Error: Missing expected value in expression."));
		return 1;
	}

	// Wind back up the stack, if it exists (including last value!)
	while (nStack > -1)
	{
		if (do_calc_eval(si, total, lastop, curval, &curval, errmsg))
		{
			return 1;	// error
		}
		if (nStack == 0)
			break;
		total = pStack[--nStack]._value;
		lastop = pStack[nStack]._oper;
		// rank doesn't matter now
	}

	*presult = curval;
	return 0;		// no error
}


long double calc_dice_simple(long double lhs, long double rhs)
{
	long double i;
	long double out = 0.0;

	if (!lhs)
		lhs = 1.0;

	if (!rhs)
		rhs = 1.0;

	for (i = 0; i < lhs; i++)
	{
		int sides = floorl(rhs);
		out += 1.0 + (arc4random() % sides);
	}

	return out;
}

//////////////////////////////////////////////////////////////////////////

int do_calc_eval(sourceinfo_t *si, long double lhs, char oper, long double rhs, long double *out, char *errmsg)
{
	switch (oper)
	{
	  case '~':		// 1's compliment (unary)
		  *out = (long double)(~(long long)rhs);
		  break;
	  case '!':		// NOT (unary)
		  *out = !rhs;
		  break;
	  case 'd':
		  *out = calc_dice_simple(lhs, rhs);
		  break;
	  case '*':		// multiplication
		  *out = lhs * rhs;
		  break;
	  case '/':		// division
	  case '%':		// MOD
	  case '\\':		// DIV
		  if (rhs == 0 || (oper == '%' && ((long long)rhs == 0)))
		  {
			  command_fail(si, fault_badparams, _("Error: Cannot perform modulus or division by zero."));
			  return 1;
		  }

		  switch (oper)
		  {
		    case '/':
			    *out = lhs / rhs;
			    break;
		    case '%':
			    *out = (long double)((long long)lhs % (long long)rhs);
			    break;
		    case '\\':
			    lhs /= rhs;
			    *out = (lhs < 0) ? ceil(lhs) : floor(lhs);
			    break;
		  }
		  break;
	  case '^':		// power-of
		  *out = powl(lhs, rhs);
		  break;
	  case '+':		// addition
		  *out = lhs + rhs;
		  break;
	  case '-':		// subtraction
		  *out = lhs - rhs;
		  break;
	  case '&':		// AND (bitwise)
		  *out = (long double)((long long)lhs & (long long)rhs);
		  break;
	  case '$':		// XOR (bitwise)
		  *out = (long double)((long long)lhs ^ (long long)rhs);
		  break;
	  case '|':		// OR (bitwise)
		  *out = (long double)((long long)lhs | (long long)rhs);
		  break;
	  default:
		  command_fail(si, fault_unimplemented, _("Error: Unknown mathematical operator %c."), oper);
		  return 1;
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////

// Is 'oper' an operator? If so, return its 'rank', else '0'.
//
int is_calcoper(char oper)
{
	static char *opers = "~!d ^ */%\\ +- & $ |";
	char *c = opers;
	int rank = 1;

	while (*c && *c != oper)
	{
		if (*c == ' ')
			rank++;
		c++;
	}

	return (*c ? rank : 0);
}

/*************************************************************************************/

void eval_dice(sourceinfo_t *si, char *s_input)
{
	static char buffer[1024], result[32];

	char op = '\0', *c = s_input;
	unsigned int dice, roll, x, y, z = 0;
	double total;

	while (*c && isspace(*c))
		++c;
	if (!*c || !isdigit(*c))
	{
		gs_command_report(si, _("Syntax: XdY [ {-|+|*|/} Z ]"));
		return;
	}

	x = strtoul(c, &c, 10);
	if (x == 0 || c == NULL || ToLower(*c++) != 'd' || !isdigit(*c))
	{
		if (x < 1 || x > DICE_MAX_DICE)
		{
			gs_command_report(si, _("Only 1-100 dice may be thrown at once."));
			return;
		}

		gs_command_report(si, _("Syntax: XdY [ {-|+|*|/} Z ]"));
		return;
	}

	y = strtoul(c, &c, 10);
	if (c != NULL)
	{
		while (*c && isspace(*c))
			++c;

		if (*c && strchr("-+*/", *c) == NULL)
		{
			gs_command_report(si, _("Syntax: XdY [ {-|+|*|/} Z ]"));
			return;
		}
	}

	if (x < 1 || x > 100)
	{
		gs_command_report(si, _("Syntax: XdY [ {-|+|*|/} Z ]"));
		return;
	}

	if (y < 1 || y > DICE_MAX_SIDES)
	{
		gs_command_report(si, _("Only 1-100 sides may be used on a dice."));
		return;
	}

	if (*c)
	{
		op = *c++;

		z = strtoul(c, &c, 10);

		while (*c && isspace(*c))
			++c;

		if (*c)
		{
			gs_command_report(si, _("Syntax: XdY [ {-|+|*|/} Z ]"));
			return;
		}
		else if (op == '/' && z == 0)
		{
			gs_command_report(si, _("Can't divide by zero."));
			return;
		}
	}

	total = 0.0;
	snprintf(buffer, 1024, "\2%s\2 rolled %lud%lu: ", si->su->nick, x, y);
	for (roll = 0; roll < x; ++roll)
	{
		snprintf(result, 32, "%d ", dice = (1 + (arc4random() % y)));
		strlcat(buffer, result, sizeof(buffer));
		total += dice;
	}

	if (op == '\0')
		snprintf(result, 32, " <Total: %g>", total);
	else
	{
		snprintf(result, 32, " <Total: %g(%c%lu) = ", total, op, z);
		strlcat(buffer, result, sizeof(buffer));
		switch (op)
		{
		  case '+':
			  total += z;
			  break;
		  case '-':
			  total -= z;
			  break;
		  case '/':
			  total /= z;
			  break;
		  case '*':
			  total *= z;
			  break;
		  default:
			  break;
		}
		snprintf(result, 32, "%g>", total);
	}
	strlcat(buffer, result, sizeof(buffer));

	gs_command_report(si, "%s", buffer);
}

static void command_dice(sourceinfo_t *si, int parc, char *parv[])
{
	char *arg;
	mychan_t *mc;
	int i, times = 1;

	if (!gs_do_parameters(si, &parc, &parv, &mc))
		return;
	if (parc < 1)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "ROLL");
		command_fail(si, fault_needmoreparams, _("Syntax: ROLL [times] [dice]d<sides>"));
		return;
	}
	if (parc < 2)
		arg = parv[0];
	else
	{
		times = atoi(parv[0]);
		arg = parv[1];

		if (times > 10)
			times = 10;
	}

	for (i = 0; i < times; i++)
		eval_dice(si, arg);
}

static void command_calc(sourceinfo_t *si, int parc, char *parv[])
{
	char *arg;
	mychan_t *mc;
	int i, times = 1;

	if (!gs_do_parameters(si, &parc, &parv, &mc))
		return;
	if (parc < 1)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CALC");
		command_fail(si, fault_needmoreparams, _("Syntax: CALC [times] <expression>"));
		return;
	}
	if (parc < 2)
		arg = parv[0];
	else
	{
		times = atoi(parv[0]);
		arg = parv[1];

		if (times > 10)
			times = 10;
	}

	for (i = 0; i < times; i++)
		eval_calc(si, arg);
}

//////////////////////////////////////////////////////////////////////////
