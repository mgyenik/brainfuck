#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    int frameval;
};

u8 unroller_counts[256];
signed char accumulator[30000];

typedef struct instruction instruction;
typedef struct branch branch;

INSTRUCTION(inc, 0x83, 0x80);
INSTRUCTION(dec, 0x83, 0xa8);
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

u8 *copy_bytes(u8 *buffer, instruction inst) {
    for (int i = 0; i < inst.blen; i++) {
        *buffer++ = inst.bytes[i];
    }
    return buffer;
}

u8 *jit_inc(u8 *buffer, u8 amount, int offset) {
    buffer = copy_bytes(buffer, inc);
    *buffer++ = offset & 0xff;
    *buffer++ = (offset >> 8) & 0xff;
    *buffer++ = (offset >>16) & 0xff;
    *buffer++ = (offset >>24) & 0xff;
    *buffer++ = amount;
    return buffer;
}

u8 *jit_frame_add(u8 *buffer, int amount) {
    buffer = copy_bytes(buffer, frame_add);
    *buffer++ = amount & 0xff;
    *buffer++ = (amount >> 8) & 0xff;
    *buffer++ = (amount >>16) & 0xff;
    *buffer++ = (amount >>24) & 0xff;
    return buffer;
}

u8 *jit_frame_sub(u8 *buffer, int amount) {
    buffer = copy_bytes(buffer, frame_sub);
    *buffer++ = amount & 0xff;
    *buffer++ = (amount >> 8) & 0xff;
    *buffer++ = (amount >>16) & 0xff;
    *buffer++ = (amount >>24) & 0xff;
    return buffer;
}

u8 *jit_dec(u8 *buffer, u8 amount, int offset) {
    buffer = copy_bytes(buffer, dec);
    *buffer++ = offset & 0xff;
    *buffer++ = (offset >> 8) & 0xff;
    *buffer++ = (offset >>16) & 0xff;
    *buffer++ = (offset >>24) & 0xff;
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
    *buffer++ = offset & 0xff;
    *buffer++ = (offset >> 8) & 0xff;
    *buffer++ = (offset >>16) & 0xff;
    *buffer++ = (offset >>24) & 0xff;
    buffer = copy_bytes(buffer, put_char_call);
    buffer = copy_bytes(buffer, pop_machine);
    return buffer;
}

u8 *jit_get(u8 *buffer, int offset) {
    buffer = copy_bytes(buffer, get_char_call);
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

char *balanced_loop_unroll(char *loop_start) {
    char *ptr = 0;
    u8 counts[256];

    ptr = loop_start;
    memset(counts, 0, 256);

    while(*ptr != ']') {
        if (*ptr == '[') {
            balanced_loop_unroll(ptr);
            
        }
        counts[*ptr]++;
        ptr++;
    }

    if (counts['>'] == counts['<'])
        printf("Balanced loop detected: %d\n", counts['>']);

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
                //bfp = get_reps(bfp, '>', &count);
                //ptr = jit_inc_ptr(ptr, count);
                vpointer++;
                break;
            case '<':
                //bfp = get_reps(bfp, '<', &count);
                //ptr = jit_dec_ptr(ptr, count);
                vpointer--;
                break;
            case '+':
                //bfp = get_reps(bfp, '+', &count);
                accumulator[vpointer]++;
                min = (vpointer < min) ? vpointer : min;
                max = (vpointer > max) ? vpointer : max;
                //ptr = jit_inc(ptr, count);
                break;
            case '-':
                //bfp = get_reps(bfp, '-', &count);
                accumulator[vpointer]--;
                min = (vpointer < min) ? vpointer : min;
                max = (vpointer > max) ? vpointer : max;
                //ptr = jit_dec(ptr, count);
                break;
            case '[':
                //balanced_loop_unroll(bfp);
                ptr = emit_frame(ptr, accumulator, &min, &max, &vpointer);
                ptr = jit_start_loop(ptr, brstack++, vpointer);
                break;
            case ']':
                ptr = emit_frame(ptr, accumulator, &min, &max, &vpointer);
                ptr = jit_end_loop(ptr, &closer, vpointer);
                link_branches(--brstack, &closer);
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
    runcode(code, mem, (u8 *)&putchar, (u8 *)&getchar);
    munmap(code, 128*1024);
    free(mem);
    free(brstack);
}
