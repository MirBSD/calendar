/*-
 * Copyright (c) 2006, 2008, 2011
 *	mirabilos <m$(date +%Y)@mirbsd.de>
 *
 * Provided that these terms and disclaimer and all copyright notices
 * are retained or reproduced in an accompanying document, permission
 * is granted to deal in this work without restriction, including un-
 * limited rights to use, publicly perform, distribute, sell, modify,
 * merge, give away, or sublicence.
 *
 * This work is provided "AS IS" and WITHOUT WARRANTY of any kind, to
 * the utmost extent permitted by applicable law, neither express nor
 * implied; without malicious intent or gross negligence. In no event
 * may a licensor, author or contributor be held liable for indirect,
 * direct, other damage, loss, or other issues arising in any way out
 * of dealing in the work, even if advised of the possibility of such
 * damage or existence of a defect, except proven that it results out
 * of said person's immediate fault when using the work as intended.
 */

#include <sys/types.h>
#if defined(MBSDPORT_H)
#include MBSDPORT_H
#elif !defined(OUTSIDE_OF_LIBKERN)
#include <libckern.h>
#endif

#ifndef __RCSID
#define __RCSID(x)		/* fallback */ static const char __rcsid[] = x
#endif

/* excerpt */
__RCSID("$MirOS: src/kern/c/strlfun.c,v 1.9 2020/09/06 02:24:02 tg Exp $");

#define NUL			'\0'
#define char_t			char
#define fn_len			strlen
#define	fn_cat			strlcat
#define fn_cpy			strlcpy

#if defined(MBSDPORT_H) || defined(OUTSIDE_OF_LIBKERN)
extern size_t fn_len(const char_t *);
#ifdef NEED_STRLFUN_PROTOS
size_t fn_cat(char_t *, const char_t *, size_t);
size_t fn_cpy(char_t *, const char_t *, size_t);
#endif
#endif

#ifndef __predict_true
#define __predict_true(exp)	(exp)
#define __predict_false(exp)	(exp)
#endif

/*
 * Appends src to string dst of size dlen (unlike strncat, dlen is the
 * full size of dst, not space left).  At most dlen-1 characters
 * will be copied.  Always NUL terminates (unless dlen <= strlen(dst)).
 * Returns strlen(src) + MIN(dlen, strlen(initial dst)), without the
 * trailing NUL byte counted.  If retval >= dlen, truncation occurred.
 */
size_t
fn_cat(char_t *dst, const char_t *src, size_t dlen)
{
	size_t n = 0, slen;

	slen = fn_len(src);
	while (__predict_true(n + 1 < dlen && dst[n] != NUL))
		++n;
	if (__predict_false(dlen == 0 || dst[n] != NUL))
		return (dlen + slen);
	while (__predict_true((slen > 0) && (n < (dlen - 1)))) {
		dst[n++] = *src++;
		--slen;
	}
	dst[n] = NUL;
	return (n + slen);
}
