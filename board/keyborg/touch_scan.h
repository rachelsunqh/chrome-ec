/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Touch scanning module */

#ifndef __BOARD_KEYBORG_TOUCH_SCAN_H
#define __BOARD_KEYBORG_TOUCH_SCAN_H

enum pin_type {
	PIN_ROW,
	PIN_COL,
	PIN_PD,
	PIN_Z,
};

/* 8-bit window */
#define ADC_WINDOW_POS 2

/* Threshold for each cell */
#define THRESHOLD 35

/* Threshold for entire column */
#define COL_THRESHOLD 16

/* The span width of active columns */
#define COL_SPAN 5 /* Must not exceed 15 */

/* ADC speed */
#define ADC_SMPR_VAL 0x2 /* 13.5 cycles */
#define ADC_SMPL_CYCLE_2 27
#define ADC_QUNTZ_CYCLE_2 25 /* Quantization always takes 12.5 cycles */

/* CPU clock is 4 times faster than ADC clock */
#define ADC_SMPL_CPU_CYCLE (ADC_SMPL_CYCLE_2 * 2)

struct ts_pin {
	uint8_t port_id; /* GPIO_A = 0, GPIO_B = 1, ... */
	uint8_t pin;
};

#define TS_GPIO_A 0
#define TS_GPIO_B 1
#define TS_GPIO_C 2
#define TS_GPIO_D 3
#define TS_GPIO_E 4
#define TS_GPIO_F 5
#define TS_GPIO_G 6
#define TS_GPIO_H 7
#define TS_GPIO_I 8

extern const struct ts_pin row_pins[];
extern const struct ts_pin col_pins[];

#define ROW_COUNT 41
#define COL_COUNT 60

/* Initialize touch scan module */
void touch_scan_init(void);

/* Start scanning on the slave. This is called by SPI command handler. */
void touch_scan_slave_start(void);

/**
 * Initiate full matrix scan on the master. This also sends SPI command
 * to the slave.
 */
int touch_scan_full_matrix(void);

/* Configure touch scan module to interrupt on touch. */
void touch_scan_enable_interrupt(void);

/* Disable touch scan interrupt. */
void touch_scan_disable_interrupt(void);

#endif /* __BOARD_KEYBORG_TOUCH_SCAN_H */
