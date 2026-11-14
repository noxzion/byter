#ifndef BYTER_A059_BUTTONS_H
#define BYTER_A059_BUTTONS_H

#define BYTER_BUTTON_VOLUME_UP   (1U << 0)
#define BYTER_BUTTON_VOLUME_DOWN (1U << 1)
#define BYTER_BUTTON_POWER      (1U << 2)

/* Read-only PMIC access. Returns zero if this device's arbiter is unsupported. */
int byter_a059_buttons_init(void);
int byter_a059_buttons_read(unsigned *pressed);

#endif
