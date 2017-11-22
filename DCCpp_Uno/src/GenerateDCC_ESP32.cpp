#include "DCCpp.h"
#include "GenerateDCC.h"
#if USE_DCC_GENERATOR_ESP32
#include "CommInterface.h"
#include "PacketRegister.h"
#include <array>
#include <atomic>
#include <Arduino.h>
constexpr uint16_t TIMER_DIVISOR = 80;  //1MHz (1us) timer count
constexpr uint64_t ZERO_PERIOD = 58;    // 58us
constexpr uint64_t ONE_PERIOD = 100;
// each half-pulse should be 58us for a one and 100us for a zero

namespace GenerateDCC{
    namespace {
        struct Esp32Gen {
            hw_timer_t* timerMid, *timerFull;

            template<int timerId>
            void setupTimers();
        };

        Esp32Gen generators[2];
        volatile int isr_count = 0;

        inline auto pinForTimer(int timerId)
        { return timerId == 0? DCC_SIGNAL_PIN_MAIN : DCC_SIGNAL_PIN_PROG; }

        template<int timerId>
        void timerISR_full(void)
        {
            auto& gen = generators[timerId];
            auto& packetReg = timerId == 0 ? mainRegs : progRegs;
            auto period = packetReg.NextBit() ? ONE_PERIOD : ZERO_PERIOD;
            timerAlarmWrite(gen.timerMid, period, false);
            timerAlarmWrite(gen.timerFull, period * 2, true);
            timerWrite(gen.timerMid, 0);
            timerAlarmEnable(gen.timerMid);
            digitalWrite(pinForTimer(timerId), HIGH);
        }
        template<int timerId>
        void timerISR_mid()
        {
            ++isr_count;
            digitalWrite(pinForTimer(timerId), LOW);
        }

        template<int timerId>
        void Esp32Gen::setupTimers()
        {
            timerFull = timerBegin(2*timerId, 80, /*countUp*/true);
            timerAttachInterrupt(timerFull, &timerISR_full<timerId>, /*edge*/true);
            timerAlarmWrite(timerFull, 2*ZERO_PERIOD, /*autoreload*/true);
            timerWrite(timerFull, 0);
            timerAlarmEnable(timerFull);

            timerMid = timerBegin(2*timerId+1, 80, /*countUp*/true);
            timerAttachInterrupt(timerMid, &timerISR_mid<timerId>, /*edge*/true);
            timerAlarmWrite(timerMid, ZERO_PERIOD, /*autoreload*/false);
            timerWrite(timerMid, 0);
            timerAlarmEnable(timerMid);
        }
    }
    void setup()
    {
        mainRegs.loadPacket(1,RegisterList::idlePacket,2,0);
        progRegs.loadPacket(1,RegisterList::idlePacket,2,0);    // load idle packet into register 1

        pinMode(DIRECTION_MOTOR_CHANNEL_PIN_A,INPUT);      // ensure this pin is not active! Direction will be controlled by DCC SIGNAL instead (below)
        digitalWrite(DIRECTION_MOTOR_CHANNEL_PIN_A,LOW);
        pinMode(DCC_SIGNAL_PIN_MAIN, OUTPUT);      // THIS ARDUINO OUPUT PIN MUST BE PHYSICALLY CONNECTED TO THE PIN FOR DIRECTION-A OF MOTOR CHANNEL-A
        pinMode(DIRECTION_MOTOR_CHANNEL_PIN_B,INPUT);      // ensure this pin is not active! Direction will be controlled by DCC SIGNAL instead (below)
        digitalWrite(DIRECTION_MOTOR_CHANNEL_PIN_B,LOW);
        pinMode(DCC_SIGNAL_PIN_PROG,OUTPUT);      // THIS ARDUINO OUTPUT PIN MUST BE PHYSICALLY CONNECTED TO THE PIN FOR DIRECTION-B OF MOTOR CHANNEL-B

        generators[0].setupTimers<0>();
        generators[1].setupTimers<1>();
    }

    void loop()
    {
        /*auto micros = timerRead(generators[1].timerMid);
        auto config = timerGetConfig(generators[1].timerMid);
        auto int_status = TIMERG0.int_st_timers.val;
        CommManager::printf("timer read: %" PRIu64" confgi 0x%x int_Status = 0x%x isr_count = %d\n", micros, config, int_status,  isr_count);
        */
    }
}

#endif
