
/*  Example of a sound being triggered by MIDI input.
 * NOW COMPATIBLE WITH MOZZI v1.0.2, MIDI library 4.2
 * LESS COMPATIBLE WITH ARDUINO IDE 1.0.6 onwards - http://sensorium.github.io/Mozzi/blog/2015/04/08/use-arduino-1-dot-0-5/
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
 *  sgreen - modified to use standard Arduino midi library, added saw wave
 *  Audio output from pin 9 (pwm)
 *  Midi plug pin 2 (centre) to Arduino gnd, pin 5 to RX (0)
 *  http://www.philrees.co.uk/midiplug.htm

D Green July 2015:
Big transparent button cycles through different backing sequences (including an empty one at the end)

Small button to its left toggles between normal envelope and fake "delay" effect on all sounds

Small button to its right switches main drums on and off

Different waveforms selected via the right-hand corner up and down keys between octave up
and octave down - cycles between (I think): sawtooth, squarewave (variable duty cycle controlled by touchstrip), 
squarewave with variable duty cycle autolocked to tempo, triangle wave (sounds like electric piano)
 
 */
#define MAX_NOTES 3
//Dave G: now seems to do 2+ note polyphony without clicking (higher notes are a bit grainy)
int threshold=255/(MAX_NOTES);

#include <MIDI.h>
//#include <SPI.h>
#include <MozziGuts.h>
#include <Oscil.h> // oscillator template
#include <Line.h> // for envelope
#include <Sample.h>
#include <mozzi_utils.h>
#include <mozzi_analog.h>

//#include <tables/square_analogue512_int8.h> 
#include <tables/saw512_int8.h>
#include <tables/cos512_int8.h>

#include <mozzi_midi.h>
#include <ADSR.h>
//#include <fixedMath.h>
#include <LowPassFilter.h>
//#include "Wave.h"
#include <WaveShaper.h>
#include <tables/waveshape_compress_512_to_488_int16.h>


#define DRUM_SAMPLES 1

#if DRUM_SAMPLES
#include "kick909.h"
#include "snare909.h"
#include "hihatc909.h"
#include "hihato909.h"
#endif

// use #define for CONTROL_RATE, not a constant
#define CONTROL_RATE 128 // powers of 2 please

#define ENABLE_MIDI 1
#define DEBUG 0
#define BPM 120
#define STEPS_PER_BEAT 4

unsigned long beatCounter = 0;
unsigned long ticksPerStep = (60*CONTROL_RATE) / (BPM*STEPS_PER_BEAT);

// audio sinewave oscillator
//Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> oscSin(SIN2048_DATA);
//Oscil <SAW512_NUM_CELLS, AUDIO_RATE> sawwave(SAW512_DATA);
//Oscil <TRIANGLE2048_NUM_CELLS, AUDIO_RATE> oscTri(TRIANGLE2048_DATA);
//Wave myosc;
WaveShaper <int> aCompress(WAVESHAPE_COMPRESS_512_TO_488_DATA); // to compress instead of dividing by 2 after adding signals

struct Channel {
  //Wave osc;
  Oscil <COS512_NUM_CELLS, AUDIO_RATE> osc;
  ADSR <CONTROL_RATE,CONTROL_RATE> env;  
//  ADSR <CONTROL_RATE> fxenv;  
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

boolean enableDrums = true;

int freeChan=0;
int beat = 0;
int waveType = 3;
int bitmask = 0b0000000010000000;

byte delayOn=0;
byte snareOn=1;
byte hatsOn=1;
byte bassOn=1;

int button0_old = 0;
int button1_old = 0;

#if DRUM_SAMPLES
// drums
Sample <kick909_NUM_CELLS, AUDIO_RATE> kickSamp(kick909_DATA);
Sample <snare909_NUM_CELLS, AUDIO_RATE> snareSamp(snare909_DATA);
Sample <hihatc909_NUM_CELLS, AUDIO_RATE> hihatcSamp(hihatc909_DATA);
Sample <hihato_NUM_CELLS, AUDIO_RATE> hihatoSamp(hihato_DATA);
#endif

#define BASSPLUS4 7 //basically how many basslines there are plus 4 for drum parts
byte pattern[BASSPLUS4][32] = {
  //0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0 hhc
    0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1,0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1,  // 1 hho
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,  // 2 snare
    1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 0,   // 3 kick
    12,0,24,0,12,0,24,0, 12, 0,24,0,12,0,24,0, 12,0,24,0,12,0,24,0, 12, 0,24,0,12,0,24,0,  // 5 bass
    12,0,12,0,14,0,12,0, 15,16,12,0,17,0,16,0, 12,0,12,0,14,0,12,0, 15,16,12,0,17,0,16,0,   // 6 bass
    24,0,12,0,24,0,12,0, 34, 0,22,0,29,0,17,0, 24,0,12,0,24,0,12,0, 34, 0,22,0,29,0,17,0,  // 7 bass
};
//bittersweet   0,12,15,12,13,10,13,0, 18,13,18,0, 17,13,17,0,

// pin defines
#define LED 13 // to see if MIDI is being recieved
#define BUTTON0_PIN 2
#define BUTTON1_PIN 3

// forward declarations
void HandleNoteOn(byte channel, byte note, byte velocity);
void HandleNoteOff(byte channel, byte note, byte velocity);
void HandleControlChange (byte channel, byte number, byte value);
void HandlePitchBend (byte channel, int bend);

//yes surely this should go in setup but then not scoped in other functions..??
  MIDI_CREATE_DEFAULT_INSTANCE();

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
  MIDI.setHandleStart(HandleStart);  
  MIDI.setHandleProgramChange(HandleProgramChange); 
 
#endif

#if 0
  //osc.setFreq(440u); // default frequency
  //myosc.setFreq(440.0f);
  myosc.setFreq(440.0f*4.0f);
  myosc.setPulseWidth(32);

  envelope.setADLevels(255, 200);
  envelope.setTimes(50, 200, 65535, 5000);
#endif
  
  for(int i=0; i<MAX_NOTES; i++)
  {
   // chan[i].osc.setType(WAVE_TRI);
    chan[i].osc.setTable(SAW512_DATA);
    //chan[i].osc.setPulseWidth(16);
    chan[i].note=128;
    chan[i].env.setADLevels(127,127);
    chan[i].env.setTimes(10, 10, 20000, 510);
//    chan[i].fxenv.setADLevels(255,255);
//    chan[i].fxenv.setTimes(10, 10, 0, 2000);
  }
 
  lpf.setResonance(128);
  lpf.setCutoffFreq(190);

#if DRUM_SAMPLES
  kickSamp.setFreq((float) kick909_SAMPLERATE / (float) kick909_NUM_CELLS);
  snareSamp.setFreq((float) snare909_SAMPLERATE / (float) snare909_NUM_CELLS);
  hihatcSamp.setFreq((float) hihatc909_SAMPLERATE / (float) hihatc909_NUM_CELLS);
  hihatoSamp.setFreq((float) hihato_SAMPLERATE / (float) hihato_NUM_CELLS);
  //snareSamp.start();
#endif

  //setupFastAnalogRead(); // optional  
//  adcEnableInterrupt();  // for analog reads

  startMozzi(CONTROL_RATE); 
}

void HandleProgramChange (byte channel, byte number)
{
   // change wave
  waveType = (number) % 4;

  for(int i=0; i<MAX_NOTES; i++)
  {
#if 1
    switch(waveType) {
//    case 4:
//      chan[i].osc.setTable(SQUARE_ANALOGUE512_DATA);
//      break;
    case 0:
      chan[i].osc.setTable(SAW512_DATA);
      break;
    case 1:
      chan[i].osc.setTable(COS512_DATA);
      break;
    case 2:
      chan[i].osc.setTable(SAW512_DATA);
      break;
    case 3:
      chan[i].osc.setTable(SAW512_DATA);
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


void HandleNoteOn(byte channel, byte note, byte velocity) {
  //osc.setFreq(mtof(note)); // simple but less accurate frequency
  //osc.setFreq_Q16n16(Q16n16_mtof(Q8n0_to_Q16n16(note))); // accurate frequency
  //myosc.setFreq(mtof(note)); // accurate frequency
  //envelope.noteOn();
 
   // start note on next channel - now with v basic "note priority"!
int index;
int empty=99;

if (0==velocity) {empty=0;} 
else {
      digitalWrite(LED,HIGH);
      for(int i=0; i<MAX_NOTES; i++)
       {
        index=((currentChan+i)%(MAX_NOTES-hatsOn)+hatsOn);
        if (chan[index].gain==0) {empty=index; break;}
        }
      }

if (empty==99) // ie all no empty channels found so far?
  {
  for(int i=0; i<MAX_NOTES; i++)
    {
    index=((currentChan+i)%(MAX_NOTES-hatsOn)+hatsOn);
    if (chan[index].note==128) {empty=index; break;}
    }   
  }
  
if (empty<99) {currentChan=empty;} else 
  {
  currentChan = (currentChan + 1) % MAX_NOTES;  // add 1, wrap around
  }  
  
  chan[currentChan].note = note;
  chan[currentChan].osc.setFreq(float(mtof(note)));
//  (float(mtof(note))); // accurate frequency?
  if (waveType==1) {chan[currentChan].osc.setFreq(float(mtof(note+24)));} //high triangle
  chan[currentChan].env.noteOn();
//  chan[currentChan].fxenv.noteOn();
  
}

void HandleNoteOff(byte channel, byte note, byte velocity) { 
  //envelope.noteOff();
  
  // find which channel was playing this note
  for(int i=0; i<MAX_NOTES; i++) {
    if (chan[i].note == note) {
      // kill it
      chan[i].env.noteOff();
      chan[i].note=128;
    } 
  }
  
  digitalWrite(LED,LOW);
}

void HandleControlChange (byte channel, byte number, byte value)
{
  // http://www.indiana.edu/~emusic/cntrlnumb.html
  switch(number) {
  case 1:      // modulation wheel
  int divided= constrain((value /16)+2,1,7); // values=0-127, map to 0-7
  bitmask=((0b00000000111111111)>>divided)<<divided;
  break;
  
    //gain = value*2;
/*    lpf.setCutoffFreq( int(value*1.5f) );  // control messages are in [0, 127] range
    lpf.setResonance(  int(value*1.4f) );      
//     for(int i=0; i<MAX_NOTES; i++) {chan[i].env.setTimes(10, 10, 0, 900+value); }
*/

    break;
  }
}

// pitchbend = control strip + button on Keytar
void HandlePitchBend (byte channel, int bend)
{
  //bend value from +/-8192
  //lfo.setFreq((unsigned int) (bend+8192)>>7);
  for(int i=0; i<MAX_NOTES; i++) {
//    chan[i].osc.setPulseWidth((bend+8192) >> 8); 
  }
  
}

void HandleStop () { //this is the Back button on the Xbox keyboard
//  octave *= 2.0f;
//  if (octave > 16.0f) octave = 1.0f;
//Dave G: no longer used : )
delayOn=(! delayOn);
  for(int i=0; i<MAX_NOTES; i++)
  {
if (delayOn) 
    {
   chan[i].env.setTimes(10, 10, 0, 1020);
    }
        else
    {
    chan[i].env.setTimes(10, 10, 20000, 510);
    }
  }
}

void HandleStart () { //this is the Back button on the Xbox keyboard
//  octave *= 2.0f;
//  if (octave > 16.0f) octave = 1.0f;
//Dave G: no longer used : )
snareOn=(! snareOn);
}

void HandleContinue () { // the big round button on all keytars
bassOn=(bassOn+1)% ((BASSPLUS4-4)+1) ;
if (0==bassOn) {hatsOn=0;} else {hatsOn=1;}
}

void updateControl(){
#if ENABLE_MIDI
  MIDI.read();
#endif

  //envelope.update();
  
  // update playing envelopes
  for(int i=0; i<MAX_NOTES; i++) {
    chan[i].env.update(); 
//    chan[i].fxenv.update(); 
    //int tremolo=( chan[i].fxenv.next() & 0b00111111 );
    int tremolo=chan[i].env.next();
if (delayOn) 
{chan[i].gain=(tremolo * (tremolo&0b00011111) )>>5;}  
else {chan[i].gain=tremolo;}
//subtler effect: {chan[i].gain=(tremolo + (tremolo&0b00011111) )>>1;} 
    if (chan[i].gain>threshold) {chan[i].gain=threshold;}
    // threshold=255/MAX_NOTES, genuinely not sure if this is best approach but stops crashes :)
  }


#if DRUM_SAMPLES
  if (enableDrums) { 
    // drums  
    beatCounter++;
    if (beatCounter > ticksPerStep) {
      beat = (beat+1) & 0x1f;
      beatCounter = 0;
      
//if (beat==0 || beat==8) {HandleNoteOn(1, 48, 128); }
      
      //semi-perceptible BPM-locked phaser
 if (3==waveType){int slowbeat=beat>>1;
      int divided=(slowbeat+6)>>1; if (slowbeat>7) {divided=(22-slowbeat)>>1;} 
       bitmask=((0b00000000111111111)>>divided)<<divided;
 }
//      lpf.setCutoffFreq( (filterlevel+11) <<3 ); //maps to 72-192
//      lpf.setResonance( (filterlevel+10) <<3);      
      if (pattern[0][beat] && hatsOn) hihatcSamp.start();
      if (pattern[1][beat] && hatsOn) hihatoSamp.start();
      if (pattern[2][beat] && snareOn) snareSamp.start();
      if (pattern[3][beat] && snareOn) kickSamp.start();
      if (pattern[bassOn+3][beat] && bassOn) { HandleNoteOn(0, pattern[bassOn+3][beat]+24, 0) ;}
    }
  }
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
  int x = hihatcSamp.next() + hihatoSamp.next();
  for(int i=0; i<MAX_NOTES; i++) {
    int raw=chan[i].osc.next();
    if (i==0 && 1<waveType) {int squared=((( (raw+128) & bitmask) && 1)<<8)-128; raw=squared;}
    x += (int) (chan[i].gain * raw)>>8;
  }

//Dave G: helps crash less if goes through some sort of waveshaper here?
  x = aCompress.next(256u + x);

  //x = (x*gain)>>8;
//  x = lpf.next(x);

#if DRUM_SAMPLES
  if (enableDrums) { 
    // drums please!
    int drums = kickSamp.next() + snareSamp.next() ;
    //drums = (drums>>4)<<4;  
    x += drums;
  }
#endif
  
  return x;
}


void loop() {
  audioHook(); // required here
} 



