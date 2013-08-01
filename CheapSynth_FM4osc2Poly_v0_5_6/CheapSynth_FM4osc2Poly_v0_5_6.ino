
/*  
 *  FM+LFO notes from Mozzi in response to MIDI input,
 *  using the standard Arduino MIDI library:
 *  http://playground.arduino.cc/Main/MIDILibrary
 *
 *  Mozzi help/discussion/announcements:
 *  https://groups.google.com/forum/#!forum/mozzi-users
 *
 *  Massively based on examples by Tim Barrass 2013nextenv
 *  This example code is in the public domain.
 *
 *  sgreen/dpape/dgreen - modified to bring together harmonic FM, LFO, portamento, 
 *  mono note priority, & Rock Band keytar controls - http://www.cheapsynth.com/
 *
 *  Audio output from pin 9 (pwm)
 *  Midi plug pin 2 (centre) to Arduino gnd, pin 5 to RX (0)
 *  http://www.philrees.co.uk/midiplug.htm
 *
 On Rock Band keytar, switch between patch modes using the big transparent button above
the LEDs, as follows...

Mode 0 - monophonic FM lead sound with variable modulation harmonics, portamento, modulation depth 
on modulation strip, low-frequency oscillator (LFO) speed on pitch bend strip (ie button held down)

Mode 1 - (2-note polyphonic) Squarewave added to squared-off sinewave at variable harmonics, 
pitch bend strip adjusts "duty cycle", ie how much the squared sine is On vs Off

Mode 2 - (2-note polyphonic) Actually sawtooth wave, pitch bend strip gives chorus-y effect by
variably detuning one of the current notes 

Mode 3 - (2-note "polyphonic") Squarewave XOR'd with sinewaves at variable harmonics, similar fake-LFO
detuning on pitch bend strip, massive upwards zoom on both notes when no portamento!

Mode 4 - intentionally left blank so you know where you are

Standard controls:

Different modulation freq harmonics on the right-hand corner up and down keys between octave up
and octave down (all modes except Mode 2).

Portamento speed on Back arrow to the left of big transparent Mode button, toggles between no portamento
(ie 2-note polyphony in modes that support it), 10ms fast glide between notes, 400ms slow glide.

[updated: this note decay now doesn't do anything as I've moved everything to "slow" ADSR..!]
Note decay length on forward arrow to the right of big transparent Mode button (currently applies 
to less computationally demanding envelopes in Mode 0 and 2), toggles between short/medium/long
*/

#include <MIDI.h>
#include <MozziGuts.h>
#include <Oscil.h> /oscSaw/ oscillator template
#include <Line.h> // for envelope
#include <tables/cos2048_int8.h> // table for Oscils to play
#include <tables/saw1024_int8.h> // table for Oscils to play
#include <tables/smoothsquare8192_int8.h> // NB portamento requires table > 512 size?
#include <mozzi_midi.h>
#include <ADSRslow.h>
#include <mozzi_fixmath.h>
#include <LowPassFilter.h>
#include <Portamento.h>
#include <Ead.h> 

// use #define for CONTROL_RATE, not a constant
#define CONTROL_RATE 128 // powers of 2 please

//  Mozzi example uses COS waves for carrier and modulator
//  DP: "gets brutal very fast" (esp in higher octaves) if you use a saw/square carrier
//  gets subtly more brutal if you use smaller wavetables (fewer samples per cycle)
Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> oscCarrier(SMOOTHSQUARE8192_DATA); // huge file :(
Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> oscSquare2(SMOOTHSQUARE8192_DATA); // huge file :(
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscModulator(COS2048_DATA);
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscModulator2(COS2048_DATA);
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscLFO(COS2048_DATA);
Oscil<SAW1024_NUM_CELLS, AUDIO_RATE> oscSaw1(SAW1024_DATA); 
Oscil<SAW1024_NUM_CELLS, AUDIO_RATE> oscSaw2(SAW1024_DATA); 

// envelope generator
ADSR <CONTROL_RATE> envelope;
ADSR <CONTROL_RATE> envelope2;
Ead kEnvelopeGuitar1(CONTROL_RATE); //left in here for future use but currently not vital?
Ead kEnvelopeGuitar2(CONTROL_RATE);

Portamento <CONTROL_RATE>aPortamento;
//LowPassFilter lpf; // LPF left over from earlier versions of code, may reinstate!
//int crushCtrl = 0; // ditto bitcrush

#define LED 13 // to see if MIDI is being recieved

long vibrato=0;
long vibrato2=0;
int chanGain1;
int chanGain2;
int LFO;
unsigned int carrierXmodDepth1;
unsigned int carrierXmodDepth2;

// initialise globals (some eg amplitude no longer used) 
int maxmode=4;
float carrierFreq = 10.f;
float carrier2 = 10.f;
float modFreq = 10.f;
float modFreq2 = 10.f;
float modDepth = 0;
float amplitude = 0.f;
float modOffset =1;
byte lastnote1=0;
byte lastnote2=0;
int nextenv=1;
byte portSpeed=0;
byte noteLength=1;
float shifted=0.1f;
byte mode=0;
unsigned long harmonic;
int squaredsin1;
int squaredsin2;
int bitmask=64;
int multiplier;

float modOffsets[] = {
4,3.5,3,2.5,2,1.5,1,0.6666666,0.5,0.4,0.3333333,0.2857,0.25,0,0,0,0,0,0,
}; // harmonic ratios corresponding to DP's preferred intervals of 7, 12, 7, 19, 24, 0, 12, -12, etc

void setup() {
  pinMode(LED, OUTPUT);

  // Initiate MIDI communications, listen to all channels
  MIDI.begin(MIDI_CHANNEL_OMNI);

  // Connect the HandleNoteOn function to the library, so it is called upon reception of a NoteOn.
  MIDI.setHandleNoteOn(HandleNoteOn);  // Put only the name of the function
  MIDI.setHandleNoteOff(HandleNoteOff);  // Put only the name of the function
  MIDI.setHandleControlChange(HandleControlChange);
  MIDI.setHandlePitchBend(HandlePitchBend);  // Put only the name of the function
  MIDI.setHandleProgramChange(HandleProgramChange); 
  MIDI.setHandleContinue(HandleContinue); 
  MIDI.setHandleStop(HandleStop); 
  MIDI.setHandleStart(HandleStart); 

  envelope.setADLevels(127,100);
  envelope.setTimes(20,20,20000,1200); // 20000 is so the note will sustain 20 seconds unless a noteOff comes
  envelope2.setADLevels(127,100);
  envelope2.setTimes(20,20,20000,1200); // 20000 is so the note will sustain 20 seconds unless a noteOff comes

  kEnvelopeGuitar1.set(20, 4000);
  kEnvelopeGuitar2.set(20, 4000);

  aPortamento.setTime(0u);
//  lpf.setResonance(200);
//  lpf.setCutoffFreq(255);
  oscLFO.setFreq(10); // default LFO frequency

  startMozzi(CONTROL_RATE); 
}


void UpdateFreqs()
{
  oscCarrier.setFreq(carrierFreq);
  oscSaw1.setFreq(carrierFreq);
  carrierXmodDepth1=carrierFreq * modDepth;
  modFreq=(carrierFreq * modOffset / (64/bitmask) );

  oscSquare2.setFreq(carrier2);
  oscSaw2.setFreq(carrier2 * (1.0f + ((shifted -0.1f) / 384)) );
  carrierXmodDepth2=carrier2 * modDepth;
  modFreq2=(carrier2 * modOffset / (64/bitmask) );
  
  oscModulator.setFreq( modFreq ); 
  oscModulator2.setFreq( modFreq2 ); 
}

void HandleProgramChange (byte channel, byte number)
{
  modOffset = modOffsets[number%15];
  UpdateFreqs();
//  modFreq=(carrierFreq * modOffset / (64/bitmask) );
//dreadful hack as ProgramChange returns absolute prog number 0-127, "gets stuck" at upper/lower ends 
}

void HandleNoteOn(byte channel, byte note, byte velocity) { 
  aPortamento.start(note); 
//  carrierFreq=(mtof(note)); // simple but less accurate frequency
//  osc.setFreq_Q16n16(Q16n16_mtof(Q8n0_to_Q16n16(note))); // accurate frequency
if ( portSpeed) 
 {nextenv=1;   lastnote2=999;}

  if (nextenv==1) 
  { 
  lastnote1=note;
  envelope.noteOn();
kEnvelopeGuitar1.start();
  digitalWrite(LED,HIGH);
  nextenv=2; // send next note to other envelope, unless overridden in the meantime
  
  carrierFreq = Q16n16_to_float (Q16n16_mtof(Q8n0_to_Q16n16(lastnote1)));
  UpdateFreqs();
  }
  else
  {
  lastnote2=note;
  envelope2.noteOn();
kEnvelopeGuitar2.start();
  digitalWrite(LED,HIGH); 
  nextenv=1; // send next note to other envelope, unless overridden in the meantime

  carrier2 = Q16n16_to_float (Q16n16_mtof(Q8n0_to_Q16n16(lastnote2)));
  UpdateFreqs();
  }

}

void HandleStop () { //this is the Back button on the Xbox keyboard
 portSpeed++; 
 {if (portSpeed>2) portSpeed = 0;}
  aPortamento.setTime( (portSpeed*portSpeed*100) ); // values of 0,100,400 milliseconds? (next 900)
}

void HandleStart () { //CURRENTLY NOT USED 
 noteLength=(noteLength+1)%3; // loop from 0-2
   kEnvelopeGuitar1.set(20, (noteLength+1)*(noteLength+1)*(noteLength+1)*500 );
   kEnvelopeGuitar2.set(20, (noteLength+1)*(noteLength+1)*(noteLength+1)*500 );
   //cube into values around 500, 4000, 13500
}

void HandleContinue () { // the big round button on all keytars
 mode++; 
 {if (mode>maxmode) mode = 0;} //currently has a silent mode at the end so you know where you are : )
}

void HandleNoteOff(byte channel, byte note, byte velocity) { 
  digitalWrite(LED,LOW); // may as well switch LED off

  if ( (note == lastnote1) ) //kill envelope if prev note released 
  {
  envelope.noteOff(); 
  if (chanGain2) {nextenv=1;}
} 

  if ( (note == lastnote2) ) 
  {envelope2.noteOff(); 
  if (chanGain1) {nextenv=2;}
  }
  
}


void HandlePitchBend (byte channel, int bend)
{
//bend value from +/-8192, translate to 0.1-8 Hz?
shifted= float ((bend+8500) /2048.f ) +0.1f;  
if (mode==0) {oscLFO.setFreq(shifted); }
if (mode==1) 
  {
   bitmask= 1 << (int (shifted)); multiplier=7-(int (shifted)); 
   UpdateFreqs();
  }
  else {bitmask=64;}

if (mode==2) {   oscSaw2.setFreq(carrier2 * (1.0f + ((shifted -0.1f) / 384)) ); }
}

void HandleControlChange (byte channel, byte number, byte value)
{
// http://www.indiana.edu/~emusic/cntrlnumb.html

/*
Serial.end(); // sample debug code to check values through (noisy) terminal interface 
Serial.begin(38400);
Serial.print("  number:");   
Serial.print(number);
Serial.print(" value:");   
Serial.println( value );
Serial.println();
Serial.end();
Serial.begin(31250);
*/

  switch(number) {
//  case 3: // could trap different controller IDs here

  case 1: // modulation wheel
  //lpf.setResonance(value*2);

  //WORKING MODDEPTH CONTROLLER
  float divided= float (value /46.f ); // values=0-127, map to 0-2.75
  modDepth = (divided * divided);      // square to 0-8
  if (modDepth>7.5) {modDepth=7.5;}        // sanity check (prob unnecessary) 
  if (modDepth<0.2) {modDepth=0;}      // easier to get pure tone 
  UpdateFreqs();
  break;
  }
}

void updateControl(){
  MIDI.read();
  envelope.update();
  envelope2.update();

  chanGain1 = envelope.next();
  chanGain2 = envelope2.next();

//  chanGain1 = kEnvelopeGuitar1.next();
//  chanGain2 = kEnvelopeGuitar2.next();
  if(chanGain1 > 120) chanGain1 = 120;
  if(chanGain2 > 120) chanGain2 = 120;

if ( portSpeed ) 
  {
  carrierFreq = Q16n16_to_float ( aPortamento.next() ); //NB presumably returns frequency as Q16n16
  UpdateFreqs();
  }

if (mode==3)
    {
    modFreq *= 1.0f +(shifted / 384); // generates fake LFO via detuning mod osc
    modFreq2*= 1.0f +(shifted / 384); // generates fake LFO via detuning mod osc
//    modFreq2*= 1 + (shifted -0.1f) / 384); // generates fake LFO via detuning mod osc
    oscModulator.setFreq( modFreq ); 
   oscModulator2.setFreq( modFreq2 ); 
    }

}

// strip the low bits off! - not used in current implementation
int bitCrush(int x, int a)
{
  return (x>>a)<<a;
}

int updateAudio(){
  
    switch(mode) 
    {
  case 0: // FM with LFO 
//NB this multiplication split into 2 chunks to preserve small precise values?  
LFO=oscLFO.next();
vibrato = ( LFO * oscModulator.next() ) >>7 ;
vibrato*= carrierXmodDepth1;
vibrato2 = ( LFO * oscModulator2.next() ) >>7 ;
vibrato2*= carrierXmodDepth2;
return (int) (
              (oscCarrier.phMod( vibrato >>3 ) * chanGain1 ) 
            + (oscSquare2.phMod( vibrato2>>3 ) * chanGain2 ) 
            ) >> 9;
// >> 9 = 9 fast binary divisions by 2 = divide by 512
  break;

  case 1: // ADDING square + "converted" sine
//squaredsin1= ( (oscModulator.next() & 0b0000000001000000)-1 ) <<1;
squaredsin1= ( (oscModulator.next() & bitmask) ) <<multiplier;
squaredsin2= ( (oscModulator2.next() & bitmask) ) <<multiplier;
return (int) 
( 
  ( ( (oscCarrier.next() + squaredsin1 )>>1 )* chanGain1 )
+ ( ( (oscSquare2.next() + squaredsin2 )>>1 )* chanGain2 )
) >> 8;
  break;

  case 2: // 2 tri poly
return (int)( 
             ( oscSaw1.next() *chanGain1 )
           + ( oscSaw2.next() *chanGain2 ) 
            ) >> 9;
  break;

  case 3: // STARTED AS 2 sq poly XORd with respective sines, now big whoop
return (int)(
             ( (oscCarrier.next()^oscModulator.next()) * chanGain1 )  
           + ( (oscSquare2.next()^oscModulator2.next())* chanGain2 ) 
            ) >> 8;
//return (int) ( (oscCarrier.next() | ( (oscModulator.next() * int (modDepth+1))>>3 ) )  * (envelope.next() ) ) >> 7;
  break;

  case 13: // 2 sq poly ORd with respective sines
return (int) ( ( (oscCarrier.next()|oscModulator.next() ) * envelope.next()) + ( (oscSquare2.next()|oscLFO.next() ) * envelope2.next()) ) >> 8;
//return (int) ( (oscCarrier.next() | ( (oscModulator.next() * int (modDepth+1))>>3 ) )  * (envelope.next() ) ) >> 7;
  break;


  case 6: // 2 sq poly
return (int) ( (oscCarrier.next() * envelope.next()) + (oscSquare2.next() * envelope2.next()) ) >> 8;
//return (int) ( (oscCarrier.next() | ( (oscModulator.next() * int (modDepth+1))>>3 ) )  * (envelope.next() ) ) >> 7;
  break;

//case 5 intentionally left blank

/*
  case 6: // ADDING squares
return (int) ( (oscCarrier.next() + oscSquare2.next() )  * (envelope.next() ) ) >> 8;
//return (int) ( (oscCarrier.next() | ( (oscModulator.next() * int (modDepth+1))>>3 ) )  * (envelope.next() ) ) >> 7;
  break;
*/
// case 1: // AND with fake LFO
//all this 'harmonic' stuff a failed attempt to use the same LFO as FM, causes massive beat f/x?
//harmonic = ( oscLFO.next() * oscModulator.next() ) >> 7; 
//harmonic = ( harmonic * int (modDepth+1) ) >>3 ;
//return (int) ( (oscCarrier.next() ^ ( harmonic ))  * (envelope.next() ) ) >> 7;
//return (int) ( (oscCarrier.next() & ( (oscModulator.next() * int (modDepth+1))>>3 ) )  * (envelope.next() ) ) >> 7;
//  break;

/*  case 3: // OR with fake LFO
//int harmonic = ( oscLFO.next() * oscModulator.next() ) >>7;
//return (int) ( (oscCarrier.next() | ( (oscModulator.next() * int (modDepth+1))>>3 ) )  * (envelope.next() ) ) >> 7;
//  break;
  case 4: // XOR with fake LFO
//int harmonic = ( oscLFO.next() * oscModulator.next() ) >>7;
return (int) ( (oscCarrier.next() ^ ( (oscModulator.next() * int (modDepth+1))>>3 ) )  * (envelope.next() ) ) >> 7;
  break;
  */
    }
    
// OLD CODE FROM BITCRUNCHED SINEWAVE, DIFF WAYS OF DOING THAT 
// int x = (envelope.next() * lpf.next(osc.next()))>>8; 
//  x = bitCrush(x, crushCtrl>>4);
// x = (x * crushCtrl)>>4;  // simple gain
// x = bitCrush(x, 6);
//  x = lpf.next(x);
//  return x;
}


void loop() {
  audioHook(); // required here
} 

