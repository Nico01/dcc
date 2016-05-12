# Signature tools makefile
CC = clang
CFLAGS += -Wall

all: srchsig dispsig makedsig parsehdr makedstp readsig

srchsig: srchsig.o perfhlib.o fixwild.o
	${CC} ${CFLAGS} $^ -o $@

dispsig: dispsig.o perfhlib.o
	${CC} ${CFLAGS} $^ -o $@

makedsig: makedsig.o perfhlib.o fixwild.o
	${CC} ${CFLAGS} $^ -o $@

parsehdr: parsehdr.o
	${CC} ${CFLAGS} $^ -o $@

makedstp: makedstp.o perfhlib.o fixwild.o
	${CC} ${CFLAGS} $^ -o $@

readsig: readsig.o perfhlib.o
	${CC} ${CFLAGS} $^ -o $@


%.o: %.c
	${CC} ${CFLAGS} -c $^ -o $@

.PHONY: clean
clean:
	rm -f *.o srchsig dispsig makedsig parsehdr makedstp readsig
