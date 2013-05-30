/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdio.h>
#include <sys/io.h>
#include <sys/param.h>
#include <unistd.h>

#include "comm-host.h"

#define INITIAL_UDELAY 5     /* 5 us */
#define MAXIMUM_UDELAY 10000 /* 10 ms */

/*
 * Wait for the EC to be unbusy.  Returns 0 if unbusy, non-zero if
 * timeout.
 */
static int wait_for_ec(int status_addr, int timeout_usec)
{
	int i;
	int delay = INITIAL_UDELAY;

	for (i = 0; i < timeout_usec; i += delay) {
		/*
		 * Delay first, in case we just sent out a command but the EC
		 * hasn't raise the busy flag. However, I think this doesn't
		 * happen since the LPC commands are executed in order and the
		 * busy flag is set by hardware.
		 *
		 * TODO: move this delay after inb(status).
		 */
		usleep(MIN(delay, timeout_usec - i));

		if (!(inb(status_addr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;

		/* Increase the delay interval after a few rapid checks */
		if (i > 20)
			delay = MIN(delay * 2, MAXIMUM_UDELAY);
	}
	return -1;  /* Timeout */
}

static int ec_command_lpc(int command, int version,
			  const void *outdata, int outsize,
			  void *indata, int insize)
{
	struct ec_lpc_host_args args;
	const uint8_t *d;
	uint8_t *dout;
	int csum;
	int i;

	/* Fill in args */
	args.flags = EC_HOST_ARGS_FLAG_FROM_HOST;
	args.command_version = version;
	args.data_size = outsize;

	/* Initialize checksum */
	csum = command + args.flags + args.command_version + args.data_size;

	/* Write data and update checksum */
	for (i = 0, d = (uint8_t *)outdata; i < outsize; i++, d++) {
		outb(*d, EC_LPC_ADDR_HOST_PARAM + i);
		csum += *d;
	}

	/* Finalize checksum and write args */
	args.checksum = (uint8_t)csum;
	for (i = 0, d = (const uint8_t *)&args; i < sizeof(args); i++, d++)
		outb(*d, EC_LPC_ADDR_HOST_ARGS + i);

	outb(command, EC_LPC_ADDR_HOST_CMD);

	if (wait_for_ec(EC_LPC_ADDR_HOST_CMD, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC response\n");
		return -EC_RES_ERROR;
	}

	/* Check result */
	i = inb(EC_LPC_ADDR_HOST_DATA);
	if (i) {
		fprintf(stderr, "EC returned error result code %d\n", i);
		return -i;
	}

	/* Read back args */
	for (i = 0, dout = (uint8_t *)&args; i < sizeof(args); i++, dout++)
		*dout = inb(EC_LPC_ADDR_HOST_ARGS + i);

	/*
	 * If EC didn't modify args flags, then somehow we sent a new-style
	 * command to an old EC, which means it would have read its params
	 * from the wrong place.
	 */
	if (!(args.flags & EC_HOST_ARGS_FLAG_TO_HOST)) {
		fprintf(stderr, "EC protocol mismatch\n");
		return -EC_RES_INVALID_RESPONSE;
	}

	if (args.data_size > insize) {
		fprintf(stderr, "EC returned too much data\n");
		return -EC_RES_INVALID_RESPONSE;
	}

	/* Start calculating response checksum */
	csum = command + args.flags + args.command_version + args.data_size;

	/* Read response and update checksum */
	for (i = 0, dout = (uint8_t *)indata; i < args.data_size;
	     i++, dout++) {
		*dout = inb(EC_LPC_ADDR_HOST_PARAM + i);
		csum += *dout;
	}

	/* Verify checksum */
	if (args.checksum != (uint8_t)csum) {
		fprintf(stderr, "EC response has invalid checksum\n");
		return -EC_RES_INVALID_CHECKSUM;
	}

	/* Return actual amount of data received */
	return args.data_size;
}


static int ec_readmem_lpc(int offset, int bytes, void *dest)
{
	int i = offset;
	char *s = dest;
	int cnt = 0;

	if (offset >= EC_MEMMAP_SIZE - bytes)
		return -1;

	if (bytes) {				/* fixed length */
		for (; cnt < bytes; i++, s++, cnt++)
			*s = inb(EC_LPC_ADDR_MEMMAP + i);
	} else {				/* string */
		for (; i < EC_MEMMAP_SIZE; i++, s++) {
			*s = inb(EC_LPC_ADDR_MEMMAP + i);
			cnt++;
			if (!*s)
				break;
		}
	}

	return cnt;
}

int comm_init_lpc(void)
{
	int i;
	int byte = 0xff;

	/* Request I/O privilege */
	if (iopl(3) < 0) {
		perror("Error getting I/O privilege");
		return -3;
	}

	/*
	 * Test if the I/O port has been configured for Chromium EC LPC
	 * interface.  If all the bytes are 0xff, very likely that Chromium EC
	 * is not present.
	 *
	 * TODO: (crosbug.com/p/10963) Should only need to look at the command
	 * byte, since we don't support ACPI burst mode and thus bit 4 should
	 * be 0.
	 */
	byte &= inb(EC_LPC_ADDR_HOST_CMD);
	byte &= inb(EC_LPC_ADDR_HOST_DATA);
	for (i = 0; i < EC_HOST_PARAM_SIZE && byte == 0xff; ++i)
		byte &= inb(EC_LPC_ADDR_HOST_PARAM + i);
	if (byte == 0xff) {
		fprintf(stderr, "Port 0x%x,0x%x,0x%x-0x%x are all 0xFF.\n",
			EC_LPC_ADDR_HOST_CMD, EC_LPC_ADDR_HOST_DATA,
			EC_LPC_ADDR_HOST_PARAM,
			EC_LPC_ADDR_HOST_PARAM + EC_HOST_PARAM_SIZE - 1);
		fprintf(stderr,
			"Very likely this board doesn't have a Chromium EC.\n");
		return -4;
	}

	/*
	 * Test if LPC command args are supported.
	 *
	 * The cheapest way to do this is by looking for the memory-mapped
	 * flag.  This is faster than sending a new-style 'hello' command and
	 * seeing whether the EC sets the EC_HOST_ARGS_FLAG_FROM_HOST flag
	 * in args when it responds.
	 */
	if (inb(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID) != 'E' ||
	    inb(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID + 1) != 'C' ||
	    !(inb(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_HOST_CMD_FLAGS) &
	      EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED)) {
		fprintf(stderr, "EC doesn't support command args.\n");
		return -5;
	}

	/* Okay, this works */
	ec_command = ec_command_lpc;
	ec_readmem = ec_readmem_lpc;
	return 0;
}
