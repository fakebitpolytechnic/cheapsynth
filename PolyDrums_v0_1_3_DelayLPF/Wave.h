/*
 * Wave.h
 *
 * Simon Green 2013.
 *
 * Based on Mozzi "Oscil.h"
 */

#ifndef WAVE_H_
#define WAVE_H_

#include "Arduino.h"
#include "mozzi_fixmath.h"
#include <util/atomic.h>

// fractional bits for oscillator index precision
#define OSCIL_F_BITS 16
#define OSCIL_F_BITS_AS_MULTIPLIER 65536
#define NUM_TABLE_CELLS 256
#define ADJUST_FOR_NUM_TABLE_CELLS 8
#define UPDATE_RATE AUDIO_RATE

/** Generate waveform
*/

enum WaveType { WAVE_SAW=0, WAVE_TRI, WAVE_RECT, WAVE_NOISE };

class Wave
{
public:
	/** Constructor. "Wave mywave;" makes a Wave oscillator
	*/
	Wave () {
          phase_fractional = 0;
          phase_increment_fractional = 0;
          wavetype = WAVE_SAW;
          noise = 0xACE1;
          pulse_width = 128;
	}

	/** Increments one step along the phase.
	@return the next value.
	 */
	inline
	char next()
	{
  		incrementPhase();

                char w;
  		//ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
                        int n = (phase_fractional >> OSCIL_F_BITS) & (NUM_TABLE_CELLS-1);
                        switch(wavetype) {
                          case WAVE_SAW:
                            w = n - 128;
                            break;
                          case WAVE_TRI:
                            if (n & 0x80)  // >= 128
                              w = ((n^0xFF)<<1)-128;
                            else 
                              w = (n<<1)-128;
                            break;
                          case WAVE_RECT:
                            if (n > pulse_width)
                              w = 127;
                            else
                              w = -127;
                            break;
                          case WAVE_NOISE:
                            noise = (noise >> 1) ^ (-(noise & 1) & 0xB400u);
                            w = noise>>8;
                            break;
                        }
		}
                return w;
	}

	/** Set the wave type
	 */
	inline
	void setType(WaveType x)
	{
		wavetype = x;
	}

        WaveType getType() { return wavetype; }

	/** Set the pulse width for rectangle wave
	 */
	inline
	void setPulseWidth(unsigned char x)
	{
		pulse_width = x;
	}

	/** Set the oscillator frequency with an unsigned int. This is faster than using a
	float, so it's useful when processor time is tight, but it can be tricky with
	low and high frequencies, depending on the size of the wavetable being used. If
	you're not getting the results you expect, try explicitly using a float, or try
	setFreq_Q24n8() or or setFreq_Q16n16().
	@param frequency to play the wave table.
	*/
	inline
	void setFreq (unsigned int frequency) {
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			phase_increment_fractional = ((((unsigned long)NUM_TABLE_CELLS<<ADJUST_FOR_NUM_TABLE_CELLS)*frequency)/UPDATE_RATE) << (OSCIL_F_BITS - ADJUST_FOR_NUM_TABLE_CELLS);
		}
	}
	
	
	/** Set the oscillator frequency with a float. Using a float is the most reliable
	way to set frequencies, -Might- be slower than using an int but you need either
	this, setFreq_Q24n8() or setFreq_Q16n16() for fractional frequencies.
	@param frequency to play the wave table.
	 */
	inline
	void setFreq(float frequency)
	{ // 1 us - using float doesn't seem to incur measurable overhead with the oscilloscope
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			phase_increment_fractional = (unsigned long)((((float)NUM_TABLE_CELLS * frequency)/UPDATE_RATE) * OSCIL_F_BITS_AS_MULTIPLIER);
		}
	}
	
	
	/** Set the frequency using Q24n8 fixed-point number format.
	This might be faster than the float version for setting low frequencies such as
	1.5 Hz, or other values which may not work well with your table size. A Q24n8
	representation of 1.5 is 384 (ie. 1.5 * 256). Can't be used with UPDATE_RATE
	less than 64 Hz.
	@param frequency in Q24n8 fixed-point number format.
	*/
	inline
	void setFreq_Q24n8(Q24n8 frequency)
	{
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			//phase_increment_fractional = (frequency* (NUM_TABLE_CELLS>>3)/(UPDATE_RATE>>6)) << (F_BITS-(8-3+6));
			phase_increment_fractional = (((((unsigned long)NUM_TABLE_CELLS<<ADJUST_FOR_NUM_TABLE_CELLS)>>3)*frequency)/(UPDATE_RATE>>6)) 
				<< (OSCIL_F_BITS - ADJUST_FOR_NUM_TABLE_CELLS - (8-3+6));
		}
	}


	/** Set the frequency using Q16n16 fixed-point number format. This is useful in
	combination with Q16n16_mtof(), a fast alternative to mtof(), using Q16n16
	fixed-point format instead of floats.  Note: this should work OK with tables 2048 cells or smaller and
	frequencies up to 4096 Hz.  Can't be used with UPDATE_RATE less than 64 Hz.
	@param frequency in Q16n16 fixed-point number format.
	*/
	inline
	void setFreq_Q16n16(Q16n16 frequency)
	{
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			//phase_increment_fractional = ((frequency * (NUM_TABLE_CELLS>>7))/(UPDATE_RATE>>6)) << (F_BITS-16+1);
			phase_increment_fractional = (((((unsigned long)NUM_TABLE_CELLS<<ADJUST_FOR_NUM_TABLE_CELLS)>>7)*frequency)/(UPDATE_RATE>>6)) 
				<< (OSCIL_F_BITS - ADJUST_FOR_NUM_TABLE_CELLS - 16 + 1);

		}
	}

private:
	/** Increments the phase of the oscillator without returning a sample.
	 */
	inline
	void incrementPhase()
	{
		//phase_fractional += (phase_increment_fractional | 1); // odd phase incr, attempt to reduce frequency spurs in output
		phase_fractional += phase_increment_fractional;
	}

	unsigned long phase_fractional;
	volatile unsigned long phase_increment_fractional;

        unsigned char pulse_width;
        uint16_t noise;
        
        WaveType wavetype;
};

#endif /* WAVE_H_ */
