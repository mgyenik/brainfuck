#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

//#define TRACE

#ifdef TRACE
#define trace printf
#else
#define trace(...)
#endif

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

#define put32(__b, __v) \
    *(__b)++ = (__v) & 0xff; \
    *(__b)++ = ((__v) >> 8) & 0xff; \
    *(__b)++ = ((__v) >>16) & 0xff; \
    *(__b)++ = ((__v) >>24) & 0xff;

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
    int frameval;
};

u8 unroller_counts[256];
signed char accarray[60000];
signed char *accumulator = &accarray[29999];

typedef struct instruction instruction;
typedef struct branch branch;

INSTRUCTION(inc, 0x80, 0x80);
INSTRUCTION(dec, 0x80, 0xa8);
INSTRUCTION(zero_check, 0x80, 0x38);
INSTRUCTION(frame_add, 0x48, 0x05);
INSTRUCTION(frame_sub, 0x48, 0x2d);
INSTRUCTION(jne_zero, 0x0f, 0x85);
INSTRUCTION(je_zero, 0x0f, 0x84);
INSTRUCTION(push_machine, 0x50, 0x51, 0x52); /* Save machine state to stack */
INSTRUCTION(pop_machine, 0x5a, 0x59, 0x58); /* Restore machine state from stack */
INSTRUCTION(put_char_prep, 0x48, 0x0f, 0xb6, 0xb8);
INSTRUCTION(put_char_call, 0xff, 0xd2);
INSTRUCTION(get_char_call, 0xff, 0xd1, 0x88, 0xc3);
INSTRUCTION(get_char_save, 0x88, 0x98); /* Must be done after machine state restoration */
INSTRUCTION(unroll_loop_begin, 0x49, 0x89, 0xc2);
INSTRUCTION(unroll_loop_mul, 0x41, 0x8a, 0x02, 0x41, 0xf6, 0xa2);
INSTRUCTION(unroll_loop_sto, 0x41, 0x88, 0x82);
INSTRUCTION(unroll_loop_end, 0x4c, 0x89, 0xd0);
INSTRUCTION(clear, 0xc6, 0x80);

u8 *copy_bytes(u8 *buffer, instruction inst) {
    for (int i = 0; i < inst.blen; i++) {
        *buffer++ = inst.bytes[i];
    }
    return buffer;
}

u8 *jit_inc(u8 *buffer, u8 amount, int offset) {
    buffer = copy_bytes(buffer, inc);
    put32(buffer, offset);
    *buffer++ = amount;
    return buffer;
}

u8 *jit_frame_add(u8 *buffer, int amount) {
    buffer = copy_bytes(buffer, frame_add);
    put32(buffer, amount);
    return buffer;
}

u8 *jit_frame_sub(u8 *buffer, int amount) {
    buffer = copy_bytes(buffer, frame_sub);
    put32(buffer, amount);
    return buffer;
}

u8 *jit_dec(u8 *buffer, u8 amount, int offset) {
    buffer = copy_bytes(buffer, dec);
    put32(buffer, offset);
    *buffer++ = amount;
    return buffer;
}

u8 *jit_start_loop(u8 *buffer, branch *b, int offset) {
    buffer = copy_bytes(buffer, zero_check);
    *buffer++ = 0;
    buffer = copy_bytes(buffer, je_zero);
    b->patch_loc = buffer;
    buffer += 4;
    b->target_loc = buffer;
    return buffer;
}

u8 *jit_end_loop(u8 *buffer, branch *b, int offset) {
    buffer = copy_bytes(buffer, zero_check);
    *buffer++ = 0;
    buffer = copy_bytes(buffer, jne_zero);
    b->patch_loc = buffer;
    buffer += 4;
    b->target_loc = buffer;
    return buffer;
}

u8 *jit_put(u8 *buffer, int offset) {
    buffer = copy_bytes(buffer, push_machine);
    buffer = copy_bytes(buffer, put_char_prep);
    put32(buffer, offset);
    buffer = copy_bytes(buffer, put_char_call);
    buffer = copy_bytes(buffer, pop_machine);
    return buffer;
}

u8 *jit_get(u8 *buffer, int offset) {
    buffer = copy_bytes(buffer, push_machine);
    buffer = copy_bytes(buffer, get_char_call);
    buffer = copy_bytes(buffer, pop_machine);
    buffer = copy_bytes(buffer, get_char_save);
    put32(buffer, offset);
    return buffer;
}

u8 *jit_clear(u8 *buffer, int offset) {
    buffer = copy_bytes(buffer, clear);
    put32(buffer, offset);
    *buffer++ = 0x00;
    return buffer;
}

void link_branches(branch *b_start, branch *b_end) {
    u8 *patch;
    u32 patchval;

    patchval = b_end->target_loc - b_start->target_loc;
    patch = b_start->patch_loc;
    put32(patch, patchval);

    patchval = -patchval;
    patch = b_end->patch_loc;
    put32(patch, patchval);
};

u8 *emit_frame(u8 *buffer, signed char *acc, int *min, int *max, int *offset)
{
    int i;

    for (i = *min; i <= *max; i++) {
        if (acc[i] > 0) {
            buffer = jit_inc(buffer, acc[i], i);
        }
        if (acc[i] < 0) {
            buffer = jit_dec(buffer, -acc[i], i);
        }
        acc[i] = 0;
    }
    if (*offset > 0) {
        buffer = jit_frame_add(buffer, *offset);
    }
    if (*offset < 0) {
        buffer = jit_frame_sub(buffer, -*offset);
    }
    *offset = 0;
        
    *min = 30000;
    *max = 0;
    return buffer;
}

int balanced_loop_unroll(char **loop_start, u8 **buffer, int *vp) {
    char *ptr;
    int vpointer;
    u8 counts[256];

    ptr = *loop_start + 1;
    memset(counts, 0, 256);

    while(*ptr != ']') {
        if (*ptr == '[') {
            trace("Loop not inner-most loop\n");
            return 0; /* Fail out, can't do nested loops yet */
        }
        counts[*ptr]++;
        ptr++;
    }

    if (counts['>'] == counts['<']) {
        if (counts['>'] == 0) {
            *buffer = jit_clear(*buffer, *vp);
            *loop_start = ptr;
            trace("deleting [-], %c%c%c|%c|%c%c%c\n",
                *(ptr-3),
                *(ptr-2),
                *(ptr-1),
                *(ptr),
                *(ptr+1),
                *(ptr+2),
                *(ptr+3));
            return 1;
        }
        trace("Balanced loop detected: %d\n", counts['>']);
        return 0; /* CHANGEME TODO XXX */
    }

    trace("Loop not balanced\n");
    return 0;
}

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
    int vpointer;
    int frame;
    int min;
    int max;
    int startloops;
    int endloops;

    startloops = 0;
    endloops = 0;
    vpointer = 0;
    min = 30000;
    max = 0;
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
                vpointer++;
                break;
            case '<':
                vpointer--;
                break;
            case '+':
                accumulator[vpointer]++;
                min = (vpointer < min) ? vpointer : min;
                max = (vpointer > max) ? vpointer : max;
                break;
            case '-':
                accumulator[vpointer]--;
                min = (vpointer < min) ? vpointer : min;
                max = (vpointer > max) ? vpointer : max;
                break;
            case '[':
                ptr = emit_frame(ptr, accumulator, &min, &max, &vpointer);
                if (!balanced_loop_unroll(&bfp, &ptr, &vpointer)) {
                    ptr = jit_start_loop(ptr, brstack++, vpointer);
                    startloops++;
                }
                break;
            case ']':
                ptr = emit_frame(ptr, accumulator, &min, &max, &vpointer);
                ptr = jit_end_loop(ptr, &closer, vpointer);
                link_branches(--brstack, &closer);
                endloops++;
                break;
            case '.':
                ptr = emit_frame(ptr, accumulator, &min, &max, &vpointer);
                ptr = jit_put(ptr, vpointer);
                break;
            case ',':
                ptr = emit_frame(ptr, accumulator, &min, &max, &vpointer);
                ptr = jit_get(ptr, vpointer);
                break;
            default:
                break;
        }
        bfp++;
    }
    *ptr++ = 0xc3; /* RETQ */
    trace("startloops: %d endloops: %d\n", startloops, endloops);
    runcode(code, mem, (u8 *)&putchar, (u8 *)&getchar);
    munmap(code, 128*1024);
    free(mem);
    free(brstack);
}
