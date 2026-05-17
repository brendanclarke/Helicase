/*
 * FIFO.c — ported from original LXR firmware by Julian Schmidt.
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


#include "FIFO.h"

void fifo_init(Fifo *fifo)
{
	memset((void *)fifo->data, 0, BUFFER_SIZE);
	fifo->read = fifo->write = 0;
}

uint8_t fifo_bufferIn(Fifo *fifo, uint8_t byte)
{
	uint8_t next = (fifo->write + 1) & BUFFER_MASK;
	if (fifo->read == next) return 0;
	fifo->data[fifo->write] = byte;
	fifo->write = next;
	return 1;
}

uint8_t fifo_bufferOut(Fifo *fifo, uint8_t *pByte)
{
	if (fifo->read == fifo->write) return 0;
	*pByte = fifo->data[fifo->read];
	fifo->read = (fifo->read + 1) & BUFFER_MASK;
	return 1;
}

void fifo_clear(Fifo *fifo) { fifo->read = fifo->write; }
