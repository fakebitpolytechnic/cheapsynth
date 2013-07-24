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
 *  Now with drums!
 */


#include <MIDI.h>
#include <MozziGuts.h>
#include <Oscil.h> // oscillator template
#include <Line.h> // for envelope
#include <Sample.h>

//#include <tables/sin2048_int8.h> // sine table for oscillator
#include <tables/saw2048_int8.h>
#include "kick909.h"
#include "snare909.h"
#include "hihatc909.h"
#include "hihato909.h"

#include <mozzi_midi.h>
#include <ADSR.h>
#include <fixedMath.h>
#include <LowPassFilter.h>

// use #define for CONTROL_RATE, not a constant
#define CONTROL_RATE 128 // powers of 2 please

unsigned long ctrlCounter = 0;

// audio sinewave oscillator
//Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> osc(SIN2048_DATA);
Oscil <SAW2048_NUM_CELLS, AUDIO_RATE> osc(SAW2048_DATA);

// drums
Sample <kick909_NUM_CELLS, AUDIO_RATE> kickSamp(kick909_DATA);
Sample <snare909_NUM_CELLS, AUDIO_RATE> snareSamp(snare909_DATA);
Sample <hihatc909_NUM_CELLS, AUDIO_RATE> hihatcSamp(hihatc909_DATA);
Sample <hihato_NUM_CELLS, AUDIO_RATE> hihatoSamp(hihato_DATA);

byte pattern[4][16] = {
  //0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0 hhc
    0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1,  // 1 hho
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0,  // 2 snare
    1, 0, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1   // 3 kick
};

// envelope generator
ADSR <CONTROL_RATE> envelope;
LowPassFilter lpf;
int crushCtrl = 0;

#define LED 13 // to see if MIDI is being recieved

void HandleNoteOn(byte channel, byte note, byte velocity) { 
  //osc.setFreq(mtof(note)); // simple but less accurate frequency
  osc.setFreq_Q16n16(Q16n16_mtof(Q8n0_to_Q16n16(note))); // accurate frequency
  envelope.noteOn();
  digitalWrite(LED,HIGH);
}

void HandleNoteOff(byte channel, byte note, byte velocity) { 
  envelope.noteOff();
  digitalWrite(LED,LOW);
}

void HandleControlChange (byte channel, byte number, byte value)
{
  // http://www.indiana.edu/~emusic/cntrlnumb.html
  switch(number) {
  //case 1:      // modulation wheel
  case 105:
    lpf.setCutoffFreq(value*2);  // control messages are in [0, 127] range
    break;
  case 106:
    //lpf.setResonance(value*2);
    crushCtrl = value;
    break;
  }
}

void setup() {
  pinMode(LED, OUTPUT);

  // Initiate MIDI communications, listen to all channels
  MIDI.begin(MIDI_CHANNEL_OMNI);

  // Connect the HandleNoteOn function to the library, so it is called upon reception of a NoteOn.
  MIDI.setHandleNoteOn(HandleNoteOn);  // Put only the name of the function
  MIDI.setHandleNoteOff(HandleNoteOff);  // Put only the name of the function
  MIDI.setHandleControlChange(HandleControlChange);

  envelope.setADLevels(127,64);
  envelope.setTimes(50,200,10000,200); // 10000 is so the note will sustain 10 seconds unless a noteOff comes

  lpf.setResonance(200);
  lpf.setCutoffFreq(255);

  osc.setFreq(440); // default frequency

  kickSamp.setFreq((float) kick909_SAMPLERATE / (float) kick909_NUM_CELLS);
  snareSamp.setFreq((float) snare909_SAMPLERATE / (float) snare909_NUM_CELLS);
  hihatcSamp.setFreq((float) hihatc909_SAMPLERATE / (float) hihatc909_NUM_CELLS);
  hihatoSamp.setFreq((float) hihato_SAMPLERATE / (float) hihato_NUM_CELLS);

  //snareSamp.start();

  startMozzi(CONTROL_RATE); 
}

int beat = 0;
int oldBeat = 0;

void updateControl(){
  MIDI.read();
  envelope.update();

  // drums  
  ctrlCounter++;
  // this is called at 128Hz -> 120bmp!?
  beat = ((ctrlCounter*2*4)>>7);
  if (beat != oldBeat) {
    if (pattern[0][beat&0xf]) hihatcSamp.start();
    if (pattern[1][beat&0xf]) hihatoSamp.start();
    if (pattern[2][beat&0xf]) snareSamp.start();
    if (pattern[3][beat&0xf]) kickSamp.start();
  }
  oldBeat = beat;
}

// strip the low bits off!
int bitCrush(int x, int a)
{
  return (x>>a)<<a;
}

int updateAudio(){
  //return (int) (envelope.next() * osc.next())>>8;
// return (int) (envelope.next() * lpf.next(osc.next()))>>8;
  int x = (int) (envelope.next() * osc.next())>>8;
  
 //int x = (envelope.next() * lpf.next(osc.next()))>>8; 
  //x = bitCrush(x, crushCtrl>>4);
  //x = (x * crushCtrl)>>4;  // simple gain
  //x = bitCrush(x, 6);
  x = lpf.next(x);

  // drums please!
  int drums = kickSamp.next() + snareSamp.next() + hihatcSamp.next() + hihatoSamp.next();
  x += drums<<1;
  return x;
}


void loop() {
  audioHook(); // required here
} 

