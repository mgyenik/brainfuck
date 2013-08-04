#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define MAX_RECURSE 128
#define valid(__c) ( \
    (__c == '>') || \
    (__c == '<') || \
    (__c == '+') || \
    (__c == '-') || \
    (__c == '[') || \
    (__c == ']') || \
    (__c == '.') || \
    (__c == ',') \
    )

#define INSTRUCTION(__name, ...) \
u8 __name##_bin[] = { __VA_ARGS__ }; \
instruction __name = { \
    .bytes = __name##_bin, \
    .blen = sizeof(__name##_bin)/sizeof(__name##_bin[0]), \
}

typedef unsigned char u8;
typedef unsigned int u32;

extern void runcode(u8 *code, u8 *machine_mem, u8 *getc, u8 *putc);

struct instruction {
    u8 *bytes;
    u8 blen;
};

struct branch {
    u8 *target_loc;
    u8 *patch_loc;
};

typedef struct instruction instruction;
typedef struct branch branch;

INSTRUCTION(inc_ptr, 0x48, 0x05);
INSTRUCTION(dec_ptr, 0x48, 0x2d);
INSTRUCTION(inc, 0x80, 0x00);
INSTRUCTION(dec, 0x80, 0x28);
INSTRUCTION(jne_zero, 0x80, 0x38, 0x00, 0x0f, 0x85);
INSTRUCTION(je_zero, 0x80, 0x38, 0x00, 0x0f, 0x84);
INSTRUCTION(put_char, 0x50, 0x51, 0x52, 0x48, 0x0f, 0xb6, 0x38, 0xff, 0xd2, 0x5a, 0x59, 0x58);
INSTRUCTION(get_char, 0x50, 0x51, 0x52, 0x48, 0x0f, 0xb6, 0x38, 0xff, 0xd1, 0x88, 0xc3, 0x5a, 0x59, 0x58, 0x88, 0x18);

u8 *copy_bytes(u8 *buffer, instruction inst) {
    for (int i = 0; i < inst.blen; i++) {
        *buffer++ = inst.bytes[i];
    }
    return buffer;
}

u8 *jit_inc(u8 *buffer, u8 amount) {
    buffer = copy_bytes(buffer, inc);
    *buffer++ = amount;
    return buffer;
}

u8 *jit_dec(u8 *buffer, u8 amount) {
    buffer = copy_bytes(buffer, dec);
    *buffer++ = amount;
    return buffer;
}

u8 *jit_inc_ptr(u8 *buffer, u32 amount) {
    buffer = copy_bytes(buffer, inc_ptr);
    *buffer++ = amount & 0xff;
    *buffer++ = (amount >> 8) & 0xff;
    *buffer++ = (amount >>16) & 0xff;
    *buffer++ = (amount >>24) & 0xff;
    return buffer;
}

u8 *jit_dec_ptr(u8 *buffer, u32 amount) {
    buffer = copy_bytes(buffer, dec_ptr);
    *buffer++ = amount & 0xff;
    *buffer++ = (amount >> 8) & 0xff;
    *buffer++ = (amount >>16) & 0xff;
    *buffer++ = (amount >>24) & 0xff;
    return buffer;
}

u8 *jit_start_loop(u8 *buffer, branch *b) {
    buffer = copy_bytes(buffer, je_zero);
    b->patch_loc = buffer;
    buffer += 4;
    b->target_loc = buffer;
    return buffer;
}

u8 *jit_end_loop(u8 *buffer, branch *b) {
    buffer = copy_bytes(buffer, jne_zero);
    b->patch_loc = buffer;
    buffer += 4;
    b->target_loc = buffer;
    return buffer;
}

u8 *jit_put(u8 *buffer) {
    buffer = copy_bytes(buffer, put_char);
    return buffer;
}

u8 *jit_get(u8 *buffer) {
    buffer = copy_bytes(buffer, get_char);
    return buffer;
}

void link_branches(branch *b_start, branch *b_end) {
    u8 *patch;
    u32 patchval;

    patchval = b_end->target_loc - b_start->target_loc;
    patch = b_start->patch_loc;
    *patch++ = patchval & 0xff;
    *patch++ = (patchval >> 8) & 0xff;
    *patch++ = (patchval >>16) & 0xff;
    *patch++ = (patchval >>24) & 0xff;

    patchval = -patchval;
    patch = b_end->patch_loc;
    *patch++ = patchval & 0xff;
    *patch++ = (patchval >> 8) & 0xff;
    *patch++ = (patchval >>16) & 0xff;
    *patch++ = (patchval >>24) & 0xff;
};

char *get_reps(char *ptr, char c, int *count) {
    int total = 0;

    while(*ptr && ((*ptr == c) || !valid(*ptr))) {
        if(valid(*ptr))
            total++;
        ptr++;
    }
    *count = total;
    return --ptr; /* lol */
}

int main(int argc, char **argv) {
    branch *brstack;
    branch closer;
    char *bfp;
    char *mem;
    int count;


    if(argc != 2) {
        printf("Bugger off, need 2 args but you gave %d\n", argc);
        return -1;
    }

    u8 *code = mmap(NULL, 128*1024, PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
    u8 *ptr = code;
    if(code == (u8 *)-1) {
        printf("Could not mmap!\n");
        return -1;
    }

    mem = malloc(30000);
    if(!mem) {
        printf("Could not allocate machine memory!\n");
        return -1;
    }

    brstack = malloc(sizeof(branch)*MAX_RECURSE);
    if(!brstack) {
        printf("Could not allocate branch stack!\n");
        return -1;
    }


    bfp = argv[1];
    while(*bfp) {
        switch(*bfp) {
            case '>':
                bfp = get_reps(bfp, '>', &count);
                ptr = jit_inc_ptr(ptr, count);
                break;
            case '<':
                bfp = get_reps(bfp, '<', &count);
                ptr = jit_dec_ptr(ptr, count);
                break;
            case '+':
                bfp = get_reps(bfp, '+', &count);
                ptr = jit_inc(ptr, count);
                break;
            case '-':
                bfp = get_reps(bfp, '-', &count);
                ptr = jit_dec(ptr, count);
                break;
            case '[':
                ptr = jit_start_loop(ptr, brstack++);
                break;
            case ']':
                ptr = jit_end_loop(ptr, &closer);
                link_branches(--brstack, &closer);
                break;
            case '.':
                ptr = jit_put(ptr);
                break;
            case ',':
                ptr = jit_get(ptr);
                break;
            default:
                break;
        }
        bfp++;
    }
    *ptr++ = 0xc3; /* RETQ */
    runcode(code, mem, (u8 *)&putchar, (u8 *)&getchar);
    munmap(code, 128*1024);
    free(mem);
    free(brstack);
}
