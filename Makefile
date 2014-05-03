all: interp jit vm

jit:
	gcc -O3 -g3 -std=gnu99 -o bfjit bfjit.c bfjit.S

interp:
	gcc -O3 -g3 -std=gnu99 -o bf bf.c

vm:
	gcc -O3 -g3 -std=gnu99 -o bfjitopt bfjitopt.c bfjit.S

clean:
	rm -f bf bfjit
