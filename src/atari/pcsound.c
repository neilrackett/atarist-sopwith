#include <stdint.h>

#include "pcsound.h"

bool snd_tinnyfilter = false;

/* YM2149 PSG register access (supervisor mode is already active from main.c) */
#define PSG_SELECT (*(volatile uint8_t *)0xFF8800)
#define PSG_WRITE  (*(volatile uint8_t *)0xFF8802)

static inline void psg_write(uint8_t reg, uint8_t val)
{
	PSG_SELECT = reg;
	PSG_WRITE  = val;
}

static inline uint8_t psg_read(uint8_t reg)
{
	PSG_SELECT = reg;
	return PSG_SELECT;
}

void Speaker_Init(void)
{
	psg_write(8, 0);                         /* channel A amplitude = 0 */
	psg_write(7, psg_read(7) | 0x01);        /* mixer: disable tone A (bit 0) */
}

void Speaker_Off(void)
{
	psg_write(8, 0);                         /* channel A amplitude = 0 */
}

void Speaker_Output(unsigned short count)
{
	uint16_t ym_div;

	if (count == 0)
	{
		Speaker_Off();
		return;
	}

	/* Convert PC ISA timer divisor to YM2149 12-bit tone divisor.
	   PC:  freq_hz = 1193280 / count
	   YM:  freq_hz = 2000000 / (16 * div)  =>  div = count * 10 / 96
	   Error < 0.5% across the audible range. */
	ym_div = (uint16_t)(((uint32_t)count * 10U) / 96U);
	if (ym_div > 0x0FFF)
		ym_div = 0x0FFF;

	/* Below ~30 Hz (ym_div > 4167) is inaudible on the ST speaker — silence.
	   This covers the 0xF000 engine-idle tone (~19 Hz) from the original code. */
	if (ym_div > 4167)
	{
		Speaker_Off();
		return;
	}

	psg_write(0, (uint8_t)(ym_div & 0xFF));         /* tone A fine   (bits 7:0) */
	psg_write(1, (uint8_t)((ym_div >> 8) & 0x0F));  /* tone A coarse (bits 3:0) */
	psg_write(7, psg_read(7) & ~0x01);              /* mixer: enable tone A */
	psg_write(8, 6);                                /* amplitude ~37% — lower level reduces harshness */
}
