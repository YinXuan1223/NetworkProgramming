include ../Make.defines

PROGS =	client hard_server

all:	${PROGS}


server: server.o
		${CC} ${CFLAGS} -o $@ server.o ${LIBS}

hard_server: hard_server.o
		${CC} ${CFLAGS} -o $@ hard_server.o ${LIBS}

client: client.o
		${CC} ${CFLAGS} -o $@ client.o ${LIBS} -lncurses


clean:
		rm -f ${PROGS} ${CLEANFILES}
