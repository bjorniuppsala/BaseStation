/**********************************************************************

PacketRegister.h
COPYRIGHT (c) 2013-2016 Gregg E. Berman

Part of DCC++ BASE STATION for the Arduino

**********************************************************************/

#ifndef PacketRegister_h
#define PacketRegister_h

#include "Arduino.h"

// Define constants used for reading CVs from the Programming Track

#define  ACK_BASE_COUNT            100      // number of analogRead samples to take before each CV verify to establish a baseline current
#define  ACK_SAMPLE_COUNT          500      // number of analogRead samples to take when monitoring current after a CV verify (bit or byte) has been sent
#define  ACK_SAMPLE_SMOOTHING      0.2      // exponential smoothing to use in processing the analogRead samples after a CV verify (bit or byte) has been sent
#define  ACK_SAMPLE_THRESHOLD       20      // the threshold that the exponentially-smoothed analogRead samples (after subtracting the baseline current) must cross to establish ACKNOWLEDGEMENT

// Define a series of registers that can be sequentially accessed over a loop to generate a repeating series of DCC Packets

struct Packet{
  byte buf[10];
  byte nBits;
}; // Packet

struct Register{
  Packet *activePacket; // activePacket at offset 0 saves some instructions in the timer isr.
  Packet *updatePacket;
  Packet packet[2];
  void initPackets();
}; // Register

struct RegisterList{
  int maxNumRegs;
  Register *reg;
  Register **regMap;
  Register *currentReg;
  Register *maxLoadedReg;
  Register *nextReg;
  Packet  *tempPacket;
  byte currentBit;
  byte nRepeat;
  int *speedTable;
  static byte idlePacket[];
  static byte resetPacket[];
  static byte bitMask[];
  RegisterList(int);
  void loadPacket(int, byte *, int, int, int=0) volatile;
  void setThrottle(const char *) volatile;
  void setFunction(const char *) volatile;
  void setAccessory(const char *) volatile;
  void writeTextPacket(const char *) volatile;
  void readCV(const char *) volatile;
  void writeCVByte(const char *) volatile;
  void writeCVBit(const char *) volatile;
  void writeCVByteMain(const char *) volatile;
  void writeCVBitMain(const char *s) volatile;
  void printPacket(int, byte *, int, int) volatile;

  auto NextPacket() volatile
  {
	  auto& R = *this;
	  auto packet = R.currentReg->activePacket;
	  R.currentBit=0;                           /*   reset current bit pointer and determine which Register and Packet to process next--- */
	  if(R.nRepeat>0 && R.currentReg==R.reg){               /*   IF current Register is first Register AND should be repeated */
	    R.nRepeat--;                                        /*     decrement repeat count; result is this same Packet will be repeated */
	  } else if(R.nextReg!=NULL){                           /*   ELSE IF another Register has been updated */ \
	    R.currentReg=R.nextReg;                             /*     update currentReg to nextReg */ \
	    R.nextReg=NULL;                                     /*     reset nextReg to NULL */ \
	    auto tmp =R.currentReg->activePacket;            /*     flip active and update Packets */ \
	    packet = R.currentReg->activePacket = R.currentReg->updatePacket; \
	    R.currentReg->updatePacket=tmp; \
	  } else{                                               /*   ELSE simply move to next Register */ \
	    if(R.currentReg==R.maxLoadedReg)                    /*     BUT IF this is last Register loaded */ \
	  	R.currentReg=R.reg;                               /*       first reset currentReg to base Register, THEN */ \
	    R.currentReg++;
	    packet = R.currentReg->activePacket;/*     increment current Register (note this logic causes Register[0] to be skipped when simply cycling through all Registers) */ \
	  }
	  return packet;
  }

  inline auto NextByte() volatile
  {
	  auto& R = *this;
      auto currBitNo = R.currentBit;
      auto packet = R.currentReg->activePacket;
      if(currBitNo >= packet->nBits){    /* IF no more bits in this DCC Packet */
        currBitNo = R.currentBit=0;                           /*   reset current bit pointer and determine which Register and Packet to process next--- */
        if(R.nRepeat>0 && R.currentReg==R.reg){               /*   IF current Register is first Register AND should be repeated */
          R.nRepeat--;                                        /*     decrement repeat count; result is this same Packet will be repeated */
        } else if(R.nextReg!=NULL){                           /*   ELSE IF another Register has been updated */ \
          R.currentReg=R.nextReg;                             /*     update currentReg to nextReg */ \
          R.nextReg=NULL;                                     /*     reset nextReg to NULL */ \
          auto tmp =R.currentReg->activePacket;            /*     flip active and update Packets */ \
          packet = R.currentReg->activePacket = R.currentReg->updatePacket; \
          R.currentReg->updatePacket=tmp; \
        } else{                                               /*   ELSE simply move to next Register */ \
          if(R.currentReg==R.maxLoadedReg)                    /*     BUT IF this is last Register loaded */ \
            R.currentReg=R.reg;                               /*       first reset currentReg to base Register, THEN */ \
          R.currentReg++;
          packet = R.currentReg->activePacket;/*     increment current Register (note this logic causes Register[0] to be skipped when simply cycling through all Registers) */ \
        }
      }
      auto result = packet->buf[currBitNo/8];
      R.currentBit += 8;
      return result;
  }
  inline auto NextBit() volatile __attribute__ ((always_inline))
  {
      auto& R = *this;
      auto currBitNo = R.currentBit;
      auto packet = R.currentReg->activePacket;
      if(currBitNo==packet->nBits){    /* IF no more bits in this DCC Packet */
        currBitNo = R.currentBit=0;                           /*   reset current bit pointer and determine which Register and Packet to process next--- */
        if(R.nRepeat>0 && R.currentReg==R.reg){               /*   IF current Register is first Register AND should be repeated */
          R.nRepeat--;                                        /*     decrement repeat count; result is this same Packet will be repeated */
        } else if(R.nextReg!=NULL){                           /*   ELSE IF another Register has been updated */ \
          R.currentReg=R.nextReg;                             /*     update currentReg to nextReg */ \
          R.nextReg=NULL;                                     /*     reset nextReg to NULL */ \
          auto tmp =R.currentReg->activePacket;            /*     flip active and update Packets */ \
          packet = R.currentReg->activePacket = R.currentReg->updatePacket; \
          R.currentReg->updatePacket=tmp; \
        } else{                                               /*   ELSE simply move to next Register */ \
          if(R.currentReg==R.maxLoadedReg)                    /*     BUT IF this is last Register loaded */ \
            R.currentReg=R.reg;                               /*       first reset currentReg to base Register, THEN */ \
          R.currentReg++;
          packet = R.currentReg->activePacket;/*     increment current Register (note this logic causes Register[0] to be skipped when simply cycling through all Registers) */ \
        }
      }
      auto bit = packet->buf[currBitNo/8] & R.bitMask[currBitNo%8];
      ++R.currentBit;
      return bit;
  }
};

extern volatile RegisterList mainRegs;    // create list of registers for MAX_MAIN_REGISTER Main Track Packets
extern volatile RegisterList progRegs;

#endif
