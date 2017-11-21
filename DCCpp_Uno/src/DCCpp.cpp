/**********************************************************************

DCC++ BASE STATION
COPYRIGHT (c) 2013-2016 Gregg E. Berman

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses

**********************************************************************/
/**********************************************************************

DCC++ BASE STATION is a C++ program written for the Arduino Uno and Arduino Mega
using the Arduino IDE 1.6.6.

It allows a standard Arduino Uno or Mega with an Arduino Motor Shield (as well as others)
to be used as a fully-functioning digital command and control (DCC) base station
for controlling model train layouts that conform to current National Model
Railroad Association (NMRA) DCC standards.

This version of DCC++ BASE STATION supports:

  * 2-byte and 4-byte locomotive addressing
  * Simultaneous control of multiple locomotives
  * 128-step speed throttling
  * Cab functions F0-F28
  * Activate/de-activate accessory functions using 512 addresses, each with 4 sub-addresses
      - includes optional functionailty to monitor and store of the direction of any connected turnouts
  * Programming on the Main Operations Track
      - write configuration variable bytes
      - set/clear specific configuration variable bits
  * Programming on the Programming Track
      - write configuration variable bytes
      - set/clear specific configuration variable bits
      - read configuration variable bytes

DCC++ BASE STATION is controlled with simple text commands received via
the Arduino's serial interface.  Users can type these commands directly
into the Arduino IDE Serial Monitor, or can send such commands from another
device or computer program.

When compiled for the Arduino Mega, an Ethernet Shield can be used for network
communications instead of using serial communications.

DCC++ CONTROLLER, available separately under a similar open-source
license, is a Java program written using the Processing library and Processing IDE
that provides a complete and configurable graphic interface to control model train layouts
via the DCC++ BASE STATION.

With the exception of a standard 15V power supply that can be purchased in
any electronics store, no additional hardware is required.

Neither DCC++ BASE STATION nor DCC++ CONTROLLER use any known proprietary or
commercial hardware, software, interfaces, specifications, or methods related
to the control of model trains using NMRA DCC standards.  Both programs are wholly
original, developed by the author, and are not derived from any known commercial,
free, or open-source model railroad control packages by any other parties.

However, DCC++ BASE STATION and DCC++ CONTROLLER do heavily rely on the IDEs and
embedded libraries associated with Arduino and Processing.  Tremendous thanks to those
responsible for these terrific open-source initiatives that enable programs like
DCC++ to be developed and distributed in the same fashion.

REFERENCES:

  NMRA DCC Standards:          http://www.nmra.org/index-nmra-standards-and-recommended-practices
  Arduino:                     http://www.arduino.cc/
  Processing:                  http://processing.org/
  GNU General Public License:  http://opensource.org/licenses/GPL-3.0

BRIEF NOTES ON THE THEORY AND OPERATION OF DCC++ BASE STATION:

DCC++ BASE STATION for the Uno configures the OC0B interrupt pin associated with Timer 0,
and the OC1B interupt pin associated with Timer 1, to generate separate 0-5V
unipolar signals that each properly encode zero and one bits conforming with
DCC timing standards.  When compiled for the Mega, DCC++ BASE STATION uses OC3B instead of OC0B.

Series of DCC bit streams are bundled into Packets that each form the basis of
a standard DCC instruction.  Packets are stored in Packet Registers that contain
methods for updating and queuing according to text commands sent by the user
(or another program) over the serial interface.  There is one set of registers that controls
the main operations track and one that controls the programming track.

For the main operations track, packets to store cab throttle settings are stored in
registers numbered 1 through MAX_MAIN_REGISTERS (as defined in DCCpp_Uno.h).
It is generally considered good practice to continuously send throttle control packets
to every cab so that if an engine should momentarily lose electrical connectivity with the tracks,
it will very quickly receive another throttle control signal as soon as connectivity is
restored (such as when a trin passes over  rough portion of track or the frog of a turnout).

DCC++ Base Station therefore sequentially loops through each main operations track packet regsiter
that has been loaded with a throttle control setting for a given cab.  For each register, it
transmits the appropriate DCC packet bits to the track, then moves onto the next register
without any pausing to ensure continuous bi-polar power is being provided to the tracks.
Updates to the throttle setting stored in any given packet register are done in a double-buffered
fashion and the sequencer is pointed to that register immediately after being changes so that updated DCC bits
can be transmitted to the appropriate cab without delay or any interruption in the bi-polar power signal.
The cabs identified in each stored throttle setting should be unique across registers.  If two registers
contain throttle setting for the same cab, the throttle in the engine will oscillate between the two,
which is probably not a desireable outcome.

For both the main operations track and the programming track there is also a special packet register with id=0
that is used to store all other DCC packets that do not require continious transmittal to the tracks.
This includes DCC packets to control decoder functions, set accessory decoders, and read and write Configuration Variables.
It is common practice that transmittal of these one-time packets is usually repeated a few times to ensure
proper receipt by the receiving decoder.  DCC decoders are designed to listen for repeats of the same packet
and provided there are no other packets received in between the repeats, the DCC decoder will not repeat the action itself.
Some DCC decoders actually require receipt of sequential multiple identical one-time packets as a way of
verifying proper transmittal before acting on the instructions contained in those packets

An Arduino Motor Shield (or similar), powered by a standard 15V DC power supply and attached
on top of the Arduino Uno or Mega, is used to transform the 0-5V DCC logic signals
produced by the Uno's Timer interrupts into proper 0-15V bi-polar DCC signals.

This is accomplished on the Uno by using one small jumper wire to connect the Uno's OC1B output (pin 10)
to the Motor Shield's DIRECTION A input (pin 12), and another small jumper wire to connect
the Uno's OC0B output (pin 5) to the Motor Shield's DIRECTION B input (pin 13).

For the Mega, the OC1B output is produced directly on pin 12, so no jumper is needed to connect to the
Motor Shield's DIRECTION A input.  However, one small jumper wire is needed to connect the Mega's OC3B output (pin 2)
to the Motor Shield's DIRECTION B input (pin 13).

Other Motor Shields may require different sets of jumper or configurations (see Config.h and DCCpp_Uno.h for details).

When configured as such, the CHANNEL A and CHANNEL B outputs of the Motor Shield may be
connected directly to the tracks.  This software assumes CHANNEL A is connected
to the Main Operations Track, and CHANNEL B is connected to the Programming Track.

DCC++ BASE STATION in split into multiple modules, each with its own header file:

  DCCpp_Uno:        declares required global objects and contains initial Arduino setup()
                    and Arduino loop() functions, as well as interrupt code for OC0B and OC1B.
                    Also includes declarations of optional array of Turn-Outs and optional array of Sensors

  SerialCommand:    contains methods to read and interpret text commands from the serial line,
                    process those instructions, and, if necessary call appropriate Packet RegisterList methods
                    to update either the Main Track or Programming Track Packet Registers

  PacketRegister:   contains methods to load, store, and update Packet Registers with DCC instructions

  CurrentMonitor:   contains methods to separately monitor and report the current drawn from CHANNEL A and
                    CHANNEL B of the Arduino Motor Shield's, and shut down power if a short-circuit overload
                    is detected

  Accessories:      contains methods to operate and store the status of any optionally-defined turnouts controlled
                    by a DCC stationary accessory decoder.

  Sensor:           contains methods to monitor and report on the status of optionally-defined infrared
                    sensors embedded in the Main Track and connected to various pins on the Arudino Uno

  Outputs:          contains methods to configure one or more Arduino pins as an output for your own custom use

  EEStore:          contains methods to store, update, and load various DCC settings and status
                    (e.g. the states of all defined turnouts) in the EEPROM for recall after power-up

DCC++ BASE STATION is configured through the Config.h file that contains all user-definable parameters

**********************************************************************/

// BEGIN BY INCLUDING THE HEADER FILES FOR EACH MODULE

#include "DCCpp.h"
#include "PacketRegister.h"
#include "CurrentMonitor.h"
#include "Sensor.h"
#include "SerialCommand.h"
#include "Accessories.h"
#include "EEStore.h"
#include "Config.h"
#include "CommInterface.h"
#include "CommInterfaceSerial.h"
#include "CommInterfaceESP.h"
#include "CommInterfaceEthernet.h"
#include "GenerateDCC.h"

#ifdef ENABLE_LCD
bool lcdEnabled = false;
LiquidCrystal_PCF8574 lcdDisplay(LCD_ADDRESS);
#endif

void showConfiguration();

// NEXT DECLARE GLOBAL OBJECTS TO PROCESS AND STORE DCC PACKETS AND MONITOR TRACK CURRENTS.
// NOTE REGISTER LISTS MUST BE DECLARED WITH "VOLATILE" QUALIFIER TO ENSURE THEY ARE PROPERLY UPDATED BY INTERRUPT ROUTINES

volatile RegisterList mainRegs(MAX_MAIN_REGISTERS);    // create list of registers for MAX_MAIN_REGISTER Main Track Packets
volatile RegisterList progRegs(2);                     // create a shorter list of only two registers for Program Track Packets

///////////////////////////////////////////////////////////////////////////////
// MAIN ARDUINO LOOP
///////////////////////////////////////////////////////////////////////////////

void loop(){
  CommManager::update();      // check for and process any new commands
  MotorBoardManager::check();
  Sensor::check();    // check sensors for activate/de-activate
  GenerateDCC::loop();
} // loop

///////////////////////////////////////////////////////////////////////////////
// INITIAL SETUP
///////////////////////////////////////////////////////////////////////////////

void setup(){

#ifdef ENABLE_LCD
  Wire.begin();
  // Check that we can find the LCD by its address before attempting to use it.
  Wire.beginTransmission(LCD_ADDRESS);
  if(Wire.endTransmission() == 0) {
    lcdEnabled = true;
    lcdDisplay.begin(LCD_COLUMNS, LCD_LINES);
    lcdDisplay.setBacklight(255);
    lcdDisplay.clear();
    lcdDisplay.setCursor(0, 0);
    lcdDisplay.print("DCC++ v");
    lcdDisplay.print(VERSION);
    lcdDisplay.setCursor(0, 1);
    #if COMM_INTERFACE >= 1
    lcdDisplay.print("IP: PENDING");
    #else
    lcdDisplay.print("SERIAL: READY");
    #endif
    #if LCD_LINES > 2
      lcdDisplay.setCursor(0, 3);
      lcdDisplay.print("TRACK POWER: OFF");
    #endif
  }
#endif

#if MOTOR_SHIELD_TYPE == 0
  MotorBoardManager::registerBoard(CURRENT_MONITOR_PIN_MAIN, SIGNAL_ENABLE_PIN_MAIN, MOTOR_BOARD_TYPE::ARDUINO_SHIELD, "MAIN");
  MotorBoardManager::registerBoard(CURRENT_MONITOR_PIN_PROG, SIGNAL_ENABLE_PIN_PROG, MOTOR_BOARD_TYPE::ARDUINO_SHIELD, "PROG");
#elif MOTOR_SHIELD_TYPE == 1
  MotorBoardManager::registerBoard(CURRENT_MONITOR_PIN_MAIN, SIGNAL_ENABLE_PIN_MAIN, MOTOR_BOARD_TYPE::POLOLU, "MAIN");
  MotorBoardManager::registerBoard(CURRENT_MONITOR_PIN_PROG, SIGNAL_ENABLE_PIN_PROG, MOTOR_BOARD_TYPE::POLOLU, "PROG");
#elif MOTOR_SHIELD_TYPE == 2
  MotorBoardManager::registerBoard(CURRENT_MONITOR_PIN_MAIN, SIGNAL_ENABLE_PIN_MAIN, MOTOR_BOARD_TYPE::BTS7960B_5A, "MAIN");
  MotorBoardManager::registerBoard(CURRENT_MONITOR_PIN_PROG, SIGNAL_ENABLE_PIN_PROG, MOTOR_BOARD_TYPE::BTS7960B_5A, "PROG");
#endif

#if COMM_INTERFACE != 4 || (COMM_INTERFACE == 4 && !defined(USE_SERIAL_FOR_WIFI))
  CommManager::registerInterface(new HardwareSerialInterface(Serial));
#endif
#if COMM_INTERFACE >= 1 && COMM_INTERFACE <= 3
  CommManager::registerInterface(new EthernetInterface());
#elif COMM_INTERFACE == 4
  #if defined(WIFI_SERIAL_RX) && defined(WIFI_SERIAL_TX)
    CommManager::registerInterface(new ESPSoftwareSerialInterface(WIFI_SERIAL_RX, WIFI_SERIAL_TX));
  #elif defined(USE_SERIAL_FOR_WIFI)
    CommManager::registerInterface(new ESPHardwareSerialInterface(Serial));
  #else
    CommManager::registerInterface(new ESPHardwareSerialInterface(Serial1));
  #endif
#endif

  EEStore::init();                                         // initialize and load Turnout and Sensor definitions stored in EEPROM

  #ifndef ARDUINO_ARCH_ESP32
  pinMode(A5,INPUT);                                       // if pin A5 is grounded upon start-up, print system configuration and halt
  digitalWrite(A5,HIGH);
  if(!digitalRead(A5))
    showConfiguration();
    #endif

  CommManager::printf("<iDCC++ BASE STATION FOR ARDUINO %s / %s: V-%s / %s %s>", ARDUINO_TYPE, MOTOR_SHIELD_NAME, VERSION, __DATE__, __TIME__);

  SerialCommand::init(&mainRegs, &progRegs);   // create structure to read and parse commands from serial line

  CommManager::showInitInfo();
  GenerateDCC::setup();
} // setup

///////////////////////////////////////////////////////////////////////////////
// PRINT CONFIGURATION INFO TO SERIAL PORT REGARDLESS OF INTERFACE TYPE
// - ACTIVATED ON STARTUP IF SHOW_CONFIG_PIN IS TIED HIGH

void showConfiguration(){
  Serial.print("\n*** DCC++ CONFIGURATION ***\n");

  Serial.print("\nVERSION:      ");
  Serial.print(VERSION);
  Serial.print("\nCOMPILED:     ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.print(__TIME__);

  Serial.print("\nARDUINO:      ");
  Serial.print(ARDUINO_TYPE);

  Serial.print("\n\nMOTOR SHIELD: ");
  Serial.print(MOTOR_SHIELD_NAME);

  Serial.print("\n\nDCC SIG MAIN: ");
  Serial.print(DCC_SIGNAL_PIN_MAIN);
  Serial.print("\n   DIRECTION: ");
  Serial.print(DIRECTION_MOTOR_CHANNEL_PIN_A);
  Serial.print("\n      ENABLE: ");
  Serial.print(SIGNAL_ENABLE_PIN_MAIN);
  Serial.print("\n     CURRENT: ");
  Serial.print(CURRENT_MONITOR_PIN_MAIN);

  Serial.print("\n\nDCC SIG PROG: ");
  Serial.print(DCC_SIGNAL_PIN_PROG);
  Serial.print("\n   DIRECTION: ");
  Serial.print(DIRECTION_MOTOR_CHANNEL_PIN_B);
  Serial.print("\n      ENABLE: ");
  Serial.print(SIGNAL_ENABLE_PIN_PROG);
  Serial.print("\n     CURRENT: ");
  Serial.print(CURRENT_MONITOR_PIN_PROG);

  Serial.print("\n\nNUM TURNOUTS: ");
  Serial.print(EEStore::eeStore->data.nTurnouts);
  Serial.print("\n     SENSORS: ");
  Serial.print(EEStore::eeStore->data.nSensors);
  Serial.print("\n     OUTPUTS: ");
  Serial.print(EEStore::eeStore->data.nOutputs);

  Serial.print("\n\nINTERFACE(s):\n");
  CommManager::showConfiguration();
  Serial.print("\n\nPROGRAM HALTED - PLEASE RESTART ARDUINO");
  while(true);
}

///////////////////////////////////////////////////////////////////////////////
