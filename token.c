/*
 * Copyright (c) 1997 David I. Bell
 * Permission is granted to use, distribute, or modify this source,
 * provided that this copyright notice remains intact.
 *
 * Read input file characters into tokens
 */


#include <stdio.h>
#include <setjmp.h>

#include "calc.h"
#include "token.h"
#include "string.h"
#include "args.h"
#include "math_error.h"


#define isletter(ch)	((((ch) >= 'a') && ((ch) <= 'z')) || \
				(((ch) >= 'A') && ((ch) <= 'Z')))
#define isdigit(ch)	(((ch) >= '0') && ((ch) <= '9'))
#define issymbol(ch)	(isletter(ch) || isdigit(ch) || ((ch) == '_'))
#define isoctal(ch)	(((ch) >= '0') && ((ch) <= '7'))

#define STRBUFSIZE	1024


/*
 * Current token.
 */
static struct {
	short t_type;		/* type of token */
	char *t_sym;		/* symbol name */
	long t_strindex;	/* index of string value */
	long t_numindex;	/* index of numeric value */
} curtoken;


static BOOL rescan;		/* TRUE to reread current token */
static BOOL newlines;		/* TRUE to return newlines as tokens */
static BOOL allsyms;		/* TRUE if always want a symbol token */
static STRINGHEAD strings;	/* list of constant strings */
static char *numbuf;		/* buffer for numeric tokens */
static long numbufsize;		/* current size of numeric buffer */

long errorcount = 0;			/* number of compilation errors */


/*
 * Table of keywords
 */
struct keyword {
	char *k_name;	/* keyword name */
	int k_token;	/* token number */
};

static struct keyword keywords[] = {
	{"if",		T_IF},
	{"else",	T_ELSE},
	{"for",		T_FOR},
	{"while",	T_WHILE},
	{"do",		T_DO},
	{"continue",	T_CONTINUE},
	{"break",	T_BREAK},
	{"goto",	T_GOTO},
	{"return",	T_RETURN},
	{"local",	T_LOCAL},
	{"global",	T_GLOBAL},
	{"static",	T_STATIC},
	{"switch",	T_SWITCH},
	{"case",	T_CASE},
	{"default",	T_DEFAULT},
	{"quit",	T_QUIT},
	{"exit",	T_QUIT},
	{"define",	T_DEFINE},
	{"read",	T_READ},
	{"show",	T_SHOW},
	{"help",	T_HELP},
	{"write",	T_WRITE},
	{"mat",		T_MAT},
	{"obj",		T_OBJ},
	{"print",	T_PRINT},
	{"cd",		T_CD},
	{"undefine",	T_UNDEFINE},
	{NULL,		0}
};


static void eatcomment(void);
static void eatstring(int quotechar);
static int eatsymbol(void);
static int eatnumber(void);


/*
 * Initialize all token information.
 */
void
inittokens(void)
{
	initstr(&strings);
	newlines = FALSE;
	allsyms = FALSE;
	rescan = FALSE;
	setprompt(conf->prompt1);
}


/*
 * Set the new token mode according to the specified flag, and return the
 * previous value of the flag.
 */
int
tokenmode(int flag)
{
	int	oldflag;

	oldflag = TM_DEFAULT;
	if (newlines)
		oldflag |= TM_NEWLINES;
	if (allsyms)
		oldflag |= TM_ALLSYMS;
	newlines = FALSE;
	allsyms = FALSE;
	if (flag & TM_NEWLINES)
		newlines = TRUE;
	if (flag & TM_ALLSYMS)
		allsyms = TRUE;
	setprompt(newlines ? conf->prompt1 : conf->prompt2);
	return oldflag;
}


/*
 * Routine to read in the next token from the input stream.
 * The type of token is returned as a value.  If the token is a string or
 * symbol name, information is saved so that the value can be retrieved.
 */
int
gettoken(void)
{
	int ch;			/* current input character */
	int type;		/* token type */

	if (rescan) {		/* rescanning */
		rescan = FALSE;
		return curtoken.t_type;
	}
	curtoken.t_sym = NULL;
	curtoken.t_strindex = 0;
	curtoken.t_numindex = 0;
	type = T_NULL;
	while (type == T_NULL) {
		ch = nextchar();
		if (allsyms && ((ch!=' ') && (ch!=';') && (ch!='"') && (ch!='\n'))) {
			reread();
			type = eatsymbol();
			break;
		}
		switch (ch) {
		case ' ':
		case '\t':
		case '\0':
			break;
		case '\n':
			if (newlines)
				type = T_NEWLINE;
			break;
		case EOF: type = T_EOF; break;
		case '{': type = T_LEFTBRACE; break;
		case '}': type = T_RIGHTBRACE; break;
		case '(': type = T_LEFTPAREN; break;
		case ')': type = T_RIGHTPAREN; break;
		case '[': type = T_LEFTBRACKET; break;
		case ']': type = T_RIGHTBRACKET; break;
		case ';': type = T_SEMICOLON; break;
		case ':': type = T_COLON; break;
		case ',': type = T_COMMA; break;
		case '?': type = T_QUESTIONMARK; break;
		case '@': type = T_AT; break;
		case '`': type = T_BACKQUOTE; break;
		case '$': type = T_DOLLAR; break;
		case '"':
		case '\'':
			type = T_STRING;
			eatstring(ch);
			break;
		case '^':
			switch (nextchar()) {
				case '=': type = T_POWEREQUALS; break;
				default: type = T_POWER; reread();
			}
			break;
		case '=':
			switch (nextchar()) {
				case '=': type = T_EQ; break;
				default: type = T_ASSIGN; reread();
			}
			break;
		case '+':
			switch (nextchar()) {
				case '+': type = T_PLUSPLUS; break;
				case '=': type = T_PLUSEQUALS; break;
				default: type = T_PLUS; reread();
			}
			break;
		case '-':
			switch (nextchar()) {
				case '-': type = T_MINUSMINUS; break;
				case '=': type = T_MINUSEQUALS; break;
				case '>': type = T_ARROW; break;
				default: type = T_MINUS; reread();
			}
			break;
		case '*':
			switch (nextchar()) {
				case '=': type = T_MULTEQUALS; break;
				case '*':
					switch (nextchar()) {
						case '=': type = T_POWEREQUALS; break;
						default: type = T_POWER; reread();
					}
					break;
				default: type = T_MULT; reread();
			}
			break;
		case '/':
			switch (nextchar()) {
				case '/':
					switch (nextchar()) {
						case '=': type = T_SLASHSLASHEQUALS; break;
						default: reread(); type = T_SLASHSLASH; break;
					}
					break;
				case '=': type = T_DIVEQUALS; break;
				case '*': eatcomment(); break;
				default: type = T_DIV; reread();
			}
			break;
		case '%':
			switch (nextchar()) {
				case '=': type = T_MODEQUALS; break;
				default: type = T_MOD; reread();
			}
			break;
		case '<':
			switch (nextchar()) {
				case '=': type = T_LE; break;
				case '<':
					switch (nextchar()) {
						case '=': type = T_LSHIFTEQUALS; break;
						default:  reread(); type = T_LEFTSHIFT; break;
					}
					break;
				default: type = T_LT; reread();
			}
			break;
		case '>':
			switch (nextchar()) {
				case '=': type = T_GE; break;
				case '>':
					switch (nextchar()) {
						case '=': type = T_RSHIFTEQUALS; break;
						default:  reread(); type = T_RIGHTSHIFT; break;
					}
					break;
				default: type = T_GT; reread();
			}
			break;
		case '&':
			switch (nextchar()) {
				case '&': type = T_ANDAND; break;
				case '=': type = T_ANDEQUALS; break;
				default: type = T_AND; reread(); break;
			}
			break;
		case '|':
			switch (nextchar()) {
				case '|': type = T_OROR; break;
				case '=': type = T_OREQUALS; break;
				default: type = T_OR; reread(); break;
			}
			break;
		case '!':
			switch (nextchar()) {
				case '=': type = T_NE; break;
				default: type = T_NOT; reread(); break;
			}
			break;
		case '#':
			switch(nextchar()) {
				case '=': type = T_HASHEQUALS; break;
				default: type = T_HASH; reread();
			}
			break;
		case '~':
			switch (nextchar()) {
				case '=': type = T_TILDEEQUALS; break;
				default: type = T_TILDE; reread();
			}
			break;
		case '\\':
			switch (nextchar()) {
				case '\n': setprompt(conf->prompt2); break;
				case '=': type = T_BACKSLASHEQUALS; break;
				default: type = T_BACKSLASH; reread();
			}
			break;
		default:
			if (isletter(ch) || ch == '_') {
				reread();
				type = eatsymbol();
				break;
			}
			if (isdigit(ch) || (ch == '.')) {
				reread();
				type = eatnumber();
				break;
			}
			scanerror(T_NULL, "Unknown token character '%c'", ch);
		}
	}
	curtoken.t_type = (short)type;
	return type;
}


/*
 * Continue to eat up a comment string.
 * The leading slash-asterisk has just been scanned at this point.
 */
static void
eatcomment(void)
{
	int ch;

	for (;;) {
		ch = nextchar();
		if (ch == '*') {
			ch = nextchar();
			if (ch == '/')
				return;
			reread();
		}
		if ((ch == EOF) || (ch == '\0') ||
			(newlines && (ch == '\n') && inputisterminal())) {
				reread();
				scanerror(T_NULL, "Unterminated comment");
				return;
		}
	}
}


/*
 * Read in a string and add it to the literal string pool.
 * The leading single or double quote has been read in at this point.
 */
static void
eatstring(int quotechar)
{
	register char *cp;	/* current character address */
	int ch, cch;		/* current character */
	int i;			/* index */
	char buf[STRBUFSIZE];	/* buffer for string */
	long len;		/* length in buffer */
	long totlen;		/* total length, including '\0' */
	char *str;
	BOOL done;

	str = buf;
	totlen = 0;
	done = FALSE;

	while (!done) {
	    cp = buf;
	    len = 0;
	    while (!done && len < STRBUFSIZE) {
		ch = nextchar();
		switch (ch) {
			case '\n':
				if (!newlines)
					break;
			case EOF:
				reread();
				scanerror(T_NULL, "Unterminated string constant");
				done = TRUE;
				ch = '\0';
				break;

			case '\\':
				ch = nextchar();
				if (isoctal(ch)) {
				    ch = ch - '0';
				    for (i = 2; i > 0; i--) {
					cch = nextchar();
					if (!isoctal(cch))
						break;
					ch = 8 * ch + cch - '0';
				    }
				    ch &= 0xff;
			 	    if (i > 0)
				        reread();
				    break;
				}
				switch (ch) {
				    case 'n': ch = '\n'; break;
				    case 'r': ch = '\r'; break;
				    case 't': ch = '\t'; break;
				    case 'b': ch = '\b'; break;
				    case 'f': ch = '\f'; break;
				    case 'v': ch = '\v'; break;
				    case 'a': ch = '\007'; break;
				    case 'e': ch = '\033'; break;
				    case '\n':
					setprompt(conf->prompt2);
					continue;
				    case EOF:
					reread();
					continue;
				    case 'x':
					ch = 0;
					for (i = 2; i > 0; i--) {
					    cch = nextchar();
					    if (isdigit(cch))
						ch = 16 * ch + cch - '0';
					    else if (cch >= 'a' && cch <= 'f')
						ch = 16 * ch + 10 + cch - 'a';
					    else if (cch >= 'A' && cch <= 'F')
						ch = 16 * ch + 10 + cch - 'A';
					    else break;
					}
					if (i > 0)
					    reread();
				}
				break;
			case '"':
			case '\'':
				if (ch == quotechar) {
					done = TRUE;
					ch = '\0';
				}
				break;
		    }

		*cp++ = (char) ch;
		len++;
	    }
	    if (!done || totlen) {
		if (totlen)
			str = (char *) realloc(str, totlen + len);
		else
			str = (char *) malloc(len);
		if (str == NULL) {
			math_error("Out of memory for reading tokens");
				/*NOTREACHED*/
		}
		memcpy(str + totlen, buf, len);
		totlen += len;
	    }
	}
  	curtoken.t_strindex = addstring(str, totlen + len);
	if (str != buf)
		free(str);
}


/*
 * Read in a symbol name which may or may not be a keyword.
 * If allsyms is set, keywords are not looked up and almost all chars
 * will be accepted for the symbol.  Returns the type of symbol found.
 */
static int
eatsymbol(void)
{
	register struct keyword *kp;	/* pointer to current keyword */
	register char *cp;		/* current character pointer */
	int ch;				/* current character */
	int cc;				/* character count */
	static char buf[SYMBOLSIZE+1];	/* temporary buffer */

	cp = buf;
	cc = SYMBOLSIZE;
	if (allsyms) {
		for (;;) {
			ch = nextchar();
			if ((ch == ' ') || (ch == ';') || (ch == '\n'))
				break;
			if (cc-- > 0)
				*cp++ = (char)ch;
		}
		reread();
		*cp = '\0';
		if (cc < 0)
			scanerror(T_NULL, "Symbol too long");
		curtoken.t_sym = buf;
		return T_SYMBOL;
	}
	for (;;) {
		ch = nextchar();
		if (!issymbol(ch))
			break;
		if (cc-- > 0)
			*cp++ = (char)ch;
	}
	reread();
	*cp = '\0';
	if (cc < 0)
		scanerror(T_NULL, "Symbol too long");
	for (kp = keywords; kp->k_name; kp++)
		if (strcmp(kp->k_name, buf) == 0)
			return kp->k_token;
	curtoken.t_sym = buf;
	return T_SYMBOL;
}


/*
 * Read in and remember a possibly numeric constant value.
 * The constant is inserted into a constant table so further uses
 * of the same constant will not take more memory.  This can also
 * return just a period, which is used for element accesses and for
 * the old numeric value.
 */
static int
eatnumber(void)
{
	register char *cp;	/* current character pointer */
	long len;		/* parsed size of number */
	long res;		/* result of parsing number */

	if (numbufsize == 0) {
		numbuf = (char *)malloc(128+1);
		if (numbuf == NULL)
			math_error("Cannot allocate number buffer");
		numbufsize = 128;
	}
	cp = numbuf;
	len = 0;
	for (;;) {
		if (len >= numbufsize) {
			cp = (char *)realloc(numbuf, numbufsize + 1001);
			if (cp == NULL) {
				math_error("Cannot reallocate number buffer");
				/*NOTREACHED*/
			}
			numbuf = cp;
			numbufsize += 1000;
			cp = &numbuf[len];
		}
		*cp = nextchar();
		*(++cp) = '\0';
		if ((numbuf[0] == '.') && isletter(numbuf[1])) {
			reread();
			return T_PERIOD;
		}
		res = qparse(numbuf, QPF_IMAG);
		if (res < 0) {
			reread();
			scanerror(T_NULL, "Badly formatted number");
			curtoken.t_numindex = addnumber("0");
			return T_NUMBER;
		}
		if (res != ++len)
			break;
	}
	cp[-1] = '\0';
	reread();
	if ((numbuf[0] == '.') && (numbuf[1] == '\0')) {
		curtoken.t_numindex = 0;
		return T_OLDVALUE;
	}
	cp -= 2;
	res = T_NUMBER;
	if ((*cp == 'i') || (*cp == 'I')) {
		*cp = '\0';
		res = T_IMAGINARY;
	}
	curtoken.t_numindex = addnumber(numbuf);
	return (int)res;
}


/*
 * Return the index for string value of the current token.
 */
long
tokenstring(void)
{
	return curtoken.t_strindex;
}


/*
 * Return the constant index of a numeric token.
 */
long
tokennumber(void)
{
	return curtoken.t_numindex;
}

/*
 * Return the address of a symbol
 */
char *
tokensymbol(void)
{
	return curtoken.t_sym;
}

/*
 * Push back the token just read so that it will be seen again.
 */
void
rescantoken(void)
{
	rescan = TRUE;
}


/*
 * Describe an error message.
 * Then skip to the next specified token (or one more powerful).
 */
void
scanerror(int skip, char *fmt, ...)
{
	va_list ap;
	char *name;		/* name of file with error */
	char buf[MAXERROR+1];

	/* count the error */
	errorcount++;

	/* print the error message */
	name = inputname();
	if (name)
		fprintf(stderr, "\"%s\", line %ld: ", name, linenumber());
	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	fprintf(stderr, "%s\n", buf);

	/* bail out if too many errors */
	if (conf->maxscancount > 0 && errorcount > conf->maxscancount) {
		fputs("Too many scan errors, compilation aborted.\n", stderr);
		longjmp(jmpbuf, 1);
		/*NOTREACHED*/
	}

	/* post-error report processing */
	switch (skip) {
		case T_NULL:
			return;
		case T_COMMA:
			rescan = TRUE;
			for (;;) {
				switch (gettoken()) {
				case T_NEWLINE:
				case T_SEMICOLON:
				case T_LEFTBRACE:
				case T_RIGHTBRACE:
				case T_EOF:
				case T_COMMA:
					rescan = TRUE;
					return;
				}
			}
		default:
			fprintf(stderr, "Unknown skip token for scanerror\n");
			/* fall into semicolon case */
			/*FALLTHRU*/
		case T_SEMICOLON:
			rescan = TRUE;
			for (;;) switch (gettoken()) {
				case T_NEWLINE:
				case T_SEMICOLON:
				case T_LEFTBRACE:
				case T_RIGHTBRACE:
				case T_EOF:
					rescan = TRUE;
					return;
			}
	}
}

/* END CODE */