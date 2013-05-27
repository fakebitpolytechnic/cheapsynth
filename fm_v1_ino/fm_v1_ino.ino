/*  Example of a sound being triggered by MIDI input.
 *
 *  Demonstrates playing notes with Mozzi in response to MIDI input,
 *  using the standard Arduino MIDI library:
 *  http://playground.arduino.cc/Main/MIDILibrary
 *
 *  Mozzi help/discussion/announcements:
 *  https://groups.google.com/forum/#!forum/mozzi-users
 *
 *  Tim Barrass 2013.
 *  This example code is in the public domain.
 *
 *  sgreen - modified to use standard Arduino midi library, added saw wave, lowpass filter
 *  Audio output from pin 9 (pwm)
 *  Midi plug pin 2 (centre) to Arduino gnd, pin 5 to RX (0)
 *  http://www.philrees.co.uk/midiplug.htm
 *
 * dgreen - nutty portamento added!
 */

#include <MIDI.h>
#include <MozziGuts.h>
#include <Oscil.h> // oscillator template
#include <Line.h> // for envelope
#include <mozzi_midi.h>
#include <ADSR.h>
#include <mozzi_fixmath.h>
#include <Portamento.h>
#include <tables/cos2048_int8.h> // table for Oscils to play
#include <tables/square_analogue2048_int8.h> // table for Oscils to play

// use #define for CONTROL_RATE, not a constant
#define CONTROL_RATE 256 // powers of 2 please

//  Mozzi example uses COS waves for carrier and modulator
//  Shit gets brutal very fast if you use a saw/square carrier
//  Shit gets subtly more brutal if you use smaller wavetables (fewer samples per cycle)
Oscil<SQUARE_ANALOGUE2048_NUM_CELLS, AUDIO_RATE> oscCarrier(SQUARE_ANALOGUE2048_DATA);
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscModulator(COS2048_DATA);
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscLFO(COS2048_DATA);

// envelope generator
ADSR <CONTROL_RATE> envelope;
Portamento <CONTROL_RATE>aPortamento;

#define LED 13 // to see if MIDI is being recieved

unsigned long vibrato = 0;
float carrierFreq = 10.f;
float modFreq = 10.f;
float modDepth = 0;
float amplitude = 0.f;
float modOffset = 1;
byte lastnote = 0;
int portSpeed = 100;

float modOffsets[] = {
  4, 3.5, 3, 2.5,
  2, 1.5, 1, 0.6666667,
  0.5, 0.4, 0.3333333, 0.2857,
  0.25, 0, 0, 0,
  0, 0, 0
}; // freq ratios corresponding to DP's preferred intervals of 7, 12, 7, 19, 24, 0, 12, -12, etc

void setup()
{
  pinMode(LED, OUTPUT);

  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(HandleNoteOn);
  MIDI.setHandleNoteOff(HandleNoteOff);
  MIDI.setHandleControlChange(HandleControlChange);
  MIDI.setHandlePitchBend(HandlePitchBend);
  MIDI.setHandleProgramChange(HandleProgramChange); 
  MIDI.setHandleContinue(HandleContinue); 

  envelope.setADLevels(255, 174);
  envelope.setTimes(10, 50, 20000, 10); // 20000 is so the note will sustain 20 seconds unless a noteOff comes
  aPortamento.setTime(50u);
  oscLFO.setFreq(10); // default frequency
  
  startMozzi(CONTROL_RATE); 
}


void HandleProgramChange (byte channel, byte number)
{
  //  Use MIDI Program Change to select an interval between carrier and modulator oscillator
  modOffset = modOffsets[number % 15];
}

void HandleNoteOn(byte channel, byte note, byte velocity)
{ 
  aPortamento.start((byte)(((int) note) - 5)); 

  lastnote = note;
  envelope.noteOn();

  digitalWrite(LED, HIGH);
}

void HandleContinue ()
{
  portSpeed += 100; 

  if (portSpeed > 500)
  {
    portSpeed = 0;
  }

  aPortamento.setTime(portSpeed);
}

void HandleNoteOff(byte channel, byte note, byte velocity)
{
  if (note == lastnote)
  {
    envelope.noteOff();
    digitalWrite(LED, LOW);
  }
}

void HandlePitchBend (byte channel, int bend)
{
  float shifted = float ((bend + 8500) / 2048.f) + 0.1f;  
  oscLFO.setFreq(shifted);
}

void HandleControlChange (byte channel, byte number, byte value)
{
  if(number == 1)
  {
    float divided = float(value / 46.f);
    modDepth = (divided * divided);
    if (modDepth > 5)
    {
      modDepth = 5;
    }
    if (modDepth < 0.2)
    {
      modDepth = 0;
    }
  }
}

void updateControl()
{
  MIDI.read();
  envelope.update();
  carrierFreq = Q16n16_to_float (aPortamento.next()); //NB presumably returns frequency as Q16n16
  modFreq = carrierFreq * modOffset; 
  oscCarrier.setFreq(carrierFreq);
  oscModulator.setFreq(modFreq);
}

int updateAudio()
{
  vibrato = (unsigned long) (oscLFO.next() * oscModulator.next()) >> 7;
  vibrato *= (unsigned long) (carrierFreq * modDepth) >> 3;
  return (int) ((oscCarrier.phMod(vibrato)) * (envelope.next())) >> 7;
}

void loop()
{
  audioHook();
} 

