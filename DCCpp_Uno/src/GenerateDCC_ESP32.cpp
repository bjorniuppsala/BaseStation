#include "DCCpp.h"
#include "GenerateDCC.h"
#if USE_DCC_GENERATOR_ESP32
#include "CommInterface.h"
#include "PacketRegister.h"
#include <array>
#include <atomic>
#include <Arduino.h>
#include "soc/timer_group_struct.h"
#include "soc/gpio_struct.h"

constexpr uint16_t TIMER_DIVISOR = 80;  //1MHz (1us) timer count
constexpr uint64_t ZERO_PERIOD = 100;    // 58us
constexpr uint64_t ONE_PERIOD = 58;
// each half-pulse should be 58us for a one and 100us for a zero

namespace GenerateDCC{
    namespace {
		constexpr size_t tx_buf_size = 16;
		auto nextPos(uint8_t pos) { return (pos + 1) % tx_buf_size; }
        struct Esp32Gen {
            hw_timer_t* timerMid, *timerFull;
			RegisterList volatile* registers;

			uint8_t bitBuffer, remainingBits;

			volatile uint8_t readPos, writePos;
			volatile uint8_t buffer[tx_buf_size];

            template<int timerId>
            void setupTimers();
			void fillBuffer();
        };

        Esp32Gen generators[2];
        volatile int isr_count = 0;

        inline auto pinForTimer(int timerId)
        { return timerId == 0? DCC_SIGNAL_PIN_MAIN : DCC_SIGNAL_PIN_PROG; }

		inline void IRAM_ATTR setAlarm(timg_dev_t& tg, unsigned i, uint64_t period)
		{
			tg.hw_timer[i].alarm_high = (uint32_t)(period >> 32);
			tg.hw_timer[i].alarm_low = (uint32_t)(period & 0xffffffff);
		}
		inline void IRAM_ATTR setTimers(timg_dev_t& tg, uint64_t period)
		{
			setAlarm(tg, 0, period * 2);
			setAlarm(tg, 1, period);
			tg.hw_timer[1].load_high = 0;
			tg.hw_timer[1].load_low = 0;
			tg.hw_timer[1].reload = 1;
			tg.hw_timer[1].config.alarm_en = 1;
		}

		inline void writeTimerPin(int timerId, int value)
		{
			auto pin = pinForTimer(timerId);
			if(pin < 32) {
				if(value)
					GPIO.out_w1ts |= 1 << pin;
				else
					GPIO.out_w1tc |= 1 << pin;
			} else if(pin < 40) {
				if(value)
					GPIO.out1_w1ts.data |= (1 << (pin - 32));
				else
					GPIO.out1_w1tc.data |= (1 << (pin - 32));
			}
		}

        template<int timerId>
        void IRAM_ATTR timerISR_full(void)
        {
            auto& gen = generators[timerId];
			if(gen.remainingBits >= 8) {
				if(gen.readPos != gen.writePos)
				{
					gen.bitBuffer = gen.buffer[gen.readPos];
					gen.readPos = nextPos(gen.readPos);
				} else {
					gen.bitBuffer = 0;
				}
				gen.remainingBits= 0;
			}
			auto thisBit = gen.bitBuffer & RegisterList::bitMask[gen.remainingBits];
			gen.remainingBits++;
            auto period = thisBit ? ONE_PERIOD : ZERO_PERIOD;
            /*we cant use the timer* functions since they are not IRAM_ATTR
			timerAlarmWrite(gen.timerMid, period, false);
            timerAlarmWrite(gen.timerFull, period * 2, true);*/
			auto& timergroup = timerId == 0 ? TIMERG0 : TIMERG1;
			setTimers(timergroup, period);
            digitalWrite(pinForTimer(timerId), HIGH);
        }
        template<int timerId>
        void IRAM_ATTR timerISR_mid()
        {
            ++isr_count;
            digitalWrite(pinForTimer(timerId), LOW);
        }
		// gcc ignores section attr on template functions, but not on template instantiations...
		template [[gnu::section(".iram1")]] void timerISR_mid<0>();
		template [[gnu::section(".iram1")]] void timerISR_mid<1>();
		template [[gnu::section(".iram1")]] void timerISR_full<0>();
		template [[gnu::section(".iram1")]] void timerISR_full<1>();

        template<int timerId>
        void Esp32Gen::setupTimers()
        {
			remainingBits = 0;
			registers = timerId == 0 ? &mainRegs : &progRegs;
			memset(const_cast<uint8_t*>(buffer), sizeof(buffer), 0);
			readPos = 0;
			writePos = tx_buf_size - 1;
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
		void Esp32Gen::fillBuffer()
		{
			//Serial.printf("fillBuffer: read: %d write: %d\n", readPos, writePos);
			decltype(writePos) nextWrite;
			while((nextWrite = nextPos(writePos)) != readPos) {
				buffer[nextWrite] = registers->NextByte();
				writePos = nextWrite;
			}
		}
    }
    void setup()
    {
        mainRegs.loadPacket(1,RegisterList::idlePacket,2,0);
        progRegs.loadPacket(1,RegisterList::idlePacket,2,0);    // load idle packet into register 1

//        pinMode(DIRECTION_MOTOR_CHANNEL_PIN_A,INPUT);      // ensure this pin is not active! Direction will be controlled by DCC SIGNAL instead (below)
//        digitalWrite(DIRECTION_MOTOR_CHANNEL_PIN_A,LOW);
        pinMode(DCC_SIGNAL_PIN_MAIN, OUTPUT);      // THIS ARDUINO OUPUT PIN MUST BE PHYSICALLY CONNECTED TO THE PIN FOR DIRECTION-A OF MOTOR CHANNEL-A
//        pinMode(DIRECTION_MOTOR_CHANNEL_PIN_B,INPUT);      // ensure this pin is not active! Direction will be controlled by DCC SIGNAL instead (below)
//        digitalWrite(DIRECTION_MOTOR_CHANNEL_PIN_B,LOW);
        pinMode(DCC_SIGNAL_PIN_PROG, OUTPUT);      // THIS ARDUINO OUTPUT PIN MUST BE PHYSICALLY CONNECTED TO THE PIN FOR DIRECTION-B OF MOTOR CHANNEL-B

        generators[0].setupTimers<0>();
        generators[1].setupTimers<1>();
    }

    void loop()
    {
		for(int i = 0; i < 2; ++i)
			generators[i].fillBuffer();
        /*auto micros = timerRead(generators[1].timerMid);
        auto config = timerGetConfig(generators[1].timerMid);
        auto int_status = TIMERG0.int_st_timers.val;
        CommManager::printf("timer read: %" PRIu64" confgi 0x%x int_Status = 0x%x isr_count = %d\n", micros, config, int_status,  isr_count);
        */
    }
}

#endif
