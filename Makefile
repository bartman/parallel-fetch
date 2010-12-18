
CFLAGS=-Wall -O2
CPPFLAGS=-I.
LDFLAGS=
LIBS=-lpthread

#CFLAGS+=-ggdb -pg -O0

PROG=pf
SRCS=pf_ctx.c pf_http.c pf_main.c pf_run.c
OBJS=$(SRCS:%.c=%.o)
DEPS=$(SRCS:%.c=.%.dep)
EXISTING_DEPS=$(wildcard ${DEPS})

.PHONY: all run clean tags
all: ${DEPS}
	${MAKE} ${PROG}

ifneq (,${EXISTING_DEPS})
include ${EXISTING_DEPS}
endif

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS}

${OBJS}: %.o: %.c Makefile

${DEPS}: .%.dep: %.c Makefile
	${CC} ${CPPFLAGS} -MM $< > $@

run: ${PROG}
	./${PROG}

clean:
	-rm -f *~ ${OBJS} ${DEPS} ${PROG}

tags:
	-ctags -R .
	-cscope -R -b

