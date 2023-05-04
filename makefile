SRC=ngcubeslib.c log.c pack.c
OBJ=$(subst .c,.o,$(SRC))
all: ngcubeslib

%.o: %.c
	gcc -Wall -g -c $< -o $@

ngcubeslib: $(OBJ)
	gcc -Wall -g -o $@ $+

clean:
	-rm *.o ngcubeslib 