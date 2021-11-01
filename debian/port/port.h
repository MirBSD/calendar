/*-
 * Copyright © 2004, 2006, 2009, 2010, 2020, 2021
 *	mirabilos <m@mirbsd.org>
 *
 * Provided that these terms and disclaimer and all copyright notices
 * are retained or reproduced in an accompanying document, permission
 * is granted to deal in this work without restriction, including un‐
 * limited rights to use, publicly perform, distribute, sell, modify,
 * merge, give away, or sublicence.
 *
 * This work is provided “AS IS” and WITHOUT WARRANTY of any kind, to
 * the utmost extent permitted by applicable law, neither express nor
 * implied; without malicious intent or gross negligence. In no event
 * may a licensor, author or contributor be held liable for indirect,
 * direct, other damage, loss, or other issues arising in any way out
 * of dealing in the work, even if advised of the possibility of such
 * damage or existence of a defect, except proven that it results out
 * of said person’s immediate fault when using the work as intended.
 */

#ifndef DEBIAN_CALENDAR_MIRBSD_PORT_H
#define DEBIAN_CALENDAR_MIRBSD_PORT_H

/* really sys/cdefs.h but that’s less portable */
#include <sys/types.h>

/* now for stuff from our cdefs */

#if defined(MirBSD) && (MirBSD >= 0x09A1) && \
    defined(__ELF__) && defined(__GNUC__) && \
    !defined(__llvm__) && !defined(__NWCC__)
/*
 * We got usable __IDSTRING __COPYRIGHT __RCSID __SCCSID macros
 * which work for all cases; no need to redefine them using the
 * "portable" macros from below when we might have the "better"
 * gcc+ELF specific macros or other system dependent ones.
 */
#else
#undef __IDSTRING
#undef __IDSTRING_CONCAT
#undef __IDSTRING_EXPAND
#undef __COPYRIGHT
#undef __RCSID
#undef __SCCSID
#define __IDSTRING_CONCAT(l,p)		__LINTED__ ## l ## _ ## p
#define __IDSTRING_EXPAND(l,p)		__IDSTRING_CONCAT(l,p)
#ifdef MKSH_DONT_EMIT_IDSTRING
#define __IDSTRING(prefix, string)	/* nothing */
#elif defined(__ELF__) && defined(__GNUC__) && \
    !(defined(__GNUC__) && defined(__mips16) && (__GNUC__ >= 8)) && \
    !defined(__llvm__) && !defined(__NWCC__) && !defined(NO_ASM)
#define __IDSTRING(prefix, string)				\
	__asm__(".section .comment"				\
	"\n	.ascii	\"@(\"\"#)" #prefix ": \""		\
	"\n	.asciz	\"" string "\""				\
	"\n	.previous")
#else
#define __IDSTRING(prefix, string)				\
	static const char __IDSTRING_EXPAND(__LINE__,prefix) []	\
	    __attribute__((__used__)) = "@(""#)" #prefix ": " string
#endif
#define __COPYRIGHT(x)		__IDSTRING(copyright,x)
#define __RCSID(x)		__IDSTRING(rcsid,x)
#define __SCCSID(x)		__IDSTRING(sccsid,x)
#endif

#ifndef __BEGIN_DECLS
#define __BEGIN_DECLS		/* nothing */
#define __END_DECLS		/* unless this is CFrustFrust */
#endif

#ifndef __dead
#define __dead			__attribute__((__noreturn__))
#endif

#ifndef __unused
#define __unused		__attribute__((__unused__))
#endif

/* above was stuff from MirBSD <sys/cdefs.h> */

__BEGIN_DECLS
size_t strlcat(char *, const char *, size_t);
__END_DECLS

#define arc4random_uniform(three) __extension__({	\
	unsigned char rndbyte;				\
							\
	if (getentropy(&rndbyte, sizeof(rndbyte)))	\
		err(1, "getentropy");			\
	/* ignore minimal bias towards bodun’ing */	\
	(rndbyte % 3U);					\
})

/* causes #998206 but is same as the calendar package does */
#define userswitch(pw)	0

#endif /* !DEBIAN_CALENDAR_MIRBSD_PORT_H */
