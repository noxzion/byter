#include "boot_menu.h"
#include "console.h"
#include "../platform/a059/buttons.h"

typedef unsigned long long u64;
typedef unsigned int u32;

#define MENU_TIMEOUT_SECONDS 5U
#define POLL_INTERVAL_MS 20U

static u64 counter(void) {
    u64 value;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

static u64 counter_frequency(void) {
    u64 value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

static void wait_ms(u32 milliseconds, u64 frequency) {
    u64 ticks = frequency / 1000 * milliseconds;
    u64 start = counter();
    if (frequency && ticks) {
        while (counter() - start < ticks) __asm__ volatile("yield");
    } else {
        for (volatile u32 i = 0; i < milliseconds * 20000U; ++i)
            __asm__ volatile("yield");
    }
}

static void show_menu(u32 seconds_left) {
    byter_console_clear();
    byter_console_write("BYTER BOOT MENU\n\n");
    byter_console_write("  Nothing Phone (3a)\n\n");
    byter_console_write("  > Start configured ELF\n\n");
    byter_console_write("  VOL +/-  reset timer   POWER  boot\n");
    byter_console_write("  Automatic boot in ");
    char digit[2] = {(char)('0' + seconds_left), 0};
    byter_console_write(digit);
    byter_console_write(" seconds\n");
}

int byter_boot_menu_wait(void) {
    if (!byter_console_clear()) return 1;
    byter_console_write("Checking phone buttons...\n");
    if (!byter_a059_buttons_init()) {
        byter_console_write("Buttons unavailable; continuing boot.\n");
        return 1;
    }

    u64 frequency = counter_frequency();
    if (!frequency || frequency > ~0ULL / MENU_TIMEOUT_SECONDS) {
        byter_console_write("Timer unavailable; continuing boot.\n");
        return 1;
    }
    u64 timeout_ticks = frequency * MENU_TIMEOUT_SECONDS;
    u64 start = counter();
    unsigned previous = 0;
    u32 shown_seconds = MENU_TIMEOUT_SECONDS + 1;

    for (;;) {
        unsigned pressed;
        if (!byter_a059_buttons_read(&pressed)) {
            byter_console_write("Button read failed; continuing boot.\n");
            return 1;
        }
        unsigned down = pressed & ~previous;
        previous = pressed;
        if (down & BYTER_BUTTON_POWER) {
            byter_console_write("Power selected the configured ELF.\n");
            return 1;
        }
        /* There is one configured ELF for now, so volume keys restart its timer. */
        if (down & (BYTER_BUTTON_VOLUME_UP | BYTER_BUTTON_VOLUME_DOWN)) {
            start = counter();
            shown_seconds = MENU_TIMEOUT_SECONDS + 1;
        }

        u64 elapsed = counter() - start;
        if (frequency && elapsed >= timeout_ticks) return 1;
        u32 left = MENU_TIMEOUT_SECONDS;
        if (frequency) {
            u64 remaining = timeout_ticks - elapsed;
            left = (u32)((remaining + frequency - 1) / frequency);
        }
        if (left != shown_seconds) {
            show_menu(left);
            shown_seconds = left;
        }
        wait_ms(POLL_INTERVAL_MS, frequency);
    }
}
