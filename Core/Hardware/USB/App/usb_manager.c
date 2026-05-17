/*
 * usb_manager.c
 *
 * ------------------------------------------------------------------------------
 *
 *  This file is part of the "Cortex Hardware Audio Operating System (CHAOS)".
 *
 *	Copyright 2012 Julian Schmidt / Sonic Potions
 *	http://www.sonic-potions.com
 *
 *  ------------------------------------------------------------------------------
 *
 *	CHAOS is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	CHAOS is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with CHAOS.  If not, see <http://www.gnu.org/licenses/>.
 *
 *	------------------------------------------------------------------------------
 *
 *
 *  Created on: 26.10.2012
 *      Author: Julian Schmidt
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



#include "usb_manager.h"
#include "usb_dcd.h"
#include <string.h>

enum {
	USB_NOT_DETECTED,
	USB_DETECTED,
};

USB_OTG_CORE_HANDLE           USB_OTG_dev;

static uint32_t usb_irqSave(void)
{
	uint32_t primask;
	__asm volatile ("mrs %0, primask\ncpsid i" : "=r" (primask) :: "memory");
	return primask;
}

static void usb_irqRestore(uint32_t primask)
{
	__asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}
//-------------------------------------------------------------------------------
void usb_init()
{
	/*
	//init usb detect pin
	  GPIO_InitTypeDef  GPIO_InitStructure;
	  // GPIOD Periph clock enable
	  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	  // Configure PD12, PD13, PD14 and PD15 in output push-pull mode
	  GPIO_InitStructure.GPIO_Pin = USB_DETECT_PIN;
	  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_25MHz;
	  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;

	  // standard output pin
	  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	  GPIO_Init(USB_DETECT_PORT, &GPIO_InitStructure);
	  */

	  usb_start();
}
//-------------------------------------------------------------------------------
void usb_stop()
{
	//stop usb port
	USBD_DeInit(&USB_OTG_dev);
}
//-------------------------------------------------------------------------------
void usb_tick()
{
	usb_flushMidi();
}
//-------------------------------------------------------------------------------
void usb_start()
{
	//zero init the usb receive buffer
	memset(usb_MidiMessages,0, sizeof(MidiMsg)*USB_MIDI_INPUT_BUFFER_SIZE);

	//start usb port
	 USBD_Init(&USB_OTG_dev,
	#ifdef USE_USB_OTG_HS
	            USB_OTG_HS_CORE_ID,
	#else
	            USB_OTG_FS_CORE_ID,
	#endif
	            &USR_desc,
	            &MIDI_cb,
	            &USR_cb);

	/* Explicitly assert soft connect (clear DCTL.SDIS).
	**
	** The library's normal path clears SDIS only via the DevConnected ISR
	** callback, which is triggered by VBUS detection. Since we set VBDEN=0
	** (VBUS detection disabled — the ADUM3160 handles isolation), the ISR
	** callback never fires and SDIS is never cleared. DCD_DevConnect() forces
	** SDIS=0 directly, asserting the D+ pull-up so the host sees the device
	** regardless of VBUS sensing state. */
	DCD_DevConnect(&USB_OTG_dev);
}
//-------------------------------------------------------------------------------
/*
 * writes a byte to the Usb Tx buffer
 * Will be send out by the usb_flushMidi() call
 */
void usb_sendByte(uint8_t byte)
{
	*usb_MidiInWrPtr++ = byte;
	if(usb_MidiInWrPtr >= (usb_MidiInBuff+(TOTAL_MIDI_BUF_SIZE * NUM_SUB_BUFFERS)))
	{
		usb_MidiInWrPtr = usb_MidiInBuff;
	}
}
//------------------------------------------------------------------------------
/*
 * flush FIFO and send out next block of messages stored in the usb_MidiInBuff
 * called periodically by usb_tick()
 * TODO:  maybe there is a better way to call this automatically by the USB driver when it is ready to TX some more data?
 */
void usb_flushMidi()
{
	uint8_t *rdPtr;
	uint8_t len;
	uint8_t shouldTx;
	uint32_t primask;

	primask = usb_irqSave();
	len = (uint8_t)(usb_MidiInWrPtr - usb_MidiInRdPtr);
	shouldTx = (uint8_t)((len != 0) &&
			(USB_OTG_dev.dev.device_status != USB_OTG_SUSPENDED));
	rdPtr = usb_MidiInRdPtr;

	if (shouldTx)
	{
		usb_MidiInRdPtr += TOTAL_MIDI_BUF_SIZE;
		if(usb_MidiInRdPtr >= (usb_MidiInBuff+(TOTAL_MIDI_BUF_SIZE * NUM_SUB_BUFFERS)))
		{
			usb_MidiInRdPtr = usb_MidiInBuff;
		}
		usb_MidiInWrPtr=usb_MidiInRdPtr;
	}
	usb_irqRestore(primask);

	if (shouldTx) {
		DCD_EP_Flush (&USB_OTG_dev,MIDI_IN_EP);
		DCD_EP_Tx (&USB_OTG_dev, MIDI_IN_EP, rdPtr, len);
	}
}
//------------------------------------------------------------------------------
void usb_sendMidi(MidiMsg msg)
{
	uint32_t primask = usb_irqSave();

	//MIDI byte[0] = 4 bits cable number + 4 bits event type code
	usb_sendByte(msg.status>>4);

	//then the 3 midi message bytes
	//USB MIDI msg has to be ALWAYS 4 bytes long
	//even if it is just a single clock byte
	usb_sendByte(msg.status);
	usb_sendByte(msg.data1);
	usb_sendByte(msg.data2);
	usb_irqRestore(primask);
}
//-------------------------------------------------------------------------------
uint8_t usb_getMidi(MidiMsg* msg)
{
	if(usb_MidiMessagesRead != usb_MidiMessagesWrite)
	{
		//we have unprocessed messages in the queue
		*msg = usb_MidiMessages[usb_MidiMessagesRead];

		usb_MidiMessagesRead++;
		usb_MidiMessagesRead &= USB_MIDI_INPUT_BUFFER_MASK;

		return 1; //return OK
	}
return 0; //no messages
}
