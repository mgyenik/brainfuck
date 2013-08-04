all: interp jit

jit:
	gcc -O3 -g3 -std=gnu99 -o bfjit bfjit.c bfjit.S

interp:
	gcc -O3 -g3 -std=gnu99 -o bf bf.c

clean:
	rm -f bf bfjit
