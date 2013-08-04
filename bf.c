#include <stdio.h>
#include <stdlib.h>

#define MAX_RECURSE 64

#define EXEC(__pc)          goto *ops[decode[*(__pc++)]];
#define PUSH(__stk)         *++__stk##_stack = pc

enum {
    LANGLE = 0,
    RANGLE,
    INC,
    DEC,
    PUT,
    GET,
    LBRACE,
    RBRACE,
    END,
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
    ['\0']      = END,
};

int main(int argc, char **argv) {
    char *pc;
    char *rbrace;
    unsigned char *ptr, *base;
    int depth;
    char **left_stack, **left_base;

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
        [END]       = &&end_handler,
    };

    if (argc != 2) {
        printf("Bugger off, need 2 args and you have %d\n", argc);
        return -1;
    }

    pc = argv[1];
    ptr = base = malloc(30000);
    if(!base) {
        printf("Could not malloc base!\n");
        return -1;
    }

    left_stack = left_base = malloc(sizeof(char *)*MAX_RECURSE);
    if(!left_stack) {
        printf("Could not malloc bracket stack!\n");
        return -1;
    }

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
            while((*pc != ']') || depth) {
                if (*pc == '[')
                    depth++;
                if (*pc == ']')
                    depth--;
                pc++;
            }
            pc++;
            EXEC(pc);
        }
        else {
            PUSH(left);
        }
        EXEC(pc);
    rbrace_handler:
        if (*ptr)
            pc = *left_stack;
        else
            left_stack--;
        EXEC(pc);
    inval_handler:
        EXEC(pc);
    end_handler:
        free(base);
        free(left_base);
        return 0;
}
