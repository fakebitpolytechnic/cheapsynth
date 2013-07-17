// CheapSynth "sequencer" mode 2013 - quite clumsily stuck (by Dave Green) on top of
// "A very simple MIDI synth."
// Greg Kennedy 2011
// http://forum.arduino.cc/index.php?topic=79326.0
//
// You should be able to spot the note arrays further down, and can use the modulation MIDI control
// to adjust the "modulation depth" (ie how grainy the notes sound)
//
// Other controls: Program Up + Down = change tempo
// Continue = "Pause" 

#include <avr/pgmspace.h>

#include <MozziGuts.h>
#include <Oscil.h>
#include <tables/cos256_int8.h> // table for Oscils to play
#include <tables/saw256_int8.h> // table for Oscils to play
#include <tables/square_analogue512_int8.h> // table for Oscils to play
#include <mozzi_midi.h> // for mtof

#define CONTROL_RATE 128 // powers of 2 please

//  Mozzi example uses COS waves for carrier and modulator
//  gets brutal very fast if you use a saw/square carrier
//  gets subtly more brutal if you use smaller wavetables (fewer samples per cycle)
Oscil<SQUARE_ANALOGUE512_NUM_CELLS, AUDIO_RATE> oscCarrier(SQUARE_ANALOGUE512_DATA);
Oscil<COS256_NUM_CELLS, AUDIO_RATE> oscModulator(COS256_DATA);

int midiNotes[] = {
//these are MIDI note values, ie 12 = C, 24 = next C up
//NB note values of 25+ currently considered as absolute (ie don't change with keypress)
/*
16,16,28,28,16,16,26,28,//lee coombs
16,16,28,28,16,16,26,28,
16,16,28,28,16,16,26,28,
16,16,28,28,16,16,26,28,
*/
//octaves - blue mon, hung up, erasure? etc
12,24,12,24,12,24,12,24,12,24,12,24,12,24,12,24,
12,24,12,24,12,24,12,24,12,24,12,24,12,24,12,24,

//descending bass (eg Bowie's Heroes)
24,24,21,19,24,24,21,19,24,24,21,19,24,24,21,19,
24,24,21,19,24,24,21,19,24,24,21,19,24,24,21,19,

//different accented octaves (similar to OMD's Enola Gay)
12,12,24,12,12,24,12,24,
12,12,24,12,12,24,12,24,
12,12,24,12,12,24,12,24,
12,12,24,12,12,24,12,24,

/// 12/8 shuffle rhythm (eg Muse)
12,12,24,12,12,24,12,12,24,12,12,24,
12,12,24,12,12,24,12,12,24,12,12,24,
};

int modOffsets[] = {
/*
//lee C  
12, 12, 12, 12, 12, 19, 12, 12, 
12, 12, 12, 12, 12, 19, 12, 12, 
12, 12, 12, 12, 12, 19, 12, 12, 
12, 12, 12, 12, 12, 19, 12, 12, 
*/
12, 12, 12, 12, 12, 12, 12, 12, //octaves
12, 12, 12, 12, 12, 12, 12, 12, 
12, 12, 12, 12, 12, 12, 12, 12, 
12, 12, 12, 12, 12, 12, 12, 12, 

// here I'm using different modulation offsets to distinguish between adjacent notes
// (probably also easy to devise a system to play a rest (ie amplitude=0) if note value=0??
0,12,0,0,12,0,0,0,0,12,0,0,12,0,0,0,//descending bass
0,12,0,0,12,0,0,0,0,12,0,0,12,0,0,0,

0,12,0,0,12,0,0,0,0,12,0,0,12,0,0,0,//accented octaves
0,12,0,0,12,0,0,0,0,12,0,0,12,0,0,0,

12, 12, 12, 12, 12, 12, 12, 12, // shuffle
12, 12, 12, 12, 12, 12, 12, 12, 
12, 12, 12, 12, 12, 12, 12, 12, 
12, 12, 12, 12, 12, 12, 12, 12, 
};

//float modDepths[] = {
//  0, 1, 3, 0.5, 6, 2, 1, 2,
//  0.5, 8, 3, 0.5, 2, 2, 1, 0.125
//};

float note = 24;
int lastByte;
float bendByte =50;

int tuneOffset = 0;
int tickLengthConverter = 26;
int arraySize = 32; 
int controlTick = 0;
int arrayIndex = 0;
int sequenced;
int playflag = 1;

float carrierFreq = 10.f;
float modFreq = 10.f;
float bendRead = 10.f;
long modDepth = 1;

float amplitude = 1.f;

void setParams(){
  //  mtof converts MIDI note range to Hz freq
sequenced=midiNotes[arrayIndex + (arraySize*tuneOffset)];

if (sequenced<25) {sequenced += note; } else {sequenced += 36; }
//if (tuneOffset >0) {sequenced -= 12; }

carrierFreq = mtof(sequenced);

//  FM harmonics are most musical when modulator's @ simple musical offsets from carrier
//  EG +12 semitones (1 oct), -24 semitones (-2 oct), +19 semitones (+1 octave and a fifth)
modFreq = mtof( ( sequenced + modOffsets[arrayIndex+ (arraySize*tuneOffset)] ) );
  
//  I like to make modulation depth (in Hz) some ratio of the carrier's freq
//  That way, the perceived intensity of the FM is the same throughout the note range
//  modDepth = (long) 6;
bendRead=(bendByte / 30);
modDepth = (long) carrierFreq * bendRead * bendRead; // 7 was good
  
  //  Set oscillator frequencies
  oscCarrier.setFreq(carrierFreq);
  oscModulator.setFreq(modFreq);
}

//  Move on to the next note
void incrementIndexes(){
  arrayIndex ++;
  int sizelimit=arraySize-1;

//Sample code on how to view what's going on  
Serial.end();
Serial.begin(115200);
Serial.print(" arrayIndex:");   
Serial.println( arrayIndex
                );
Serial.println();
Serial.end();
Serial.begin(31250);
  
if (tuneOffset==3) {sizelimit = 23;} // hack to allow patterns of different length
if(arrayIndex>sizelimit){
    arrayIndex = 0;
  }
}

// NOW BACK TO YOUR REGULARLY SCHEDULED MIDI SCAN - comments by Greg Kennedy 2011

#define statusLed 13
#define tonePin 8

// MIDI channel to answer to, 0x00 - 0x0F
#define myChannel 0x00
// set to TRUE and the device will respond to all channels
#define respondAllChannels true

// midi commands
#define MIDI_CMD_NOTE_OFF 0x80
#define MIDI_CMD_NOTE_ON 0x90
#define MIDI_CMD_KEY_PRESSURE 0xA0
#define MIDI_CMD_CONTROLLER_CHANGE 0xB0
#define MIDI_CMD_PROGRAM_CHANGE 0xC0
#define MIDI_CMD_CHANNEL_PRESSURE 0xD0
#define MIDI_CMD_PITCH_BEND 0xB0

// this is a placeholder: there are
//  in fact real midi commands from F0-FF which
//  are not channel specific.
// this simple synth will just ignore those though.
#define MIDI_CMD_SYSEX 0xF0

// a dummy "ignore" state for commands which
//  we wish to ignore.
#define MIDI_IGNORE 0x00

// midi "state" - which data byte we are receiving
#define MIDI_STATE_BYTE1 0x00
#define MIDI_STATE_BYTE2 0x01

// MIDI note to frequency
//  This isn't exact and may sound a bit detuned at lower notes, because
//  the floating point values have been rounded to uint16.
//  Based on A440 tuning.

// I would prefer to use the typedef for this (prog_uint16_t), but alas that triggers a gcc bug
// and does not put anything into the flash memory.

// Also note the limitations of tone() which at 16mhz specifies a minimum frequency of 31hz - in other words, notes below
// B0 will play at the wrong frequency since the timer can't run that slowly!
uint16_t frequency[128] PROGMEM = {8, 9, 9, 10, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 19, 21, 22, 23, 24, 26, 28, 29, 31, 33, 35, 37, 39, 41, 44, 46, 49, 52, 55, 58, 62, 65, 69, 73, 78, 82, 87, 92, 98, 104, 110, 117, 123, 131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247, 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, 523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988, 1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976, 2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951, 4186, 4435, 4699, 4978, 5274, 5588, 5920, 5920, 6645, 7040, 7459, 7902, 8372, 8870, 9397, 9956, 10548, 11175, 11840, 12544};

//setup: declaring iputs and outputs and begin serial
void setup() {
  startMozzi(CONTROL_RATE);
    
  pinMode(statusLed,OUTPUT);   // declare the LED's pin as output

  pinMode(tonePin,OUTPUT);           // setup tone output pin

  //start serial with midi baudrate 31250
  // or 38400 for debugging (eg MIDI over serial from PC)
  Serial.begin(31250);

  // indicate we are ready to receive data!
  digitalWrite(statusLed,HIGH);
}

//loop: wait for serial data - DG: in hindsight, this should really go in updateControl()
// (impressed that it even works this way round!)
void loop () {
  audioHook();
  static byte lastCommand = MIDI_IGNORE;
  static byte state;

  while (Serial.available()) {

    // read the incoming byte:
    byte incomingByte = Serial.read();

    // Command byte?
    if (incomingByte & 0b10000000) {
      if (respondAllChannels ||
             (incomingByte & 0x0F) == myChannel) { // See if this is our channel

    if (incomingByte==0xFC) {tuneOffset--; {if (tuneOffset<0) tuneOffset=3;} }
    if (incomingByte==0xFA) {tuneOffset++; {if (tuneOffset>3) tuneOffset=0;} }
    if (incomingByte==0xFB) {playflag++; {if (playflag>1) playflag=0;} }

        lastCommand = incomingByte & 0xF0;
      } else { // Not our channel.  Ignore command.
        lastCommand = MIDI_IGNORE;
      }
      state = MIDI_STATE_BYTE1; // Reset our state to byte1.
    } else if (state == MIDI_STATE_BYTE1) { // process first data byte
      if ( lastCommand==MIDI_CMD_NOTE_OFF )
      { // if we received a "note off", make sure that is what is currently playing
        if (note == incomingByte) noTone(tonePin);
        state = MIDI_STATE_BYTE2; // expect to receive a velocity byte
      } else if ( lastCommand == MIDI_CMD_NOTE_ON ){ // if we received a "note on", we wait for the note (databyte)
        lastByte=incomingByte;    // save the current note
        state = MIDI_STATE_BYTE2; // expect to receive a velocity byte
      }
      // implement whatever further commands you want here
      else if ( lastCommand == MIDI_CMD_PITCH_BEND ){ 
        bendByte=incomingByte;    // save the current note
        state = MIDI_STATE_BYTE1; // expect to receive a velocity byte
      }      
      
      else if ( lastCommand == MIDI_CMD_PROGRAM_CHANGE )
      { 
tickLengthConverter = 31 - (incomingByte % 32);

       // save the current note
        state = MIDI_STATE_BYTE1; // expect to receive a velocity byte
      }      
            
            
    } else { // process second data byte
      if (lastCommand == MIDI_CMD_NOTE_ON) {
        if (incomingByte != 0) {
          note = lastByte;
//        tone(tonePin,(unsigned int)pgm_read_word(&frequency[note]));
// NB ToneMIDISynth generally good for diagnosing MIDI activity 
//          setParams();               
        } else if (note == lastByte) {
         noTone(tonePin);
        }
      }
      state = MIDI_STATE_BYTE1; // message data complete
                                 // This should be changed for SysEx
    }
  }
}

void updateControl(){
  if(controlTick == 0)
  {
      setParams();
      incrementIndexes();
  }
  
  //  Update controlTick
  controlTick +=playflag;

  if(controlTick > tickLengthConverter)
  {
    controlTick = 0;
    }
}

int updateAudio(){
    long vibrato = modDepth * oscModulator.next();
    return (int) (oscCarrier.phMod(vibrato)) * amplitude;
}
