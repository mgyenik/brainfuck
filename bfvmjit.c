#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "bfvmjit.h"

INSTRUCTION(inc, 0x83, 0x80);
INSTRUCTION(dec, 0x83, 0xa8);
INSTRUCTION(zero_check, 0x80, 0xb8);
INSTRUCTION(jne_zero, 0x0f, 0x85);
INSTRUCTION(je_zero, 0x0f, 0x84);
INSTRUCTION(push_machine, 0x50, 0x51, 0x52); /* Save machine state to stack */
INSTRUCTION(pop_machine, 0x5a, 0x59, 0x58); /* Restore machine state from stack */
INSTRUCTION(put_char_prep, 0x48, 0x0f, 0xb6, 0xb8);
INSTRUCTION(put_char_call, 0xff, 0xd1);
INSTRUCTION(get_char_call, 0xff, 0xd2, 0x88, 0xc3);
INSTRUCTION(get_char_save, 0x88, 0x98); /* Must be done after machine state restoration */

struct ir_inst ir_prog[64000];

char *ir_print[9] = {
    [INC]   = "INC  - p:\t%d o:\t%d a:\t%d\n",
    [DEC]   = "DEC  - p:\t%d o:\t%d a:\t%d\n",
    [UP]    = "UP   - p:\t%d o:\t%d a:\t%d\n",
    [DOWN]  = "DOWN - p:\t%d o:\t%d a:\t%d\n",
    [SLOOP] = "SLOOP- p:\t%d o:\t%d a:\t%d\n",
    [ELOOP] = "ELOOP- p:\t%d o:\t%d a:\t%d\n",
    [PUTC]  = "PUTC - p:\t%d o:\t%d a:\t%d\n",
    [GETC]  = "GETC - p:\t%d o:\t%d a:\t%d\n",
    [END]   = "END  - p:\t%d o:\t%d a:\t%d\n",
};

void compaction_pass(struct ir_inst *ir)
{
    while(ir->opcode != END) {
        
    }
}

u8 *copy_bytes(u8 *buffer, instruction inst) {
    for (int i = 0; i < inst.blen; i++) {
        *buffer++ = inst.bytes[i];
    }
    return buffer;
}

u8 *jit_cmp(u8 *buffer, struct ir_inst *ir) {
    buffer = copy_bytes(buffer, zero_check);
    reverse_endian_store(buffer, ir->vpointer);
    *buffer++ = 0;
    return buffer;
}

u8 *jit_save(u8 *buffer) {
    buffer = copy_bytes(buffer, push_machine);
    return buffer;
}

u8 *jit_restore(u8 *buffer) {
    buffer = copy_bytes(buffer, pop_machine);
    return buffer;
}

u8 *jit_inc(u8 *buffer, struct ir_inst *ir) {
    buffer = copy_bytes(buffer, inc);
    reverse_endian_store(buffer, ir->vpointer);
    *buffer++ = ir->amount;
    return buffer;
}

u8 *jit_dec(u8 *buffer, struct ir_inst *ir) {
    buffer = copy_bytes(buffer, dec);
    reverse_endian_store(buffer, ir->vpointer);
    *buffer++ = ir->amount;
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
    buffer = copy_bytes(buffer, push_machine);
    buffer = copy_bytes(buffer, put_char_prep);
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


void translate_to_ir(char *bfprog, struct ir_inst *ir)
{
    u8 *pointer;
    u8 *frame;
    u8 *last_frame;

    pointer = 0;
    frame = 0;
    while (*bfprog) {
        switch (*bfprog) {
            case '>':
                ir->opcode = UP;
                ir->vpointer = pointer;
                ir++;
                /*
                pointer++;
                */
                bfprog++;
                break;
            case '<':
                ir->opcode = DOWN;
                ir->vpointer = pointer;
                ir++;
                /*
                pointer--;
                */
                bfprog++;
                break;
            case '+':
                ir->opcode = INC;
                while(*bfprog == '+') {
                    ir->amount++;
                    bfprog++;
                }

                ir->vpointer = pointer;
                ir->offset = pointer - frame;
                ir++;
                break;
            case '-':
                ir->opcode = DEC;
                while(*bfprog == '-') {
                    ir->amount++;
                    bfprog++;
                }

                ir->vpointer = pointer;
                ir->offset = pointer - frame;
                ir++;
                break;
            case '[':
                ir->opcode = SLOOP;
                ir->vpointer = pointer;
                ir++;
                bfprog++;
                break;
            case ']':
                ir->opcode = ELOOP;
                ir->vpointer = pointer;
                ir++;
                bfprog++;
                break;
            case '.':
                ir->opcode = PUTC;
                ir->vpointer = pointer;
                ir++;
                bfprog++;
                break;
            case ',':
                ir->opcode = GETC;
                ir->vpointer = pointer;
                ir++;
                bfprog++;
                break;
            default:
                bfprog++;
                break;
        }
    }
    ir++; /* BUG? Source cannot end with a comment */
    ir->opcode = END;
}

int main(int argc, char **argv) {
    branch *brstack;
    branch closer;
    char *bfp;
    char *mem;
    int count;
    struct ir_inst *ir_count;


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
    translate_to_ir(bfp, &ir_prog[0]);
    ir_count = ir_prog;

    while(ir_count->opcode != END) {
        printf(ir_print[ir_count->opcode], (unsigned int)ir_count->vpointer, ir_count->offset, ir_count->amount);
        /*
        switch(ir_count->opcode) {
            case INC:
                break;
            case DEC:
                break;
            case UP:
                break;
            case DOWN:
                break;
            case SLOOP:
                break;
            case ELOOP:
                break;
            case PUTC:
                break;
            case GETC:
                break;
            default:
                break;
        }
        */
        ir_count++;
    }
    *ptr++ = 0xc3; /* RETQ */
    //runcode(code, mem, (u8 *)&putchar, (u8 *)&getchar);
    munmap(code, 128*1024);
    free(mem);
    free(brstack);
}
