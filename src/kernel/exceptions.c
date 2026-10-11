#include <stdint.h>
#include "exceptions.h"
#include "graphics.h"

#define EXCEPTION_COUNT 32

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[EXCEPTION_COUNT];
static volatile int exception_active;

extern void *exception_stub_table[];

static void stop_cpu(void)
{
    __asm__ volatile ("cli");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void set_idt_gate(uint32_t vector, void *handler)
{
    uint64_t address = (uint64_t)handler;
    uint16_t selector;

    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));

    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = selector;
    idt[vector].ist = 0;
    idt[vector].attributes = 0x8E;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

void exceptions_init(void)
{
    for (uint32_t i = 0; i < EXCEPTION_COUNT; i++) {
        set_idt_gate(i, exception_stub_table[i]);
    }

    struct idt_pointer pointer = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)idt
    };

    __asm__ volatile ("lidt %0" : : "m"(pointer));
}

static void draw_hex(uint64_t value, char *buffer)
{
    static const char digits[] = "0123456789ABCDEF";

    buffer[0] = '0';
    buffer[1] = 'x';

    for (uint32_t i = 0; i < 16; i++) {
        uint32_t shift = 60 - i * 4;
        buffer[i + 2] = digits[(value >> shift) & 0xF];
    }

    buffer[18] = '\0';
}

void exception_handler(uint64_t *frame)
{
    if (exception_active) {
        stop_cpu();
    }

    exception_active = 1;

    /*
     * Stack layout after the assembly register saves:
     * 0-14: saved general-purpose registers
     * 15: exception vector
     * 16: error code
     * 17: instruction pointer (RIP)
     * 18: code segment (CS)
     * 19: flags (RFLAGS)
     */
    uint64_t vector = frame[15];
    uint64_t error = frame[16];
    uint64_t rip = frame[17];

    char value[19];

    gfx_text(20, 400, "NEPTUNE OS CPU EXCEPTION", NC_WHITE, 1);

    gfx_text(20, 420, "VECTOR", NC_WHITE, 1);
    draw_hex(vector, value);
    gfx_text(100, 420, value, NC_YELLOW, 1);

    gfx_text(20, 440, "ERROR", NC_WHITE, 1);
    draw_hex(error, value);
    gfx_text(100, 440, value, NC_YELLOW, 1);

    gfx_text(20, 460, "RIP", NC_WHITE, 1);
    draw_hex(rip, value);
    gfx_text(100, 460, value, NC_YELLOW, 1);

    if (vector == 14) {
        uint64_t cr2;

        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

        gfx_text(20, 480, "PAGE FAULT ADDRESS", NC_WHITE, 1);
        draw_hex(cr2, value);
        gfx_text(180, 480, value, NC_YELLOW, 1);
    }

    stop_cpu();
}
