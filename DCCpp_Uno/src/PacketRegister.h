/**********************************************************************

PacketRegister.h
COPYRIGHT (c) 2013-2016 Gregg E. Berman

Part of DCC++ BASE STATION for the Arduino

**********************************************************************/

#ifndef PacketRegister_h
#define PacketRegister_h

#include "Arduino.h"
#include <atomic>

// Define constants used for reading CVs from the Programming Track

#define  ACK_BASE_COUNT            100      // number of analogRead samples to take before each CV verify to establish a baseline current
#define  ACK_SAMPLE_COUNT          2500     // number of analogRead samples to take when monitoring current after a CV verify (bit or byte) has been sent
#define  ACK_SAMPLE_SMOOTHING      0.2      // exponential smoothing to use in processing the analogRead samples after a CV verify (bit or byte) has been sent
#define  ACK_SAMPLE_THRESHOLD       40      // the threshold that the exponentially-smoothed analogRead samples (after subtracting the baseline current) must cross to establish ACKNOWLEDGEMENT

// Define a series of registers that can be sequentially accessed over a loop to generate a repeating series of DCC Packets

struct Packet{
  byte nBits;
  byte buf[10];
  std::atomic<byte> nRepeat;
  Packet(Packet const&) = default;
  Packet(Packet&& other) : nBits{other.nBits}, nRepeat{other.nRepeat.load()} { memcpy(buf, other.buf, sizeof(buf));}
  Packet(byte const* d, byte n, byte r);

  template<size_t n>
  Packet(byte (&data)[n], int r)
  { setup(data, n - 1, r); }

  Packet() {}
  //!Setup the contents of packet, using d as a scratchbuffer must be able to hold n + 1 bytes.
  void setup(byte* d, byte n, byte r);
}; // Packet

struct Register {
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
  Register *nextReg;  // The most recently updated register, to be sent next, once the current packet is sent.
  Packet *tempPacket;  // for the _uno
  std::atomic<Packet*> sequence;   //A sequnece of packets to be sent without other packets interrupting.
  std::atomic<byte>    sequenceLength;
  std::atomic<Packet*> currentPacket; // packet acutally being sent at the moment.
  byte currentBit;
  byte nRepeat;
  int *speedTable;
  static byte idlePacket[3];
  static byte resetPacket[3];
  static byte bitMask[8];
  RegisterList(int);
  void loadPacket(int nReg, byte *b, int nBytes, int nRepeat, int printFlag = 0) volatile;
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

  template<size_t n>
  void scheduleSequence(Packet (&packets)[n]) volatile { return scheduleSequence(packets, n); }
  void scheduleSequence(Packet* packets, size_t n) volatile;
  auto remainingSequenceLength() const volatile { return sequenceLength; }
  void waitForSequence(size_t expectedRemainingLength = 0) volatile const;
  void killSequence();

  auto NextRegister() volatile __attribute__ ((always_inline))
  {
    if(nextReg) {                           /* IF another Register has been updated */
	    auto r = nextReg;                     /*     update currentReg to nextReg */
      nextReg = nullptr;                    /*     reset nextReg to NULL */
      return r;
	  } else {                                  /*   ELSE simply move to next Register */
	    return (currentReg == maxLoadedReg) ? reg : (currentReg + 1);
    }
  }

  auto NextPacket() volatile __attribute__ ((always_inline))
  {
	  auto packet = currentPacket.load();
	  if(auto s = sequence.load()) {
      //Serial.printf("Sequence: 0x%x length: %d repeat: %d\n", (unsigned int)s, sequenceLength.load(), s->nRepeat.load());
      if(s->nRepeat.fetch_sub(1) == 1) {
        auto l = sequenceLength.fetch_sub(1);
        auto s_replaced = s;
        sequence.compare_exchange_strong(s_replaced, (l <= 1) ? nullptr : (s + 1));
      }
      currentPacket = s;
      return s;
    } else if(packet->nRepeat>0 && currentReg == reg) {  /*   IF current Register is first Register AND should be repeated */
  	    packet->nRepeat--;                              /*     decrement repeat count; result is this same Packet will be repeated */
        return packet;
    } else {
      currentReg = NextRegister();
      currentPacket = currentReg->activePacket;
      return currentReg->activePacket;         /*     increment current Register (note this logic causes Register[0] to be skipped when simply cycling through all Registers) */
    }
  }

  inline auto NextBit() volatile __attribute__ ((always_inline))
  {
    auto currBitNo = currentBit;
    auto packet = currentPacket.load();
    if(currBitNo == packet->nBits){    /* IF no more bits in this DCC Packet */
      currentBit = currBitNo = 0;
      packet = currentPacket = NextPacket();
    }
    auto bit = packet->buf[currBitNo/8] & bitMask[currBitNo%8];
    ++currentBit;
    return bit;
  }
};

extern volatile RegisterList mainRegs;    // create list of registers for MAX_MAIN_REGISTER Main Track Packets
extern volatile RegisterList progRegs;

#endif
