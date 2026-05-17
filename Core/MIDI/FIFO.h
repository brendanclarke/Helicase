/*
 * FIFO.h — ported from original LXR firmware by Julian Schmidt.
 * globals.h dependency replaced with stdint.h.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */


#ifndef FIFO_H_
#define FIFO_H_

#include <stdint.h>
#include <string.h>

#define BUFFER_SIZE   64
#define BUFFER_MASK   (BUFFER_SIZE - 1)

typedef struct FifoStruct {
	volatile uint8_t data[BUFFER_SIZE];
	volatile uint8_t read;
	volatile uint8_t write;
} Fifo;

void    fifo_init(Fifo *fifo);
uint8_t fifo_bufferIn(Fifo *fifo, uint8_t byte);
uint8_t fifo_bufferOut(Fifo *fifo, uint8_t *pByte);
void    fifo_clear(Fifo *fifo);

#endif /* FIFO_H_ */
