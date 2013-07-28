jit:
	gcc -O3 -g3 -std=gnu99 -o bfjit bfjit.c bfjit.S
    
interp:
	gcc -O3 -g3 -std=gnu99 -o bf bf.c

all: interp jit

clean:
	rm -f bf bfjit
