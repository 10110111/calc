/*
 * Copyright (c) 1996 David I. Bell
 * Permission is granted to use, distribute, or modify this source,
 * provided that this copyright notice remains intact.
 *
 * Module to generate opcodes from the input tokens.
 */

#include "have_unistd.h"
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif

#include "calc.h"
#include "token.h"
#include "symbol.h"
#include "label.h"
#include "opcodes.h"
#include "string.h"
#include "func.h"
#include "conf.h"

static BOOL rdonce;	/* TRUE => do not reread this file */

FUNC *curfunc;

static BOOL getfilename(char *name, BOOL msg_ok, BOOL *once);
static BOOL getid(char *buf);
static void getshowstatement(void);
static void getfunction(void);
static void getbody(LABEL *contlabel, LABEL *breaklabel,
		    LABEL *nextcaselabel, LABEL *defaultlabel, BOOL toplevel);
static void getdeclarations(void);
static void getstatement(LABEL *contlabel, LABEL *breaklabel,
			 LABEL *nextcaselabel, LABEL *defaultlabel);
static void getobjdeclaration(int symtype);
static void getoneobj(long index, int symtype);
static void getobjvars(char *name, int symtype);
static void getmatdeclaration(int symtype);
static void getonematrix(int symtype);
static void creatematrix(void);
static void getsimplebody(void);
static void getonedeclaration(int type);
static void getcondition(void);
static void getmatargs(void);
static void getelement(void);
static void usesymbol(char *name, BOOL autodef);
static void definesymbol(char *name, int symtype);
static void getcallargs(char *name);
static void do_changedir(void);
static int getexprlist(void);
static int getassignment(void);
static int getaltcond(void);
static int getorcond(void);
static int getandcond(void);
static int getrelation(void);
static int getsum(void);
static int getproduct(void);
static int getorexpr(void);
static int getandexpr(void);
static int getshiftexpr(void);
static int getterm(void);
static int getidexpr(BOOL okmat, BOOL autodef);
static long getinitlist(void);


/*
 * Read all the commands from an input file.
 * These are either declarations, or else are commands to execute now.
 * In general, commands are terminated by newlines or semicolons.
 * Exceptions are function definitions and escaped newlines.
 * Commands are read and executed until the end of file.
 * The toplevel flag indicates whether we are at the top interactive level.
 */
void
getcommands(BOOL toplevel)
{
	char name[PATHSIZE+1];	/* program name */

	if (!toplevel)
		enterfilescope();
	for (;;) {
		(void) tokenmode(TM_NEWLINES);
		switch (gettoken()) {

		case T_DEFINE:
			getfunction();
			break;

		case T_EOF:
			if (!toplevel)
				exitfilescope();
			return;

		case T_HELP:
			if (!getfilename(name, FALSE, NULL)) {
				strcpy(name, DEFAULTCALCHELP);
			}
			givehelp(name);
			break;

		case T_READ:
			if (!getfilename(name, TRUE, &rdonce))
				break;
			if (!allow_read) {
				scanerror(T_NULL,
				    "read command disallowed by -m mode\n");
				break;
			}
			switch (opensearchfile(name,calcpath,CALCEXT,rdonce)) {
			case 0:
				getcommands(FALSE);
				break;
			case 1:
				/* previously read and -once was given */
				break;
			case -2:
				scanerror(T_NULL, "Maximum input depth reached");
				break;
			default:
				scanerror(T_NULL, "Cannot open \"%s\"\n", name);
				break;
			}
			break;

		case T_WRITE:
			if (!getfilename(name, TRUE, NULL))
				break;
			if (!allow_write) {
				scanerror(T_NULL,
				    "write command disallowed by -m mode\n");
				break;
			}
			if (writeglobals(name))
				scanerror(T_NULL, "Error writing \"%s\"\n", name);
			break;

		case T_CD:
			do_changedir();
			break;
		case T_NEWLINE:
		case T_SEMICOLON:
			break;

		default:
			rescantoken();
			initstack();
			if (evaluate(FALSE))
				updateoldvalue(curfunc);
		}
	}
}


/*
 * Evaluate a line of statements.
 * This is done by treating the current line as a function body,
 * compiling it, and then executing it.  Returns TRUE if the line
 * successfully compiled and executed.  The last expression result
 * is saved in the f_savedvalue element of the current function.
 * The nestflag variable should be FALSE for the outermost evaluation
 * level, and TRUE for all other calls (such as the 'eval' function).
 * The function name begins with an asterisk to indicate specialness.
 *
 * given:
 *	nestflag		TRUE if this is a nested evaluation
 */
BOOL
evaluate(BOOL nestflag)
{
	char *funcname;
	BOOL gotstatement;
	int loop = 1;		/* 0 => end the main while loop */

	funcname = (nestflag ? "**" : "*");
	beginfunc(funcname, nestflag);
	gotstatement = FALSE;
	if (nestflag)
		(void) tokenmode(TM_DEFAULT);
	while (loop) {
		switch (gettoken()) {
			case T_SEMICOLON:
				break;

			case T_NEWLINE:
			case T_EOF:
				loop = 0;
				break;

			case T_GLOBAL:
			case T_LOCAL:
			case T_STATIC:
				if (gotstatement) {
					scanerror(T_SEMICOLON, "Declarations must be used before code");
					return FALSE;
				}
				rescantoken();
				getdeclarations();
				break;

			default:
				rescantoken();
				getstatement(NULL_LABEL, NULL_LABEL,
					NULL_LABEL, NULL_LABEL);
				gotstatement = TRUE;
		}
	}
	addop(OP_UNDEF);
	addop(OP_RETURN);
	checklabels();
	if (errorcount)
		return FALSE;
	calculate(curfunc, 0);
	return TRUE;
}


/*
 * Get a function declaration.
 * func = name '(' '' | name [ ',' name] ... ')' simplebody
 *	| name '(' '' | name [ ',' name] ... ')' body.
 */
static void
getfunction(void)
{
	char *name;		/* parameter name */
	int type;		/* type of token read */

	(void) tokenmode(TM_DEFAULT);
	if (gettoken() != T_SYMBOL) {
		scanerror(T_NULL, "Function name expected");
		return;
	}
	name = tokenstring();
	type = getbuiltinfunc(name);
	if (type >= 0) {
		scanerror(T_SEMICOLON, "Using builtin function name");
		return;
	}
	beginfunc(name, FALSE);
	enterfuncscope();
	if (gettoken() != T_LEFTPAREN) {
		scanerror(T_SEMICOLON, "Left parenthesis expected for function");
		return;
	}
	for (;;) {
		type = gettoken();
		if (type == T_RIGHTPAREN)
			break;
		if (type != T_SYMBOL) {
			scanerror(T_COMMA, "Bad function definition");
			return;
		}
		name = tokenstring();
		switch (symboltype(name)) {
			case SYM_UNDEFINED:
			case SYM_GLOBAL:
			case SYM_STATIC:
				(void) addparam(name);
				break;
			default:
				scanerror(T_NULL, "Parameter \"%s\" is already defined", name);
		}
		type = gettoken();
		if (type == T_RIGHTPAREN)
			break;
		if (type != T_COMMA) {
			scanerror(T_COMMA, "Bad function definition");
			return;
		}
	}
	switch (gettoken()) {
		case T_ASSIGN:
			rescantoken();
			getsimplebody();
			break;
		case T_LEFTBRACE:
			rescantoken();
			getbody(NULL_LABEL, NULL_LABEL, NULL_LABEL,
				NULL_LABEL, TRUE);
			break;
		default:
			scanerror(T_NULL,
				"Left brace or equals sign expected for function");
			return;
	}
	addop(OP_UNDEF);
	addop(OP_RETURN);
	endfunc();
	exitfuncscope();
}


/*
 * Get a simple assignment style body for a function declaration.
 * simplebody = '=' assignment '\n'.
 */
static void
getsimplebody(void)
{
	if (gettoken() != T_ASSIGN) {
		scanerror(T_SEMICOLON,
		    "Missing equals for simple function body");
		return;
	}
	(void) tokenmode(TM_NEWLINES);
	(void) getexprlist();
	addop(OP_RETURN);
	if (gettoken() != T_SEMICOLON)
		rescantoken();
	if (gettoken() != T_NEWLINE)
		scanerror(T_NULL, "Illegal function definition");
}


/*
 * Get the body of a function, or a subbody of a function.
 * body = '{' [ declarations ] ... [ statement ] ... '}'
 *	| [ declarations ] ... [statement ] ... '\n'
 */
static void
getbody(LABEL *contlabel, LABEL *breaklabel, LABEL *nextcaselabel, LABEL *defaultlabel, BOOL toplevel)
{
	BOOL gotstatement;	/* TRUE if seen a real statement yet */
	int oldmode;

	if (gettoken() != T_LEFTBRACE) {
		scanerror(T_SEMICOLON, "Missing left brace for function body");
		return;
	}
	oldmode = tokenmode(TM_DEFAULT);
	gotstatement = FALSE;
	while (TRUE) {
		switch (gettoken()) {
		case T_RIGHTBRACE:
			(void) tokenmode(oldmode);
			return;

		case T_GLOBAL:
		case T_LOCAL:
		case T_STATIC:
			if (!toplevel) {
				scanerror(T_SEMICOLON, "Declarations must be at the top of the function");
				return;
			}
			if (gotstatement) {
				scanerror(T_SEMICOLON, "Declarations must be used before code");
				return;
			}
			rescantoken();
			getdeclarations();
			break;

		default:
			rescantoken();
			getstatement(contlabel, breaklabel, nextcaselabel, defaultlabel);
			gotstatement = TRUE;
		}
	}
}


/*
 * Get a line of possible local, global, or static variable declarations.
 * declarations = { LOCAL | GLOBAL | STATIC } onedeclaration
 *	[ ',' onedeclaration ] ... ';'.
 */
static void
getdeclarations(void)
{
	int type;

	type = gettoken();

	if ((type != T_LOCAL) && (type != T_GLOBAL) && (type != T_STATIC)) {
		rescantoken();
		return;
	}

	while (TRUE) {
		getonedeclaration(type);

		switch (gettoken()) {
			case T_COMMA:
				continue;
			case T_NEWLINE:
				rescantoken();
			case T_SEMICOLON:
				return;

			default:
				scanerror(T_SEMICOLON, "Bad syntax in declaration statement");
				return;
		}
	}
}


/*
 * Get a single declaration of a symbol of the specified type.
 * onedeclaration = name [ '=' getassignment ]
 *	| 'obj' type name [ '=' objvalues ]
 *	| 'mat' name '[' matargs ']' [ '=' matvalues ].
 */
static void
getonedeclaration(int type)
{
	char *name;		/* name of symbol seen */
	int symtype;		/* type of symbol */
	int vartype;		/* type of variable being defined */
	LABEL label;

	switch (type) {
		case T_LOCAL:
			symtype = SYM_LOCAL;
			break;
		case T_GLOBAL:
			symtype = SYM_GLOBAL;
			break;
		case T_STATIC:
			symtype = SYM_STATIC;
			clearlabel(&label);
			addoplabel(OP_INITSTATIC, &label);
			break;
		default:
			symtype = SYM_UNDEFINED;
			break;
	}

	vartype = gettoken();
	switch (vartype) {
		case T_SYMBOL:
			name = tokenstring();
			definesymbol(name, symtype);
			break;

		case T_MAT:
			addopone(OP_DEBUG, linenumber());
			getmatdeclaration(symtype);
			addop(OP_POP);
			if (symtype == SYM_STATIC)
				setlabel(&label);
			return;

		case T_OBJ:
			addopone(OP_DEBUG, linenumber());
			getobjdeclaration(symtype);
			addop(OP_POP);
			if (symtype == SYM_STATIC)
				setlabel(&label);
			return;

		default:
			scanerror(T_COMMA, "Bad syntax for declaration");
			return;
	}

	if (gettoken() != T_ASSIGN) {
		rescantoken();
		if (symtype == SYM_STATIC)
			setlabel(&label);
		return;
	}

	/*
	 * Initialize the variable with the expression.  If the variable is
	 * static, arrange for the initialization to only be done once.
	 */
	addopone(OP_DEBUG, linenumber());
	usesymbol(name, FALSE);
	getassignment();
	addop(OP_ASSIGNPOP);
	if (symtype == SYM_STATIC)
		setlabel(&label);
}


/*
 * Get a statement.
 * statement = IF condition statement [ELSE statement]
 *	| FOR '(' [assignment] ';' [assignment] ';' [assignment] ')' statement
 *	| WHILE condition statement
 *	| DO statement WHILE condition ';'
 *	| SWITCH condition '{' [caseclause] ... '}'
 *	| CONTINUE ';'
 *	| BREAK ';'
 *	| RETURN assignment ';'
 *	| GOTO label ';'
 *	| MAT name '[' value [ ':' value ] [',' value [ ':' value ] ] ']' ';'
 *	| OBJ type '{' arg [ ',' arg ] ... '}' ] ';'
 *	| OBJ type name [ ',' name ] ';'
 *	| PRINT assignment [, assignment ] ... ';'
 *	| QUIT [ string ] ';'
 *	| SHOW item ';'
 *	| body
 *	| assignment ';'
 *	| label ':' statement
 *	| ';'.
 *
 * given:
 *	contlabel		label for continue statement
 *	breaklabel		label for break statement
 *	nextcaselabel		label for next case statement
 *	defaultlabel		label for default case
 */
static void
getstatement(LABEL *contlabel, LABEL *breaklabel, LABEL *nextcaselabel, LABEL *defaultlabel)
{
	LABEL label1, label2, label3, label4;	/* locations for jumps */
	int type;
	BOOL printeol;

	addopone(OP_DEBUG, linenumber());
	switch (gettoken()) {
	case T_NEWLINE:
	case T_SEMICOLON:
		return;

	case T_RIGHTBRACE:
		scanerror(T_NULL, "Extraneous right brace");
		return;

	case T_CONTINUE:
		if (contlabel == NULL_LABEL) {
			scanerror(T_SEMICOLON, "CONTINUE not within FOR, WHILE, or DO");
			return;
		}
		addoplabel(OP_JUMP, contlabel);
		break;

	case T_BREAK:
		if (breaklabel == NULL_LABEL) {
			scanerror(T_SEMICOLON, "BREAK not within FOR, WHILE, or DO");
			return;
		}
		addoplabel(OP_JUMP, breaklabel);
		break;

	case T_GOTO:
		if (gettoken() != T_SYMBOL) {
			scanerror(T_SEMICOLON, "Missing label in goto");
			return;
		}
		addop(OP_JUMP);
		addlabel(tokenstring());
		break;

	case T_RETURN:
		switch (gettoken()) {
			case T_NEWLINE:
			case T_SEMICOLON:
				addop(OP_UNDEF);
				addop(OP_RETURN);
				return;
			default:
				rescantoken();
				(void) getexprlist();
				if (curfunc->f_name[0] == '*')
					addop(OP_SAVE);
				addop(OP_RETURN);
		}
		break;

	case T_LEFTBRACE:
		rescantoken();
		getbody(contlabel, breaklabel, nextcaselabel, defaultlabel, FALSE);
		return;

	case T_IF:
		clearlabel(&label1);
		clearlabel(&label2);
		getcondition();
		addoplabel(OP_JUMPEQ, &label1);
		getstatement(contlabel, breaklabel, NULL_LABEL, NULL_LABEL);
		if (gettoken() != T_ELSE) {
			setlabel(&label1);
			rescantoken();
			return;
		}
		addoplabel(OP_JUMP, &label2);
		setlabel(&label1);
		getstatement(contlabel, breaklabel, NULL_LABEL, NULL_LABEL);
		setlabel(&label2);
		return;

	case T_FOR:	/* for (a; b; c) x */
		clearlabel(&label1);
		clearlabel(&label2);
		clearlabel(&label3);
		clearlabel(&label4);
		contlabel = NULL_LABEL;
		breaklabel = &label4;
		if (gettoken() != T_LEFTPAREN) {
			scanerror(T_SEMICOLON, "Left parenthesis expected");
			return;
		}
		if (gettoken() != T_SEMICOLON) {	/* have 'a' part */
			rescantoken();
			(void) getexprlist();
			addop(OP_POP);
			if (gettoken() != T_SEMICOLON) {
				scanerror(T_SEMICOLON, "Missing semicolon");
				return;
			}
		}
		if (gettoken() != T_SEMICOLON) {	/* have 'b' part */
			setlabel(&label1);
			contlabel = &label1;
			rescantoken();
			(void) getexprlist();
			addoplabel(OP_JUMPNE, &label3);
			addoplabel(OP_JUMP, breaklabel);
			if (gettoken() != T_SEMICOLON) {
				scanerror(T_SEMICOLON, "Missing semicolon");
				return;
			}
		}
		if (gettoken() != T_RIGHTPAREN) {	/* have 'c' part */
			if (label1.l_offset <= 0)
				addoplabel(OP_JUMP, &label3);
			setlabel(&label2);
			contlabel = &label2;
			rescantoken();
			(void) getexprlist();
			addop(OP_POP);
			if (label1.l_offset > 0)
				addoplabel(OP_JUMP, &label1);
			if (gettoken() != T_RIGHTPAREN) {
				scanerror(T_SEMICOLON, "Right parenthesis expected");
				return;
			}
		}
		setlabel(&label3);
		if (contlabel == NULL_LABEL)
			contlabel = &label3;
		getstatement(contlabel, breaklabel, NULL_LABEL, NULL_LABEL);
		addoplabel(OP_JUMP, contlabel);
		setlabel(breaklabel);
		return;

	case T_WHILE:
		contlabel = &label1;
		breaklabel = &label2;
		clearlabel(contlabel);
		clearlabel(breaklabel);
		setlabel(contlabel);
		getcondition();
		addoplabel(OP_JUMPEQ, breaklabel);
		getstatement(contlabel, breaklabel, NULL_LABEL, NULL_LABEL);
		addoplabel(OP_JUMP, contlabel);
		setlabel(breaklabel);
		return;

	case T_DO:
		contlabel = &label1;
		breaklabel = &label2;
		clearlabel(contlabel);
		clearlabel(breaklabel);
		clearlabel(&label3);
		setlabel(&label3);
		getstatement(contlabel, breaklabel, NULL_LABEL, NULL_LABEL);
		if (gettoken() != T_WHILE) {
			scanerror(T_SEMICOLON, "WHILE keyword expected for DO statement");
			return;
		}
		setlabel(contlabel);
		getcondition();
		addoplabel(OP_JUMPNE, &label3);
		setlabel(breaklabel);
		return;

	case T_SWITCH:
		breaklabel = &label1;
		nextcaselabel = &label2;
		defaultlabel = &label3;
		clearlabel(breaklabel);
		clearlabel(nextcaselabel);
		clearlabel(defaultlabel);
		getcondition();
		if (gettoken() != T_LEFTBRACE) {
			scanerror(T_SEMICOLON, "Missing left brace for switch statement");
			return;
		}
		addoplabel(OP_JUMP, nextcaselabel);
		rescantoken();
		getstatement(contlabel, breaklabel, nextcaselabel, defaultlabel);
		addoplabel(OP_JUMP, breaklabel);
		setlabel(nextcaselabel);
		if (defaultlabel->l_offset > 0)
			addoplabel(OP_JUMP, defaultlabel);
		else
			addop(OP_POP);
		setlabel(breaklabel);
		return;

	case T_CASE:
		if (nextcaselabel == NULL_LABEL) {
			scanerror(T_SEMICOLON, "CASE not within SWITCH statement");
			return;
		}
		clearlabel(&label1);
		addoplabel(OP_JUMP, &label1);
		setlabel(nextcaselabel);
		clearlabel(nextcaselabel);
		(void) getexprlist();
		if (gettoken() != T_COLON) {
			scanerror(T_SEMICOLON, "Colon expected after CASE expression");
			return;
		}
		addoplabel(OP_CASEJUMP, nextcaselabel);
		setlabel(&label1);
		getstatement(contlabel, breaklabel, nextcaselabel, defaultlabel);
		return;

	case T_DEFAULT:
		if (gettoken() != T_COLON) {
			scanerror(T_SEMICOLON, "Colon expected after DEFAULT keyword");
			return;
		}
		if (defaultlabel == NULL_LABEL) {
			scanerror(T_SEMICOLON, "DEFAULT not within SWITCH statement");
			return;
		}
		if (defaultlabel->l_offset > 0) {
			scanerror(T_SEMICOLON, "Multiple DEFAULT clauses in SWITCH");
			return;
		}
		clearlabel(&label1);
		addoplabel(OP_JUMP, &label1);
		setlabel(defaultlabel);
		addop(OP_POP);
		setlabel(&label1);
		getstatement(contlabel, breaklabel, nextcaselabel, defaultlabel);
		return;

	case T_ELSE:
		scanerror(T_SEMICOLON, "ELSE without preceeding IF");
		return;

	case T_SHOW:
		getshowstatement();
		break;

	case T_PRINT:
		printeol = TRUE;
		for (;;) {
			switch (gettoken()) {
				case T_RIGHTBRACE:
				case T_NEWLINE:
				case T_EOF:
					rescantoken();
					/*FALLTHRU*/
				case T_SEMICOLON:
					if (printeol)
						addop(OP_PRINTEOL);
					return;
				case T_COMMA:
					addop(OP_PRINTSPACE);
					/*FALLTHRU*/
				case T_COLON:
					printeol = FALSE;
					break;
				case T_STRING:
					printeol = TRUE;
					addopptr(OP_PRINTSTRING, tokenstring());
					break;
				default:
					printeol = TRUE;
					rescantoken();
					(void) getassignment();
					addopone(OP_PRINT, (long) PRINT_NORMAL);
			}
		}

	case T_QUIT:
		switch (gettoken()) {
			case T_STRING:
				addopptr(OP_QUIT, tokenstring());
				break;
			default:
				addopptr(OP_QUIT, NULL);
				rescantoken();
		}
		break;

	case T_SYMBOL:
		if (nextchar() == ':') {	/****HACK HACK ****/
			definelabel(tokenstring());
			getstatement(contlabel, breaklabel,
				NULL_LABEL, NULL_LABEL);
			return;
		}
		reread();
		/* fall into default case */

	default:
		rescantoken();
		type = getexprlist();
		if (contlabel || breaklabel || (curfunc->f_name[0] != '*')) {
			addop(OP_POP);
			break;
		}
		addop(OP_SAVE);
		if (isassign(type) || (curfunc->f_name[1] != '\0')) {
			addop(OP_POP);
			break;
		}
		addop(OP_PRINTRESULT);
		break;
	}
	switch (gettoken()) {
		case T_RIGHTBRACE:
		case T_NEWLINE:
		case T_EOF:
			rescantoken();
			break;
		case T_SEMICOLON:
			break;
		default:
			scanerror(T_SEMICOLON, "Semicolon expected");
			break;
	}
}


/*
 * Read in an object declaration.
 * This is of the following form:
 *	OBJ type [ '{' id [ ',' id ] ... '}' ]  [ objlist ].
 * The OBJ keyword has already been read.  Symtype is SYM_UNDEFINED if this
 * is an OBJ statement, otherwise this is part of a declaration which will
 * define new symbols with the specified type.
 */
static void
getobjdeclaration(int symtype)
{
	char *name;			/* name of object type */
	int count;			/* number of elements */
	int index;			/* current index */
	int i;				/* loop counter */
	BOOL err;			/* error flag */
	int indices[MAXINDICES];	/* indices for elements */

	err = FALSE;
	if (gettoken() != T_SYMBOL) {
		scanerror(T_SEMICOLON, "Object type name missing");
		return;
	}
	name = addliteral(tokenstring());
	if (gettoken() != T_LEFTBRACE) {
		rescantoken();
		getobjvars(name, symtype);
		return;
	}
	/*
	 * Read in the definition of the elements of the object.
	 */
	count = 0;
	for (;;) {
		if (gettoken() != T_SYMBOL) {
			scanerror(T_SEMICOLON, "Missing element name in OBJ statement");
			return;
		}
		index = addelement(tokenstring());
		for (i = 0; i < count; i++) {
			if (indices[i] == index) {
				scanerror(T_NULL, "Duplicate element name \"%s\"", tokenstring());
				err = TRUE;
				break;
			}
		}
		indices[count++] = index;
		switch (gettoken()) {
			case T_RIGHTBRACE:
				if (!err) {
					(void) defineobject(name, indices, count);
					getobjvars(name, symtype);
					return;
				}
				scanerror(T_SEMICOLON, "Error in object definition");
			case T_COMMA:
			case T_SEMICOLON:
			case T_NEWLINE:
				break;
			default:
				scanerror(T_SEMICOLON, "Bad object element definition");
				return;
		}
	}
}

static void
getoneobj(long index, int symtype)
{
	char *symname;

	if (gettoken() == T_SYMBOL) {
		if (symtype == SYM_UNDEFINED) {
			rescantoken();
			(void) getidexpr(FALSE, TRUE);
		}
		else {
			symname = tokenstring();
			definesymbol(symname, symtype);
			usesymbol(symname, FALSE);
		}
		while (gettoken() == T_COMMA);
		rescantoken();
		getoneobj(index, symtype);
		addop(OP_ASSIGN);
		return;
	}
	rescantoken();
	addopone(OP_OBJCREATE, index);
	if (gettoken() == T_ASSIGN)
		(void) getinitlist();
	else
		rescantoken();
}

/*
 * Routine to collect a set of variables for the specified object type
 * and initialize them as being that type of object.
 * Here
 *	objlist = name initlist [ ',' name initlist ] ... ';'.
 * If symtype is SYM_UNDEFINED, then this is an OBJ statement where the
 * values can be any variable expression, and no symbols are to be defined.
 * Otherwise this is part of a declaration, and the variables must be raw
 * symbol names which are defined with the specified symbol type.
 *
 * given:
 *	name		object name
 *	symtype		type of symbol to collect for
 */
static void
getobjvars(char *name, int symtype)
{
	long index;		/* index for object */

	index = checkobject(name);
	if (index < 0) {
		scanerror(T_SEMICOLON, "Object %s has not been defined yet", name);
		return;
	}
	for (;;) {
		getoneobj(index, symtype);
		if (gettoken() != T_COMMA) {
			rescantoken();
			return;
		}
		addop(OP_POP);
	}
}


static void
getmatdeclaration(int symtype)
{


	for(;;) {
		getonematrix(symtype);
		if (gettoken() != T_COMMA) {
			rescantoken();
			return;
		}
		addop(OP_POP);
	}
}

static
void getonematrix(int symtype)
{
	long dim;
	long index;
	long count;
	unsigned long patchpc;
	char *name;

	if (gettoken() == T_SYMBOL) {
		if (symtype == SYM_UNDEFINED) {
			rescantoken();
			(void) getidexpr(FALSE, TRUE);
		}
		else {
			name = tokenstring();
			definesymbol(name, symtype);
			usesymbol(name, FALSE);
		}
		while (gettoken() == T_COMMA);
		rescantoken();
		getonematrix(symtype);
		addop(OP_ASSIGN);
		return;
	}
	rescantoken();

	if (gettoken() != T_LEFTBRACKET) {
		addopone(OP_MATCREATE, 0);
		rescantoken();
		return;
	}
	dim = 1;

	/*
	 * If there are no bounds given for the matrix, then they must be
	 * implicitly defined by a list of initialization values.  Put in
	 * a dummy number in the opcode stream for the bounds and remember
	 * its location.  After we know how many values are in the list, we
	 * will patch the correct value back into the opcode.
	 */
	if (gettoken() == T_RIGHTBRACKET) {
		clearopt();
		patchpc = curfunc->f_opcodecount + 1;
		addopone(OP_NUMBER, (long) -1);
		clearopt();
		addop(OP_ZERO);
		addopone(OP_MATCREATE, dim);
		addop(OP_ZERO);
		addop(OP_INITFILL);
		count = 0;
		if (gettoken() == T_ASSIGN)
			count = getinitlist();
		else
			rescantoken();
		index = addqconstant(itoq(count));
		if (index < 0)
			math_error("Cannot allocate constant");
		curfunc->f_opcodes[patchpc] = index;
		return;
	}

	/*
	 * This isn't implicit, so we expect expressions for the bounds.
	 */
	rescantoken();
	creatematrix();
	if (gettoken() == T_ASSIGN)
		(void) getinitlist();
	else
		rescantoken();
	return;
}
	

static void
creatematrix(void) 
{
	long dim;

	dim = 1;

	while (TRUE) {
		(void) getassignment();
		switch (gettoken()) {
			case T_RIGHTBRACKET:
			case T_COMMA:
				rescantoken();
				addop(OP_ONE);
				addop(OP_SUB);
				addop(OP_ZERO);
				break;
			case T_COLON:
				(void) getassignment();
				break;
			default:
				rescantoken();
		}
		switch (gettoken()) {
			case T_RIGHTBRACKET:
				addopone(OP_MATCREATE, dim);
				if (gettoken() == T_LEFTBRACKET)
					creatematrix();
				else {
					rescantoken();
					addop(OP_ZERO);
				}
				addop(OP_INITFILL);
				return;
			case T_COMMA:
				if (++dim <= MAXDIM)
					break;
				scanerror(T_SEMICOLON, "Only %ld dimensions allowed", MAXDIM);
				return;
			default:
				scanerror(T_SEMICOLON, "Illegal matrix definition");
				return;
		}
	}
}


/*
 * Get an optional initialization list for a matrix or object definition.
 * Returns the number of elements that are in the list, or -1 on parse error.
 *	initlist = { assignment [ , assignment ] ... }.
 */
static long
getinitlist(void)
{
	long index;
	int oldmode;

	oldmode = tokenmode(TM_DEFAULT);

	if (gettoken() != T_LEFTBRACE) {
		scanerror(T_SEMICOLON, "Missing brace for initialization list");
		(void) tokenmode(oldmode);
		return -1;
	}

	for (index = 0; ; index++) {
		switch(gettoken()) {
			case T_COMMA:
				continue;
			case T_RIGHTBRACE:
				(void) tokenmode(oldmode);
				return index;
			case T_LEFTBRACE:
				rescantoken();
				addop(OP_DUPLICATE);
				addopone(OP_ELEMADDR, index);
				(void) getinitlist();
				break;
			default:
				rescantoken();
				getassignment();
		}
		addopone(OP_ELEMINIT, index);
		switch (gettoken()) {
			case T_COMMA:
				continue;

			case T_RIGHTBRACE:
				(void) tokenmode(oldmode);
				return index;

			default:
				scanerror(T_SEMICOLON, "Bad initialization list");
				(void) tokenmode(oldmode);
				return -1;
		}
	}
}


/*
 * Get a condition.
 * condition = '(' assignment ')'.
 */
static void
getcondition(void)
{
	if (gettoken() != T_LEFTPAREN) {
		scanerror(T_SEMICOLON, "Missing left parenthesis for condition");
		return;
	}
	(void) getexprlist();
	if (gettoken() != T_RIGHTPAREN) {
		scanerror(T_SEMICOLON, "Missing right parenthesis for condition");
		return;
	}
}


/*
 * Get an expression list consisting of one or more expressions,
 * separated by commas.  The value of the list is that of the final expression.
 * This is the top level routine for parsing expressions.
 * Returns flags describing the type of assignment or expression found.
 * exprlist = assignment [ ',' assignment ] ...
 */
static int
getexprlist(void)
{
	int	type;

	type = getassignment();
	while (gettoken() == T_COMMA) {
		addop(OP_POP);
		(void) getassignment();
		type = EXPR_RVALUE;
	}
	rescantoken();
	return type;
}


/*
 * Get an assignment (or possibly just an expression).
 * Returns flags describing the type of assignment or expression found.
 * assignment = lvalue '=' assignment
 *	| lvalue '+=' assignment
 *	| lvalue '-=' assignment
 *	| lvalue '*=' assignment
 *	| lvalue '/=' assignment
 *	| lvalue '%=' assignment
 *	| lvalue '//=' assignment
 *	| lvalue '&=' assignment
 *	| lvalue '|=' assignment
 *	| lvalue '<<=' assignment
 *	| lvalue '>>=' assignment
 *	| lvalue '^=' assignment
 *	| lvalue '**=' assignment
 *	| orcond.
 */
static int
getassignment(void)
{
	int type;		/* type of expression */
	long op;		/* opcode to generate */

	type = getaltcond();
	switch (gettoken()) {
		case T_ASSIGN:		op = 0; break;
		case T_PLUSEQUALS:	op = OP_ADD; break;
		case T_MINUSEQUALS:	op = OP_SUB; break;
		case T_MULTEQUALS:	op = OP_MUL; break;
		case T_DIVEQUALS:	op = OP_DIV; break;
		case T_SLASHSLASHEQUALS: op = OP_QUO; break;
		case T_MODEQUALS:	op = OP_MOD; break;
		case T_ANDEQUALS:	op = OP_AND; break;
		case T_OREQUALS:	op = OP_OR; break;
		case T_LSHIFTEQUALS: 	op = OP_LEFTSHIFT; break;
		case T_RSHIFTEQUALS: 	op = OP_RIGHTSHIFT; break;
		case T_POWEREQUALS:	op = OP_POWER; break;

		case T_NUMBER:
		case T_IMAGINARY:
		case T_STRING:
		case T_SYMBOL:
		case T_OLDVALUE:
		case T_LEFTPAREN:
		case T_PLUSPLUS:
		case T_MINUSMINUS:
		case T_NOT:
			scanerror(T_NULL, "Missing operator");
			return type;

		default:
			rescantoken();
			return type;
	}
	if (isrvalue(type)) {
		scanerror(T_NULL, "Illegal assignment");
		(void) getassignment();
		return (EXPR_RVALUE | EXPR_ASSIGN);
	}
	writeindexop();
	if (op)
		addop(OP_DUPLICATE);
	if (gettoken() == T_LEFTBRACE) {
		rescantoken();
		if (op) {
			addop(OP_DUPVALUE);
			getinitlist();
			addop(op);
			addop(OP_ASSIGN);
		}
		else
			getinitlist();
		return EXPR_ASSIGN;
	}
	rescantoken();
	(void) getassignment();
	if (op) {
		addop(op);
	}
	addop(OP_ASSIGN);
	return EXPR_ASSIGN;
}


/*
 * Get a possible conditional result expression (question mark).
 * Flags are returned indicating the type of expression found.
 * altcond = orcond [ '?' orcond ':' altcond ].
 */
static int
getaltcond(void)
{
	int type;		/* type of expression */
	LABEL donelab;		/* label for done */
	LABEL altlab;		/* label for alternate expression */

	type = getorcond();
	if (gettoken() != T_QUESTIONMARK) {
		rescantoken();
		return type;
	}
	clearlabel(&donelab);
	clearlabel(&altlab);
	addoplabel(OP_JUMPEQ, &altlab);
	(void) getaltcond();
	if (gettoken() != T_COLON) {
		scanerror(T_SEMICOLON, "Missing colon for conditional expression");
		return EXPR_RVALUE;
	}
	addoplabel(OP_JUMP, &donelab);
	setlabel(&altlab);
	(void) getaltcond();
	setlabel(&donelab);
	return EXPR_RVALUE;
}


/*
 * Get a possible conditional or expression.
 * Flags are returned indicating the type of expression found.
 * orcond = andcond [ '||' andcond ] ...
 */
static int
getorcond(void)
{
	int type;		/* type of expression */
	LABEL donelab;		/* label for done */

	clearlabel(&donelab);
	type = getandcond();
	while (gettoken() == T_OROR) {
		addoplabel(OP_CONDORJUMP, &donelab);
		(void) getandcond();
		type = EXPR_RVALUE;
	}
	rescantoken();
	if (donelab.l_chain > 0)
		setlabel(&donelab);
	return type;
}


/*
 * Get a possible conditional and expression.
 * Flags are returned indicating the type of expression found.
 * andcond = relation [ '&&' relation ] ...
 */
static int
getandcond(void)
{
	int type;		/* type of expression */
	LABEL donelab;		/* label for done */

	clearlabel(&donelab);
	type = getrelation();
	while (gettoken() == T_ANDAND) {
		addoplabel(OP_CONDANDJUMP, &donelab);
		(void) getrelation();
		type = EXPR_RVALUE;
	}
	rescantoken();
	if (donelab.l_chain > 0)
		setlabel(&donelab);
	return type;
}


/*
 * Get a possible relation (equality or inequality), or just an expression.
 * Flags are returned indicating the type of relation found.
 * relation = sum '==' sum
 *	| sum '!=' sum
 *	| sum '<=' sum
 *	| sum '>=' sum
 *	| sum '<' sum
 *	| sum '>' sum
 *	| sum.
 */
static int
getrelation(void)
{
	int type;		/* type of expression */
	long op;		/* opcode to generate */

	type = getsum();
	switch (gettoken()) {
		case T_EQ: op = OP_EQ; break;
		case T_NE: op = OP_NE; break;
		case T_LT: op = OP_LT; break;
		case T_GT: op = OP_GT; break;
		case T_LE: op = OP_LE; break;
		case T_GE: op = OP_GE; break;
		default:
			rescantoken();
			return type;
	}
	(void) getsum();
	addop(op);
	return EXPR_RVALUE;
}


/*
 * Get an expression made up of sums of products.
 * Flags indicating the type of expression found are returned.
 * sum = product [ {'+' | '-'} product ] ...
 */
static int
getsum(void)
{
	int type;		/* type of expression found */
	long op;		/* opcode to generate */

	type = getproduct();
	for (;;) {
		switch (gettoken()) {
			case T_PLUS:	op = OP_ADD; break;
			case T_MINUS:	op = OP_SUB; break;
			default:
				rescantoken();
				return type;
		}
		(void) getproduct();
		addop(op);
		type = EXPR_RVALUE;
	}
}


/*
 * Get the product of arithmetic or expressions.
 * Flags indicating the type of expression found are returned.
 * product = orexpr [ {'*' | '/' | '//' | '%'} orexpr ] ...
 */
static int
getproduct(void)
{
	int type;		/* type of value found */
	long op;		/* opcode to generate */

	type = getorexpr();
	for (;;) {
		switch (gettoken()) {
			case T_MULT:	op = OP_MUL; break;
			case T_DIV:	op = OP_DIV; break;
			case T_MOD:	op = OP_MOD; break;
			case T_SLASHSLASH: op = OP_QUO; break;
			default:
				rescantoken();
				return type;
		}
		(void) getorexpr();
		addop(op);
		type = EXPR_RVALUE;
	}
}


/*
 * Get an expression made up of arithmetic or operators.
 * Flags indicating the type of expression found are returned.
 * orexpr = andexpr [ '|' andexpr ] ...
 */
static int
getorexpr(void)
{
	int type;		/* type of value found */

	type = getandexpr();
	while (gettoken() == T_OR) {
		(void) getandexpr();
		addop(OP_OR);
		type = EXPR_RVALUE;
	}
	rescantoken();
	return type;
}


/*
 * Get an expression made up of arithmetic and operators.
 * Flags indicating the type of expression found are returned.
 * andexpr = shiftexpr [ '&' shiftexpr ] ...
 */
static int
getandexpr(void)
{
	int type;		/* type of value found */

	type = getshiftexpr();
	while (gettoken() == T_AND) {
		(void) getshiftexpr();
		addop(OP_AND);
		type = EXPR_RVALUE;
	}
	rescantoken();
	return type;
}


/*
 * Get a shift or power expression.
 * Flags indicating the type of expression found are returned.
 * shift = term '^' shiftexpr
 *	 | term '<<' shiftexpr
 *	 | term '>>' shiftexpr
 *	 | term.
 */
static int
getshiftexpr(void)
{
	int type;		/* type of value found */
	long op;		/* opcode to generate */
	int tok;

	type = getterm();
	tok = gettoken();
	if (tok == T_PLUSPLUS || tok == T_MINUSMINUS) {
		if (isrvalue(type))
				scanerror(T_NULL, "Bad ++ usage");
		writeindexop();
		if (tok == T_PLUSPLUS)
			addop(OP_POSTINC);
		else
			addop(OP_POSTDEC);
		for (;;) {
			tok = gettoken();
			switch(tok) {
				case T_PLUSPLUS:
					addop(OP_PREINC);
					continue;
				case T_MINUSMINUS:
					addop(OP_PREDEC);
					continue;
				default:
					addop(OP_POP);
					goto done;
			}
		}
done:		type = EXPR_RVALUE | EXPR_ASSIGN;
	}
	if (tok == T_NOT) {
		addopfunction(OP_CALL, getbuiltinfunc("fact"), 1);
		tok = gettoken();
		type = EXPR_RVALUE;
	}
	switch (tok) {
		case T_POWER:		op = OP_POWER; break;
		case T_LEFTSHIFT:	op = OP_LEFTSHIFT; break;
		case T_RIGHTSHIFT: 	op = OP_RIGHTSHIFT; break;
		default:
			rescantoken();
			return type;
	}
	(void) getshiftexpr();
	addop(op);
	return EXPR_RVALUE;
}


/*
 * Get a single term.
 * Flags indicating the type of value found are returned.
 * term = lvalue
 *	| lvalue '[' assignment ']'
 *	| lvalue '++'
 *	| lvalue '--'
 *	| '++' lvalue
 *	| '--' lvalue
 *	| real_number
 *	| imaginary_number
 *	| '.'
 *	| string
 *	| '(' assignment ')'
 *	| function [ '(' [assignment  [',' assignment] ] ')' ]
 *	| '!' term
 *	| '+' term
 *	| '-' term.
 */
static int
getterm(void)
{
	int type;		/* type of term found */

	type = gettoken();
	switch (type) {
		case T_NUMBER:
			addopone(OP_NUMBER, tokennumber());
			type = (EXPR_RVALUE | EXPR_CONST);
			break;

		case T_IMAGINARY:
			addopone(OP_IMAGINARY, tokennumber());
			type = (EXPR_RVALUE | EXPR_CONST);
			break;

		case T_OLDVALUE:
			addop(OP_OLDVALUE);
			type = 0;
			break;

		case T_STRING:
			addopptr(OP_STRING, tokenstring());
			type = (EXPR_RVALUE | EXPR_CONST);
			break;

		case T_PLUSPLUS:
			if (isrvalue(getterm()))
				scanerror(T_NULL, "Bad ++ usage");
			writeindexop();
			addop(OP_PREINC);
			type = EXPR_ASSIGN;
			break;

		case T_MINUSMINUS:
			if (isrvalue(getterm()))
				scanerror(T_NULL, "Bad -- usage");
			writeindexop();
			addop(OP_PREDEC);
			type = EXPR_ASSIGN;
			break;

		case T_NOT:
			(void) getterm();
			addop(OP_NOT);
			type = EXPR_RVALUE;
			break;

		case T_MINUS:
			(void) getterm();
			addop(OP_NEGATE);
			type = EXPR_RVALUE;
			break;

		case T_PLUS:
			(void) getterm();
			type = EXPR_RVALUE;
			break;

		case T_LEFTPAREN:
			type = getexprlist();
			if (gettoken() != T_RIGHTPAREN)
				scanerror(T_SEMICOLON, "Missing right parenthesis");
			break;

		case T_MAT:
			getmatdeclaration(SYM_UNDEFINED);
			type = EXPR_ASSIGN;
			break;

		case T_OBJ:
			getobjdeclaration(SYM_UNDEFINED);
			type = EXPR_ASSIGN;
			break;

		case T_SYMBOL:
			rescantoken();
			type = getidexpr(TRUE, FALSE);
			break;

		case T_LEFTBRACKET:
			scanerror(T_NULL, "Bad index usage");
			type = 0;
			break;

		case T_PERIOD:
			scanerror(T_NULL, "Bad element reference");
			type = 0;
			break;

		default:
			if (iskeyword(type)) {
				scanerror(T_NULL, "Expression contains reserved keyword");
				type = 0;
				break;
			}
			rescantoken();
			scanerror(T_COMMA, "Missing expression");
			type = 0;
	}
	return type;
}


/*
 * Read in an identifier expressions.
 * This is a symbol name followed by parenthesis, or by square brackets or
 * element refernces.  The symbol can be a global or a local variable name.
 * Returns the type of expression found.
 */
static int
getidexpr(BOOL okmat, BOOL autodef)
{
	int type;
	char name[SYMBOLSIZE+1];	/* symbol name */

	type = 0;
	if (!getid(name))
		return type;
	switch (gettoken()) {
		case T_LEFTPAREN:
			getcallargs(name);
			type = 0;
			break;
		case T_ASSIGN:
			autodef = TRUE;
			/* fall into default case */
		default:
			rescantoken();
			usesymbol(name, autodef);
	}
	/*
	 * Now collect as many element references and matrix index operations
	 * as there are following the id.
	 */
	for (;;) {
		switch (gettoken()) {
			case T_LEFTBRACKET:
				rescantoken();
				if (!okmat)
					return type;
				getmatargs();
				type = 0;
				break;
			case T_PERIOD:
				getelement();
				type = 0;
				break;
			case T_LEFTPAREN:
				scanerror(T_NULL, "Function calls not allowed as expressions");
			default:
				rescantoken();
				return type;
		}
	}
}


/*
 * Read in a filename for a read or write command.
 * Both quoted and unquoted filenames are handled here.
 * The name must be terminated by an end of line or semicolon.
 * Returns TRUE if the filename was successfully parsed.
 *
 * given:
 *	name		filename to read
 *	msg_ok		TRUE => ok to print error messages
 *	once		non-NULL => set to TRUE of -once read 
 */
static BOOL
getfilename(char *name, BOOL msg_ok, BOOL *once)
{
	/* look at the next token */
	(void) tokenmode(TM_NEWLINES | TM_ALLSYMS);
	switch (gettoken()) {
		case T_STRING:
		case T_SYMBOL:
			break;
		default:
			if (msg_ok)
				scanerror(T_SEMICOLON, "Filename expected");
			return FALSE;
	}
	strcpy(name, tokenstring());

	/* determine if we care about a possible -once option */
	if (once != NULL) {
		/* we care about a possible -once option */
		if (strcmp(name, "-once") == 0) {
			/* -once option found */
			*once = TRUE;
			/* look for the filename */
			switch (gettoken()) {
				case T_STRING:
				case T_SYMBOL:
					break;
				default:
					if (msg_ok)
						scanerror(T_SEMICOLON,
						    "Filename expected");
					return FALSE;
			}
			strcpy(name, tokenstring());
		} else {
			*once = FALSE;
		}
	}

	/* look at the next token */
	switch (gettoken()) {
		case T_SEMICOLON:
		case T_NEWLINE:
		case T_EOF:
			break;
		default:
			if (msg_ok)
				scanerror(T_SEMICOLON,
				    "Missing semicolon after filename");
			return FALSE;
	}
	return TRUE;
}


/*
 * Read the show command to display useful information
 */
static void
getshowstatement(void)
{
	char name[5];
	long arg, index;

	switch (gettoken()) {
		case T_SYMBOL:
			strncpy(name, tokenstring(), 4);
			name[4] = '\0';
			arg = stringindex("buil\000glob\000func\000objf\000conf\000objt\000file\000size\000opco\0", name);
			if (arg == 9) {
				if (gettoken() != T_SYMBOL) {
					rescantoken();
					scanerror(T_SEMICOLON, "Function name expected");
					return;
				}
				index = adduserfunc(tokenstring());
				addopone(OP_SHOW, index + 9);
				return;
			}
			if (arg > 0)
				addopone(OP_SHOW, arg);
			else
				printf("Unknown SHOW parameter ignored");
			return;
		default:
			printf("SHOW command to be followed by at least ");
			printf("four letters of one of:\n");
			printf("\tbuiltin, global, function, objfunc, ");
			printf("config, objtype, files, sizes\n");
			rescantoken();
			return;

	}
}


/*
 * Read in a set of matrix index arguments, surrounded with square brackets.
 * This also handles double square brackets for 'fast indexing'.
 */
static void
getmatargs(void)
{
	int dim;

	if (gettoken() != T_LEFTBRACKET) {
		scanerror(T_NULL, "Matrix indexing expected");
		return;
	}
	/*
	 * Parse all levels of the array reference
	 * Look for the 'fast index' first.
	 */
	if (gettoken() == T_LEFTBRACKET) {
		(void) getassignment();
		if ((gettoken() != T_RIGHTBRACKET) ||
			(gettoken() != T_RIGHTBRACKET)) {
				scanerror(T_NULL, "Bad fast index usage");
				return;
		}
		addop(OP_FIADDR);
		return;
	}
	rescantoken();
	/*
	 * Normal indexing with the indexes separated by commas.
	 * Initialize the flag in the opcode to assume that the array
	 * element will only be referenced for reading.  If the parser
	 * finds that the element will be referenced for writing, then
	 * it will call writeindexop to change the flag in the opcode.
	 */
	dim = 1;
	for (;;) {
		(void) getassignment();
		switch (gettoken()) {
			case T_RIGHTBRACKET:
				addoptwo(OP_INDEXADDR, (long) dim,
						(long) FALSE);
				return;
			case T_COMMA:
				dim++;
				break;
			default:
				rescantoken();
				scanerror(T_NULL, "Missing right bracket in array reference");
				return;
		}
	}
}


/*
 * Get an element of an object reference.
 * The leading period which introduces the element has already been read.
 */
static void
getelement(void)
{
	long index;
	char name[SYMBOLSIZE+1];

	if (!getid(name))
		return;
	index = findelement(name);
	if (index < 0) {
		scanerror(T_NULL, "Element \"%s\" is undefined", name);
		return;
	}
	addopone(OP_ELEMADDR, index);
}


/*
 * Read in a single symbol name and copy its value into the given buffer.
 * Returns TRUE if a valid symbol id was found.
 */
static BOOL
getid(char *buf)
{
	int type;

	type = gettoken();
	if (iskeyword(type)) {
		scanerror(T_NULL, "Reserved keyword used as symbol name");
		type = T_SYMBOL;
		*buf = '\0';
		return FALSE;
	}
	if (type != T_SYMBOL) {
		rescantoken();
		scanerror(T_NULL, "Symbol name expected");
		*buf = '\0';
		return FALSE;
	}
	strncpy(buf, tokenstring(), SYMBOLSIZE);
	buf[SYMBOLSIZE] = '\0';
	return TRUE;
}


/*
 * Define a symbol name to be of the specified symbol type.  This also checks
 * to see if the symbol was already defined in an incompatible manner.
 */
static void
definesymbol(char *name, int symtype)
{
	switch (symboltype(name)) {
		case SYM_UNDEFINED:
		case SYM_GLOBAL:
		case SYM_STATIC:
			if (symtype == SYM_LOCAL)
				(void) addlocal(name);
			else
				(void) addglobal(name, (symtype == SYM_STATIC));
			break;

		case SYM_PARAM:
		case SYM_LOCAL:
			scanerror(T_COMMA, "Variable \"%s\" is already defined", name);
			return;
	}

}


/*
 * Check a symbol name to see if it is known and generate code to reference it.
 * The symbol can be either a parameter name, a local name, or a global name.
 * If autodef is true, we automatically define the name as a global symbol
 * if it is not yet known.
 *
 * given:
 *	name		symbol name to be checked
 *	autodef		TRUE => define is symbol is not known
 */
static void
usesymbol(char *name, BOOL autodef)
{
	switch (symboltype(name)) {
		case SYM_LOCAL:
			addopone(OP_LOCALADDR, (long) findlocal(name));
			return;
		case SYM_PARAM:
			addopone(OP_PARAMADDR, (long) findparam(name));
			return;
		case SYM_GLOBAL:
		case SYM_STATIC:
			addopptr(OP_GLOBALADDR, (char *) findglobal(name));
			return;
	}
	/*
	 * The symbol is not yet defined.
	 * If we are at the top level and we are allowed to, then define it.
	 */
	if ((curfunc->f_name[0] != '*') || !autodef) {
		scanerror(T_NULL, "\"%s\" is undefined", name);
		return;
	}
	(void) addglobal(name, FALSE);
	addopptr(OP_GLOBALADDR, (char *) findglobal(name));
}


/*
 * Get arguments for a function call.
 * The name and beginning parenthesis has already been seen.
 * callargs = [ [ '&' ] assignment  [',' [ '&' ] assignment] ] ')'.
 *
 * given:
 *	name		name of function
 */
static void
getcallargs(char *name)
{
	long index;		/* function index */
	long op;		/* opcode to add */
	int argcount;		/* number of arguments */
	int type;
	BOOL addrflag;

	op = OP_CALL;
	index = getbuiltinfunc(name);
	if (index < 0) {
		op = OP_USERCALL;
		index = adduserfunc(name);
	}
	if (gettoken() == T_RIGHTPAREN) {
		if (op == OP_CALL)
			builtincheck(index, 0);
		addopfunction(op, index, 0);
		return;
	}
	rescantoken();
	argcount = 0;
	for (;;) {
		argcount++;
		if (gettoken() == T_RIGHTPAREN) {
			addop(OP_UNDEF);
			if (op == OP_CALL)
				builtincheck(index, argcount);
			addopfunction(op, index, argcount);
			return;
		}
		rescantoken();
		if (gettoken() == T_COMMA) {
			addop(OP_UNDEF);
			continue;
		}
		rescantoken();
		addrflag = (gettoken() == T_AND);
		if (!addrflag)
			rescantoken();
		type = getassignment();
		if (addrflag) {
			if (isrvalue(type))
				scanerror(T_NULL, "Taking address of non-variable");
			writeindexop();
		}
		if (!addrflag && (op != OP_CALL))
			addop(OP_GETVALUE);
		if (!strcmp(name, "quomod") && argcount > 2)
			writeindexop();
		switch (gettoken()) {
			case T_RIGHTPAREN:
				if (op == OP_CALL)
					builtincheck(index, argcount);
				addopfunction(op, index, argcount);
				return;
			case T_COMMA:
				break;
			default:
				scanerror(T_SEMICOLON, "Missing right parenthesis in function call");
				return;
		}
	}
}


/*
 * Change the current directory.  If no directory is given, assume home.
 */
static void
do_changedir(void)
{
	char *p;

	/* look at the next token */
	(void) tokenmode(TM_NEWLINES | TM_ALLSYMS);

	/* determine the new directory */
	switch (gettoken()) {
	case T_NULL:
	case T_NEWLINE:
	case T_SEMICOLON:
		p = getenv("HOME");
		break;
	default:
		p = tokenstring();
		if (p == NULL) {
			p = getenv("HOME");
		}
		break;
	}
	if (p == NULL) {
		fprintf(stderr, "Cannot determine HOME directory\n");
	}

	/* change to that directory */
	if (chdir(p)) {
		perror(p);
	}
	return;
}


/* END CODE */
