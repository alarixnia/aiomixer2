# $NetBSD$

PROG=	aiomixer
SRCS+=	main.c draw.c parse.c

LDADD+=	-lcurses
DPADD+=	$(LIBCURSES)

CFLAGS=		-g -Og
CFLAGS+=	-fsanitize=undefined

LDADD+=		-fsanitize=undefined

WARNS= 6

.include <bsd.prog.mk>
