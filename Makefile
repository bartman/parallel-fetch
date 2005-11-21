
CFLAGS=-ggdb -O01 -Wall

PROG=pf
SRCS=pf_ctx.c pf_http.c pf_main.c pf_run.c
OBJS=$(SRCS:%.c=%.o)

${PROG}: ${OBJS}
	${CC} -o $@ $^

${OBJS}: %.o: %.c Makefile

run: ${PROG}
	./${PROG}

clean:
	-rm -f *~ ${OBJS} ${PROG}
