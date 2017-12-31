#include "DCCpp.h"
#include "GenerateDCC.h"
#if USE_DCC_GENERATOR_ESP32
#include "CommInterface.h"
#include "PacketRegister.h"
#include <array>
#include <atomic>
#include <Arduino.h>
#include <driver/rmt.h>
constexpr uint16_t TIMER_DIVISOR = 80;  //1MHz (1us) timer count
constexpr uint64_t ZERO_PERIOD = 100;    // 58us
constexpr uint64_t ONE_PERIOD = 58;

// each half-pulse should be 58us for a one and 100us for a zero

namespace GenerateDCC{
    namespace {
		constexpr rmt_item32_t createBit(uint32_t duration)
		{
			rmt_item32_t item ;//{ duration, duration, 1, 0};
			item.val = (duration << 16) | duration | 1 << 15;
			return item;
		}
		constexpr rmt_item32_t zero_bit = createBit(ZERO_PERIOD);
		constexpr rmt_item32_t one_bit = createBit(ONE_PERIOD);
		constexpr uint16_t tx_buf_size = 128;
		struct dcc_generator_t {
		    rmt_channel_t channel;
			RegisterList volatile* packets;
			/*std::atomic<uint16_t>*/ uint16_t tx_intr_count;
		};

		dcc_generator_t p_rmt_obj[RMT_CHANNEL_MAX] = {};


		void fillRMTTask(void* arg)
		{
			auto gen = reinterpret_cast<dcc_generator_t*>(arg);
			rmt_item32_t bits_to_send[128];
			std::fill(std::begin(bits_to_send), std::end(bits_to_send), zero_bit);
			auto n_bits = 128;
			while(1) { // This may look backwards, but by sending, setting up the next
				// and then waiting for tx to be done we improve timing a little bit.
				//Remember that the signal stays still while we are not sending...
				auto err = rmt_write_items(gen->channel, bits_to_send, n_bits, false);
				if(err != ESP_OK) {
					Serial.printf("rmt_write_items failed: %d channel: %d\n", err, gen->channel);
				}
				auto p = gen->packets->NextPacket();
				//Serial.printf("DCC: %d nbits %d\n", gen->channel, p->nBits);
				if(p->nBits > 120) continue; // this will repeat the same packet again...
				for(int b = 0; b < p->nBits; ++b) {
					auto v = p->buf[b/8] & RegisterList::bitMask[b%8] ;
					bits_to_send[b] = v ? one_bit : zero_bit;
				}
				bits_to_send[p->nBits] = one_bit; // push an extra on_bit to ensure that the packet finishs nicely.
				n_bits = p->nBits + 1;
				err = rmt_wait_tx_done(gen->channel, portMAX_DELAY);
				if(err != ESP_OK) {
					Serial.printf("rmt_wait_tx_done failed: %d channel: %d\n", err, gen->channel);
				}
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

		p_rmt_obj[0].packets = &mainRegs;
		p_rmt_obj[1].packets = &progRegs;

		for(int c = 0; c < 2; ++c) {
			rmt_config_t c1_config = {
				/*.rmt_mode =*/ RMT_MODE_TX,
				/*.channel = */c == 0 ? RMT_CHANNEL_0 : RMT_CHANNEL_4,
				/*.clk_div = */TIMER_DIVISOR,
				/*.gpio_num = */(gpio_num_t)(c == 0 ? DCC_SIGNAL_PIN_MAIN : DCC_SIGNAL_PIN_PROG),
				/*.mem_block_num = */(uint8_t)(2), // have two rtmem block of 64 32 bit words. (space for 128 bits)
				/*.tx_config =*/ {
					/*.loop_en =*/ false,          /*!< RMT loop output mode*/
	    			/*.carrier_freq_hz =*/ 0,      /*!< RMT carrier frequency */
	    			/*.carrier_duty_percent =*/ 0, /* RMT carrier duty (%) */
	     			/*.carrier_level =*/ RMT_CARRIER_LEVEL_HIGH,  /*!< RMT carrier level */
	    			/*.carrier_en =*/ false,       /*!< RMT carrier enable */
	    			/*.idle_level = */RMT_IDLE_LEVEL_LOW, /*!< RMT idle level */
					/*.idle_output_en = */false
				}
			};
			p_rmt_obj[c].channel = c1_config.channel;
			rmt_config(&c1_config);
			auto intr_alloc_flags = ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_LEVEL3;
			auto err = rmt_driver_install(c1_config.channel, 0, 0);
			if(err != ESP_OK) Serial.printf("Failed to install driver: %d\n", err);
			char tn[] = "FILLRMT0";
			tn[8] += c;
			// spawn a task
			err = xTaskCreate(    &fillRMTTask,
	                            tn,
								5* 1024,
	                            p_rmt_obj + c,
	                            1, //UBaseType_t uxPriority,
	                            nullptr//TaskHandle_t *pxCreatedTask
	                          );

			if(err != pdPASS) Serial.printf("Failed to start task driver: %d\n", err);
		}
    }
    void loop()
    {
	//	try_fill_buffer(&p_rmt_obj[0]);
	}

}

#endif
