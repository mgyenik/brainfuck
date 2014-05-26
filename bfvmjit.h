#define INC     0
#define DEC     1
#define UP      2
#define DOWN    3
#define SLOOP   4
#define ELOOP   5
#define PUTC    6
#define GETC    7
#define END     8

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

#define reverse_endian_store(__ptr, __val) \
    *__ptr++ = __val & 0xff; \
    *__ptr++ = (__val >> 8) & 0xff; \
    *__ptr++ = (__val >>16) & 0xff; \
    *__ptr++ = (__val >>24) & 0xff;

#define INSTRUCTION(__name, ...) \
u8 __name##_bin[] = { __VA_ARGS__ }; \
instruction __name = { \
    .bytes = __name##_bin, \
    .blen = sizeof(__name##_bin)/sizeof(__name##_bin[0]), \
}

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

void runcode(u8 *code, u8 *machine_mem, u8 *getc, u8 *putc);

struct instruction {
    u8 *bytes;
    u8 blen;
};

struct branch {
    u8 *target_loc;
    u8 *patch_loc;
};

struct ir_inst {
    u8 opcode;
    u8 amount;
    u32 offset;
    u32 vpointer;
};

typedef struct instruction instruction;
typedef struct branch branch;
typedef struct ir_inst ir_inst;
