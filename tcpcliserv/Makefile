include ../Make.defines

PROGS =	demo_server demo_client client hard_server

all:	${PROGS}

demo_server: demo_server.o
		${CC} ${CFLAGS} -o $@ demo_server.o ${LIBS}

demo_client: demo_client.o
		${CC} ${CFLAGS} -o $@ demo_client.o ${LIBS} -lncurses

server: server.o
		${CC} ${CFLAGS} -o $@ server.o ${LIBS}

hard_server: hard_server.o
		${CC} ${CFLAGS} -o $@ hard_server.o ${LIBS}

client: client.o
		${CC} ${CFLAGS} -o $@ client.o ${LIBS} -lncurses


clean:
		rm -f ${PROGS} ${CLEANFILES}
