#include "DCCpp.h"
#include "GenerateDCC.h"
#if USE_DCC_GENERATOR_ESP32
#include "CommInterface.h"
#include "PacketRegister.h"
#include <array>
#include <atomic>
constexpr uint16_t TIMER_DIVISOR = 80;  //1MHz (1us) timer count
constexpr uint64_t ZERO_PERIOD = 58*100;    // 58us
constexpr uint64_t ONE_PERIOD = 100*100;
// each half-pulse should be 58us for a one and 100us for a zero
//
namespace GenerateDCC{
    namespace {
        struct Esp32Gen {
            hw_timer_t* timerMid, *timerFull;
            RegisterList* registerList;

            template<int timerId>
            void setupTimers();
        };

        Esp32Gen generators[2];

std::atomic<int> isr_count {0};
        template<int timerId>
        void timerISR_full(void)
        {
            auto& gen = generators[timerId];
            auto& packetReg = *gen.registerList;
            if(packetReg.NextBit()){ //high (one) bit
                timerAlarmWrite(gen.timerMid, ONE_PERIOD/2, true);
                timerAlarmWrite(gen.timerFull, ONE_PERIOD, false);
            } else {
                timerAlarmWrite(gen.timerMid, ZERO_PERIOD/2, true);
                timerAlarmWrite(gen.timerFull, ZERO_PERIOD, false);
            }
            digitalWrite(timerId == 0? DCC_SIGNAL_PIN_MAIN : DCC_SIGNAL_PIN_PROG,
                    HIGH);
                    //CommManager::printf("Hi!\n");
                    ++isr_count;
        }
        template<int timerId>
        void timerISR_mid()
        {
            digitalWrite(timerId == 0? DCC_SIGNAL_PIN_MAIN : DCC_SIGNAL_PIN_PROG,
                LOW);
                //CommManager::printf("Ho!\n");
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
            timerAlarmEnable(timerMid);
        }
    }
    void setup()
    {
        mainRegs.loadPacket(1,RegisterList::idlePacket,2,0);
        progRegs.loadPacket(1,RegisterList::idlePacket,2,0);    // load idle packet into register 1

        for(int i = 0; i< 300; ++i)
        {
            CommManager::printf("Bit %d is %d\n", i, mainRegs.NextBit());
        }
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
        auto micros = timerRead(generators[1].timerFull);
        CommManager::printf("timer read: %" PRIu64" isr_count=0x%x\n", micros, isr_count.load());


    }
}

#endif
