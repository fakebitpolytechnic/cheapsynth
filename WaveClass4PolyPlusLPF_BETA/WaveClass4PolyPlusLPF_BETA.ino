
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
 *  Now with drums! (still has all this code in there, commented out : )

S Green: 
mod controller/ribbon is low pass filter cut-off
big button switches between tri/rectangle/noise/saw 
(noise is at constant frequency)

NB think mostly fixed, but may still be possible to crash filter with loud sounds, hit reset!
 
 */


#include <MIDI.h>
#include <MozziGuts.h>
#include <Oscil.h> // oscillator template
#include <Line.h> // for envelope
#include <Sample.h>
#include <mozzi_utils.h>
#include <mozzi_analog.h>

#include <tables/sin2048_int8.h> // sine table for oscillator
//#include <tables/saw2048_int8.h>
//#include <tables/triangle2048_int8.h>

#include <mozzi_midi.h>
#include <ADSRslow.h>
#include <fixedMath.h>
#include <LowPassFilter.h>
#include "Wave.h"
#include <WaveShaper.h>
#include <tables/waveshape_compress_512_to_488_int16.h>

#define MAX_NOTES 4
//Dave G: now seems to do 4 note polyphony without clicking (higher notes are a bit grainy)
int threshold=255/MAX_NOTES;

#define DRUM_SAMPLES 0

/*
#if DRUM_SAMPLES
#include "kick909.h"
#include "snare909.h"
#include "hihatc909.h"
#include "hihato909.h"
#endif
*/

// use #define for CONTROL_RATE, not a constant
#define CONTROL_RATE 128 // powers of 2 please

#define ENABLE_MIDI 1
#define DEBUG 0
#define BPM 120
#define STEPS_PER_BEAT 4

unsigned long stepCounter = 0;
unsigned long ticksPerStep = (60*CONTROL_RATE) / (BPM*STEPS_PER_BEAT);

// audio sinewave oscillator
//Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> osc(SIN2048_DATA);
//Oscil <SAW2048_NUM_CELLS, AUDIO_RATE> osc(SAW2048_DATA);
//Oscil <TRIANGLE2048_NUM_CELLS, AUDIO_RATE> osc(TRIANGLE2048_DATA);
//Wave myosc;
//Wave lfo;
Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> lfo(SIN2048_DATA);
WaveShaper <int> aCompress(WAVESHAPE_COMPRESS_512_TO_488_DATA); // to compress instead of dividing by 2 after adding signals

struct Channel {
  Wave osc;
  //Oscil <TRIANGLE2048_NUM_CELLS, AUDIO_RATE> osc;
  ADSR <CONTROL_RATE> env;  
  byte note;
  byte gain;
};
Channel chan[MAX_NOTES];
byte currentChan = 0;

// envelope generator
//ADSR <CONTROL_RATE> envelope;
LowPassFilter lpf;
int crushCtrl = 0;
int gain = 32;
float octave = 1.0f;

boolean enableDrums = false;

int step = 0;
int waveType = 1;

int button0_old = 0;
int button1_old = 0;

#if DRUM_SAMPLES
// drums
Sample <kick909_NUM_CELLS, AUDIO_RATE> kickSamp(kick909_DATA);
Sample <snare909_NUM_CELLS, AUDIO_RATE> snareSamp(snare909_DATA);
Sample <hihatc909_NUM_CELLS, AUDIO_RATE> hihatcSamp(hihatc909_DATA);
Sample <hihato_NUM_CELLS, AUDIO_RATE> hihatoSamp(hihato_DATA);
#endif

byte pattern[4][16] = {
  //0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0 hhc
    0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0,  // 1 hho
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,  // 2 s
    1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1   // 3 k
};

// pin defines
#define LED 13 // to see if MIDI is being recieved
#define BUTTON0_PIN 2
#define BUTTON1_PIN 3

// forward declarations
void HandleNoteOn(byte channel, byte note, byte velocity);
void HandleNoteOff(byte channel, byte note, byte velocity);
void HandleControlChange (byte channel, byte number, byte value);
void HandlePitchBend (byte channel, int bend);

void setup() {
  // init IO pins
  pinMode(LED, OUTPUT);

#if 0  
  pinMode(BUTTON0_PIN, INPUT);
  digitalWrite(BUTTON0_PIN, HIGH);  // turn on pull-up resistor

  pinMode(BUTTON1_PIN, INPUT);
  digitalWrite(BUTTON1_PIN, HIGH);  // turn on pull-up resistor
#endif

#if DEBUG
  Serial.begin(57600);  // for debugging
#endif

#if ENABLE_MIDI
  // Initiate MIDI communications, listen to all channels
  MIDI.begin(MIDI_CHANNEL_OMNI);

  // Connect the HandleNoteOn function to the library, so it is called upon reception of a NoteOn.
  MIDI.setHandleNoteOn(HandleNoteOn);  // Put only the name of the function
  MIDI.setHandleNoteOff(HandleNoteOff);  // Put only the name of the function
  MIDI.setHandleControlChange(HandleControlChange);
  MIDI.setHandlePitchBend(HandlePitchBend);
  MIDI.setHandleContinue(HandleContinue); 
  MIDI.setHandleStop(HandleStop);   
#endif

#if 0
  //osc.setFreq(440u); // default frequency
  //myosc.setFreq(440.0f);
  myosc.setFreq(440.0f*4.0f);
  myosc.setPulseWidth(32);

  envelope.setADLevels(255, 200);
  envelope.setTimes(50, 200, 65535, 200);
#endif
  
  for(int i=0; i<MAX_NOTES; i++)
  {
    chan[i].osc.setType(WAVE_TRI);
    //chan[i].osc.setTable(TRIANGLE2048_DATA);
    chan[i].osc.setPulseWidth(16);
    chan[i].env.setADLevels(128, 100);
    chan[i].env.setTimes(100, 200, 65535, 200);
  }
 
  //lfo.setType(WAVE_TRI);
  lfo.setFreq(10.0f);

  lpf.setResonance(128);
  lpf.setCutoffFreq(255);

#if DRUM_SAMPLES
  kickSamp.setFreq((float) kick909_SAMPLERATE / (float) kick909_NUM_CELLS);
  snareSamp.setFreq((float) snare909_SAMPLERATE / (float) snare909_NUM_CELLS);
  hihatcSamp.setFreq((float) hihatc909_SAMPLERATE / (float) hihatc909_NUM_CELLS);
  hihatoSamp.setFreq((float) hihato_SAMPLERATE / (float) hihato_NUM_CELLS);
  //snareSamp.start();
#endif

  //setupFastAnalogRead(); // optional  
  adcEnableInterrupt();  // for analog reads

  startMozzi(CONTROL_RATE); 
}

void HandleNoteOn(byte channel, byte note, byte velocity) {
  //osc.setFreq(mtof(note)); // simple but less accurate frequency
  //osc.setFreq_Q16n16(Q16n16_mtof(Q8n0_to_Q16n16(note))); // accurate frequency
  //myosc.setFreq(mtof(note)); // accurate frequency
  //envelope.noteOn();
 
   // start note on next channel - NB no "note priority" at this stage
  chan[currentChan].note = note;
  chan[currentChan].osc.setFreq( mtof(note) ); // accurate frequency
  chan[currentChan].env.noteOn();
  currentChan = (currentChan + 1) % MAX_NOTES;  // just wraps around for now
  
  digitalWrite(LED,HIGH);
}

void HandleNoteOff(byte channel, byte note, byte velocity) { 
  //envelope.noteOff();
  
  // find which channel was playing this note
  for(int i=0; i<MAX_NOTES; i++) {
    if (chan[i].note == note) {
      // kill it
      chan[i].env.noteOff();
    } 
  }
  
  digitalWrite(LED,LOW);
}

void HandleControlChange (byte channel, byte number, byte value)
{
  // http://www.indiana.edu/~emusic/cntrlnumb.html
  switch(number) {
  case 1:      // modulation wheel
    //gain = value*2;
    lpf.setCutoffFreq( int(value*1.5f) );  // control messages are in [0, 127] range
    break;
/*    
  case 105:
    lpf.setCutoffFreq(value*2);  // control messages are in [0, 127] range
    break;
*/
  case 106:
    //lpf.setResonance(value*2);
    crushCtrl = value;
    break;
  }
}

// pitchbend = control strip + button on Keytar
void HandlePitchBend (byte channel, int bend)
{
  //bend value from +/-8192
  //lfo.setFreq((unsigned int) (bend+8192)>>7);
  for(int i=0; i<MAX_NOTES; i++) {
    chan[i].osc.setPulseWidth((bend+8192) >> 8); 
  }
  
}

void HandleStop () { //this is the Back button on the Xbox keyboard
//  octave *= 2.0f;
//  if (octave > 16.0f) octave = 1.0f;
//Dave G: no longer used : )
}

void HandleContinue () { // the big round button on all keytars

  // change wave
  waveType = (waveType + 1) & 3;

  for(int i=0; i<MAX_NOTES; i++)
  {
#if 0
    switch(waveType) {
    case 0:
      chan[i].osc.setTable(TRIANGLE2048_DATA);
      break;
    case 1:
      chan[i].osc.setTable(SAW2048_DATA);
      break;
    case 2:
      chan[i].osc.setTable(SIN2048_DATA);
      break;
    }
#else
    for(int i=0; i<MAX_NOTES; i++)
    {
      chan[i].osc.setType((WaveType) waveType);
    }
#endif
  }
}

void updateControl(){
#if ENABLE_MIDI
  MIDI.read();
#endif

  //envelope.update();
  
  // update playing envelopes
  for(int i=0; i<MAX_NOTES; i++) {
    chan[i].env.update(); 
    chan[i].gain=chan[i].env.next();
    if (chan[i].gain>threshold) {chan[i].gain=threshold;}
    // threshold=255/MAX_NOTES, genuinely not sure if this is best approach but stops crashes :)
  }

#if 0
  // push buttons
  int button0 = !digitalRead(BUTTON0_PIN);  // active low
  int button1 = !digitalRead(BUTTON1_PIN);
  
  /*
  if (button0) {
    // trigger
    //envelope.noteOn();
    //lpf.reset();
    digitalWrite(LED, HIGH);    
  } else {
    digitalWrite(LED, LOW);    
  }
  */
  
  if (button0 && !button0_old) {
    lfo.setType((WaveType) ((((int) lfo.getType()) + 1) % 3));    
  }
  button0_old = button0;

  if (button1 & !button1_old) {
    waveType = (waveType + 1) & 3;
    //myosc.setType((WaveType) waveType);
    for(int i=0; i<MAX_NOTES; i++)
    {
      chan[i].osc.setType((WaveType) waveType);
    }
  }
  button1_old = button1;
#endif

#if DRUM_SAMPLES
  if (enableDrums) { 
    // drums  
    stepCounter++;
    if (stepCounter > ticksPerStep) {
      step = (step+1) & 0xf;
      stepCounter = 0;
      if (pattern[0][step]) hihatcSamp.start();
      if (pattern[1][step]) hihatoSamp.start();
      if (pattern[2][step]) snareSamp.start();
      if (pattern[3][step]) kickSamp.start();
    }
  }
#endif

#if 0  
  // knobs
  int knob0 = adcGetResult(0);  // [0, 1023]
  int knob1 = adcGetResult(1);
  //lpf.setCutoffFreq(knob0>>2);
  //lpf.setResonance(knob1>>2);

  //crushCtrl = knob1>>7;  
  lfo.setFreq((float) knob0);
  //myosc.setFreq((float) knob1);
  //myosc.setFreq((unsigned int) knob0*20);
  //myosc.setPulseWidth((unsigned char) (knob1>>2));

  // start the next read cycle in the background
  adcReadAllChannels();
#endif

#if DEBUG  
  Serial.println(knob0);
#endif
}

// strip the low bits off!
int bitCrush(int x, int a)
{
  return (x>>a)<<a;
}

int updateAudio()
{
  //int x = (int) (envelope.next() * osc.next())>>8;
  //int x = (int) (envelope.next() * myosc.next())>>8;
  //int x = (int) (envelope.next() * (lfo.next() * myosc.next())>>8)>>8;
  //int x = (int) lfo.next();
  //int x = (int) (lfo.next() * myosc.next())>>8;  
  //int x = (int) (envelope.next() * lpf.next(myosc.next()))>>8;
  //int x = (envelope.next() * lpf.next(osc.next()))>>8; 
  //x = bitCrush(x, crushCtrl>>4);
  //x = bitCrush(x, crushCtrl);
  //x = (x * crushCtrl)>>4;  // simple gain
  //x = x>>2;
  //x = lpf.next(x);

  // sum up channels
  int x = 0;
  for(int i=0; i<MAX_NOTES; i++) {
    x += (int) (chan[i].gain * chan[i].osc.next())>>8;
  }
//Dave G: helps crash less if goes through some sort of waveshaper here?
  x = aCompress.next(256u + x);

  //x = (x*gain)>>8;
  //x = (x * lfo.next())>>8;  
  x = lpf.next(x);

#if DRUM_SAMPLES
  if (enableDrums) { 
    // drums please!
    int drums = kickSamp.next() + snareSamp.next() + hihatcSamp.next() + hihatoSamp.next();
    //drums = (drums>>4)<<4;  
    x += drums*2;
  }
#endif
  
  return x;
}


void loop() {
  audioHook(); // required here
} 



