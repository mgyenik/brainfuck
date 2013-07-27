#include <stdio.h>
#include <stdlib.h>

#define EXEC(__pc) goto *ops[decode[*(__pc++)]];

enum {
    LANGLE = 0,
    RANGLE,
    INC,
    DEC,
    PUT,
    GET,
    LBRACE,
    RBRACE,
    INVAL,
};

const int decode[256] = {
    [0 ... 255] = INVAL,
    ['<']       = LANGLE,
    ['>']       = RANGLE,
    ['+']       = INC,
    ['-']       = DEC,
    ['.']       = PUT,
    [',']       = GET,
    ['[']       = LBRACE,
    [']']       = RBRACE,
};

int main(int argc, char **argv) {
    char *pc;
    char *ptr, *base;
    int depth;

    static const void* ops[] = {
        [LANGLE]    = &&langle_handler,
        [RANGLE]    = &&rangle_handler,
        [INC]       = &&inc_handler,
        [DEC]       = &&dec_handler,
        [PUT]       = &&put_handler,
        [GET]       = &&get_handler,
        [LBRACE]    = &&lbrace_handler,
        [RBRACE]    = &&rbrace_handler,
        [INVAL]     = &&inval_handler,
    };

    if (argc != 2) {
        printf("Bugger off\n");
        return -1;
    }

    pc = argv[1];
    ptr = base = malloc(100000);

    EXEC(pc);
    langle_handler:
        --ptr;
        EXEC(pc);
    rangle_handler:
        ++ptr;
        EXEC(pc);
    inc_handler:
        ++*ptr;
        EXEC(pc);
    dec_handler:
        --*ptr;
        EXEC(pc);
    put_handler:
        putchar(*ptr);
        EXEC(pc);
    get_handler:
        *ptr = getchar();
        EXEC(pc);
    lbrace_handler:
        if (*ptr == 0) {
            depth = 0;
            while((*pc != ']') && !depth) {
                if (*pc == '[')
                    depth++;
                if (*pc == ']')
                    depth--;
                pc++;
            }
            pc++;
        }
        EXEC(pc);
    rbrace_handler:
        if (*ptr) {
            depth = 0;
            pc--;
            pc--;
            while((*pc != '[') && !depth) {
                if (*pc == ']')
                    depth++;
                if (*pc == '[')
                    depth--;
                pc--;
            }
            pc++;
        }
        EXEC(pc);
    inval_handler:
    printf("Done\n");
    free(base);
}
