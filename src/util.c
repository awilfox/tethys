#include "ircd.h"

static char null_casemap[256];
static char rfc1459_casemap[256];
static char ascii_casemap[256];
static int valid_nick_map[256];
static int valid_ident_map[256];

int matchmap(mask, string, casemap)
char *mask, *string, *casemap;
{
	const char *m = mask, *s = string;
	const char *m_bt = m, *s_bt = s;
 
	for (;;) {
		switch (*m) {
		case '\0':
			if (*s == '\0')
				return 1;
		backtrack:
			if (m_bt == mask)
				return 0;
			m = m_bt;
			s = ++s_bt;
			break;
 
		case '*':
			while (*m == '*')
				m++;
			m_bt = m;
			s_bt = s;
			break;
 
		default:
			if (!*s)
				return 0;
			if (casemap[*m] != casemap[*s] && *m != '?')
				goto backtrack;
			m++;
			s++;
		}
	}
}

int match(mask, string)
char *mask, string;
{
	matchmap(mask, string, null_casemap);
}

int matchirc(mask, string)
char *mask, string;
{
	matchmap(mask, string, rfc1459_casemap);
}

int matchcase(mask, string)
char *mask, string;
{
	matchmap(mask, string, ascii_casemap);
}

void u_memmove_lower(dest, src, n)
char *dest, *src;
int n;
{
	while (n --> 0) {
		*dest++ = *src++;
	}
}

void u_memmove_upper(dest, src, n)
char *dest, *src;
int n;
{
	src += n;
	dest += n;
	while (n --> 0) {
		*--dest = *--src;
	}
}

void u_memmove(dest, src, n)
char *dest, *src;
int n;
{
	if (dest < src)
		u_memmove_lower(dest, src, n);
	if (dest > src)
		u_memmove_upper(dest, src, n);
}

void u_strlcpy(dest, src, n)
char *dest, *src;
int n;
{
	int len;
	n--;

	len = strlen(src);
	if (n > len)
		n = len;

	memcpy(dest, src, n);
	dest[n] = '\0';
}

void null_canonize(s)
char *s;
{
}

void rfc1459_canonize(s)
char *s;
{
	for (; *s; s++)
		*s = rfc1459_casemap[*s];
}

void ascii_canonize(s)
char *s;
{
	for (; *s; s++)
		*s = ascii_casemap[*s];
}

int is_valid_nick(s)
char *s;
{
	if (isdigit(*s))
		return 0;
	for (; *s; s++) {
		if (!valid_nick_map[*s])
			return 0;
	}
	return 1;
}

int is_valid_ident(s)
char *s;
{
	for (; *s; s++) {
		if (!valid_ident_map[*s])
			return 0;
	}
	return 1;
}

int init_util()
{
	int i;

	for (i=0; i<256; i++) {
		null_casemap[i] = i;
		rfc1459_casemap[i] = tolower(i);
		ascii_casemap[i] = tolower(i);
	}
	rfc1459_casemap['{'] = '[';
	rfc1459_casemap['}'] = ']';
	rfc1459_casemap['|'] = '\\';
	rfc1459_casemap['^'] = '~';

	for (i=0; i<256; i++)
		valid_nick_map[i] = isalnum(i) || strchr("[]{}|\\^-_", i);

	/* TODO: decide */
	for (i=0; i<256; i++)
		valid_ident_map[i] = isalnum(i) || strchr("[]{}-_", i);

	return 0;
}