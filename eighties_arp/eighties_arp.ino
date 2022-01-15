/**
 * eighties_arp.ino -- playing with arpeggios, testing my Arpy class
 *  - arp arp arp
 *  - two pots, two buttons, no keys!
 *  - no musical knowledge needed, just plug it, start twisting knobs
 *  - for demo see: https://www.youtube.com/watch?v=Ql72YoCJ8-8
 *  
 *  Circuit:
 *  - QT Py M0 or similar SAMD21-based board
 *  - See included "eighties_arp_wiring.jpg"
 *  - Wire a 1k resistor from audio out to Tip & Ring1 of a 
 *     standard TRRS 1/8" (3.5mm) headphone plug
 *  - For better quality, add a 500  1uF cap from 
 *  - Plug into portable speaker
 *  - Potentiometers on A1 & A2
 *  - Buttons on D7, D6
 *  
 *  Compiling:
 *  - Bounce2 library - https://github.com/thomasfredericks/Bounce2/
 *  - Adafruit TinyUSB library - https://github.com/adafruit/Adafruit_TinyUSB_Arduino/
 *  - Mozzi library - https://github.com/sensorium/Mozzi
 *  - Only Mozzi must you hand-install, others from Library Manager
 *  
 *  Code:
 *  - Knob on A1 controls root note
 *  - Knob on A2 controls bpm
 *  - Button on D7 ("RX") controls which arp to play
 *  - BUtotn on D6 ("TX") controls...
 *  
 * 12 Jan 2022 - @todbot
 * 
 */
// Mozzi is very naughty about a few things
//#pragma GCC diagnostic ignored "-Wno-expansion-to-defined"

#define CONTROL_RATE 128 // mozzi rate for updateControl()

#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <Bounce2.h>

#include <MozziGuts.h>
#include <Oscil.h>
#include <tables/saw_analogue512_int8.h> // oscillator waveform
#include <tables/square_analogue512_int8.h> // oscillator waveform
#include <tables/triangle_analogue512_int8.h> // oscillator waveform
#include <tables/sin512_int8.h> // oscillator waveform
#include <tables/cos2048_int8.h> // filter modulation waveform
#include <LowPassFilter.h>
#include <StateVariable.h> // bandpass & highpass filter
#include <ADSR.h>
#include <mozzi_rand.h>  // for rand()
#include <mozzi_midi.h>  // for mtof()

#include "Arpy.h"

const int dacPin  =  0;  // A0 // audio out assumed by Mozzi
const int knobAPin = 1;  // A1
const int knobBPin = 2;  // A2
const int butBPin  = 6;  // D6 "TX"
const int butAPin  = 7;  // D7 "RX"
const int butCPin  = 8;  // D8 "SCK"

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDIusb); // USB MIDI

#define NUM_VOICES 4

Oscil<SAW_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aOscs [NUM_VOICES];
Oscil<COS2048_NUM_CELLS, CONTROL_RATE> kFilterMod(COS2048_DATA);
ADSR <CONTROL_RATE, AUDIO_RATE> envelope;
LowPassFilter lpf;
//StateVariable <BANDPASS> svf; // can be LOWPASS, BANDPASS, HIGHPASS or NOTCH

uint8_t cutoff_freq = 180;
uint8_t resonance = 100; // range 0-255, 255 is most resonant

int bpm = 130;
uint8_t arp_octaves = 1;
uint8_t root_note;
uint8_t patch_num = 0;

Arpy arp = Arpy();

Bounce butA = Bounce();
Bounce butB = Bounce();
Bounce butC = Bounce();

void setup() { 
  MIDIusb.begin(MIDI_CHANNEL_OMNI);
  Serial.begin(115200);
//  while(!Serial) delay(1);
  MIDIusb.turnThruOff();    // turn off echo

  pinMode( LED_BUILTIN, OUTPUT);
  pinMode( knobAPin, INPUT);
  pinMode( knobBPin, INPUT);
  butA.attach( butAPin, INPUT_PULLUP);
  butB.attach( butBPin, INPUT_PULLUP);
  butC.attach( butCPin, INPUT_PULLUP);

  arp.setNoteOnHandler(noteOn);
  arp.setNoteOffHandler(noteOff);
  arp.setRootNote( 60 );
  arp.setBPM( 100 );
  arp.setGateTime( 0.75 ); // percentage of bpm
  arp.on();

  setPatch(0);
  startMozzi();

}

void loop() {
  audioHook();
}

// mozzi function, called every CONTROL_RATE
void updateControl() {
  readMIDI();
  
  butA.update();
  butB.update();
  butC.update();
  int knobA = analogRead(knobAPin);
  int knobB = analogRead(knobBPin);

  root_note = map( knobA, 0,1023, 24, 72);
  float bpm = map( knobB, 0,1023, 100, 5000 );
  arp.setRootNote( root_note );
  arp.setBPM( bpm );

  if( butA.fell() ) {
    arp.nextArpId();
    Serial.printf("--- picking next arp: %d\n", arp.getArpId());
  }
  
  if( butB.fell() ) {
    arp_octaves = arp_octaves + 1; if( arp_octaves==4) { arp_octaves=1; }
    arp.setTransposeSteps( arp_octaves );
    Serial.printf("--- arp steps: %d\n",arp_octaves);
  }
  
  if( butC.fell() ) {
    patch_num = (patch_num+1) % 2;
    setPatch(patch_num);
    Serial.printf("---- next patch: %d\n", patch_num);
  }

  envelope.update();
  
  arp.update();
}

void noteOn(uint8_t note) {
  Serial.print(" noteON:"); Serial.println((byte)note);
  digitalWrite(LED_BUILTIN, HIGH);
  float f = mtof(note);
  for(int i=1;i<NUM_VOICES-1;i++) {
    aOscs[i].setFreq( f * (float)(i*0.01 + 1)); // detune slightly
  }
  envelope.noteOn();
  MIDIusb.sendNoteOn(note, 127, 1); // velocity 127 on chan 1
}

void noteOff(uint8_t note) {
  Serial.print("noteOFF:"); Serial.println((byte)note);
  digitalWrite(LED_BUILTIN, LOW);
  envelope.noteOff();
  MIDIusb.sendNoteOn(note, 0, 1); // velocity 0 on chan 1
}


void readMIDI() {
  if (MIDIusb.read()) {
//    byte mtype = MIDIusb.getType();
//    byte data1 = MIDIusb.getData1();
//    byte data2 = MIDIusb.getData2();
//    byte chan  = MIDIusb.getChannel();
//    if( mtype == midi::NoteOn ) {  
//      Serial.printf("MIDIusb: c:%d t:%2x data:%2x %2x\n", chan, mtype, data1,data2);
//      // we're not using MIDI in yet
//    }
  }
}

void setPatch(uint8_t patchnum) {
  if( patchnum == 0 ) {
    for( int i=0; i<NUM_VOICES; i++) { 
     aOscs[i].setTable(SAW_ANALOGUE512_DATA);
    }
    kFilterMod.setFreq(0.08f);
    envelope.setADLevels(255, 10);
    envelope.setTimes(15, 200, 200, 200 );
    cutoff_freq = 80;
    resonance = 200;
    lpf.setCutoffFreqAndResonance(cutoff_freq, resonance);
//    svf.setCentreFreq(cutoff_freq * 16);
//    svf.setResonance(resonance);
  }
  else if( patchnum == 1 ) { 
    for( int i=0; i<NUM_VOICES; i++) { 
     aOscs[i].setTable(SQUARE_ANALOGUE512_DATA);
    }
    kFilterMod.setFreq(0.08f);
    envelope.setADLevels(255, 10);
    envelope.setTimes(350, 200, 200, 200);
    cutoff_freq = 127;
    resonance = 220;
    lpf.setCutoffFreqAndResonance(cutoff_freq, resonance);
//    svf.setCentreFreq(cutoff_freq * 16);
//    svf.setResonance(resonance);
  }
}

// mozzi function, called every AUDIO_RATE to output sample
AudioOutput_t updateAudio() {
  long asig = (long) 0;
  for( int i=0; i<NUM_VOICES; i++) {
    asig += aOscs[i].next();
  }
  asig = lpf.next(asig);
  return MonoOutput::fromNBit(18, envelope.next() * asig);
//  asig = svf.next(asig);
//  return MonoOutput::fromNBit(17, envelope.next() * asig);
}