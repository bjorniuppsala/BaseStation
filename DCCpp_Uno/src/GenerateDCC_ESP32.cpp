#include "DCCpp.h"
#include "GenerateDCC.h"
#if USE_DCC_GENERATOR_ESP32
#include "CommInterface.h"
#include "PacketRegister.h"
#include <array>
#include <atomic>
#include <Arduino.h>

#include <driver/rmt.h>

constexpr uint16_t TIMER_DIVISOR = 250;  //1MHz (1us) timer count
constexpr uint16_t ZERO_DURATION = 58;    // 58us
constexpr uint16_t ONE_DURATION = 100;
// each half-pulse should be 58us for a one and 100us for a zero

namespace GenerateDCC{
    namespace {
		#define constexpr
		constexpr rmt_item32_t createBit(uint16_t duration)
		{
			rmt_item32_t item;
			item.duration0 = item.duration1 = duration;
			item.level0 = 1; item.level1 = 0;
			return item;
		}
		constexpr rmt_item32_t zero_bit = createBit(ZERO_DURATION);
		constexpr rmt_item32_t one_bit = createBit(ONE_DURATION);
		constexpr uint16_t tx_buf_size = 128;
		struct dcc_generator_t {
			uint16_t tx_buf_fillstart;   // the index i which we should starat filling. index into rmt_item32_t i.e. one bit
			uint16_t tx_buf_intr_threshold; // the index at which the interrupt (wil) happen
			uint16_t last_tx_intr_handled;
		    rmt_channel_t channel;
			RegisterList volatile* packets;
			/*std::atomic<uint16_t>*/ uint16_t tx_intr_count;
		};

		dcc_generator_t p_rmt_obj[RMT_CHANNEL_MAX] = {};
		rmt_isr_handle_t s_dcc_driver_intr_handle;
		inline auto moveBufIndex(uint16_t src, uint16_t steps)
		{
			return (src + steps) % tx_buf_size;
		}
		inline void fill_with_bits(dcc_generator_t* p_rmt, uint16_t start, uint16_t end, rmt_item32_t bit)
		{
			for(auto i = start; i < end; i++) {
		        const_cast<rmt_item32_t&>(RMTMEM.chan[p_rmt->channel].data32[i]) = bit;
		    }
		}

		void IRAM_ATTR dcc_gen_rmt_isr(void* arg)
		{
		    uint32_t intr_st = RMT.int_st.val;
		    uint32_t i = 0;
		    uint8_t channel;
		    portBASE_TYPE HPTaskAwoken = 0;
		    for(i = 0; i < 32; i++) {
		        if(i < 24) {
		            if(intr_st & BIT(i)) {
		                channel = i / 3;
		                dcc_generator_t* p_rmt =&p_rmt_obj[channel];
		                switch(i % 3) {
		                    //TX END
		                    case 0:
								p_rmt->tx_intr_count++;
		                        break;
		                        //RX_END
		                    case 1:
		                        break;
		                        //ERR
		                    case 2:
		                        RMT.int_ena.val &= (~(BIT(i)));
		                        break;
		                    default:
		                        break;
		                }
		                RMT.int_clr.val = BIT(i);
		            }
		        } else {
		            if(intr_st & (BIT(i))) {
		                channel = i - 24;
		                RMT.int_clr.val = BIT(i);
						dcc_generator_t* p_rmt = &p_rmt_obj[channel];
						p_rmt->tx_intr_count++;
		            }
		        }
		    }
		}

		void do_fill_buffer(dcc_generator_t* p_rmt, uint16_t start, uint16_t end)
		{
			auto* data32 = const_cast<rmt_item32_t*>(RMTMEM.chan[p_rmt->channel].data32);
			for(auto p = start; p < end; ++p)
			{
				data32[p] = p_rmt->packets->NextBit() ? one_bit :zero_bit;
			}
		}
		void try_fill_buffer(dcc_generator_t* p_rmt)
		{
			uint32_t status = 0;
			rmt_get_status(p_rmt->channel, &status);
			Serial.printf("try_fill: intr_count %d last %d status: 0x%x apb addr: 0x%x data_ch 0x%x\n",
				p_rmt->tx_intr_count, p_rmt->last_tx_intr_handled,
				status, RMT.apb_mem_addr_ch[p_rmt->channel], RMT.data_ch[p_rmt->channel]);
			if(p_rmt->tx_intr_count == p_rmt->last_tx_intr_handled)
				return;
			p_rmt->last_tx_intr_handled = p_rmt->tx_intr_count;

			auto fill_end = p_rmt->tx_buf_intr_threshold;
			auto to_fill = (fill_end- p_rmt->tx_buf_fillstart) % tx_buf_size;
			p_rmt->tx_buf_intr_threshold = moveBufIndex(fill_end, to_fill);
			Serial.printf("Filling: fill_start %d, fill_end %d, next thresh %d\n", p_rmt->tx_buf_fillstart, fill_end, p_rmt->tx_buf_intr_threshold);
			if(fill_end < p_rmt->tx_buf_fillstart)
			{ //fill up to end of block:
				do_fill_buffer(p_rmt, p_rmt->tx_buf_fillstart, tx_buf_size);
				p_rmt->tx_buf_fillstart = 0;
			}
			do_fill_buffer(p_rmt, p_rmt->tx_buf_fillstart, fill_end);
			Serial.printf("Filling:Buffer done.\n");
			p_rmt->tx_buf_fillstart = fill_end;
			//rmt_set_tx_thr_intr_en(p_rmt->channel, true, p_rmt->tx_buf_intr_threshold);
			RMT.tx_lim_ch[p_rmt->channel].limit = p_rmt->tx_buf_intr_threshold;
			Serial.printf("Filling:Done\n");
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

		p_rmt_obj[0].packets = &mainRegs;
		p_rmt_obj[1].packets = &progRegs;

		rmt_config_t c1_config = {
			/*.rmt_mode =*/ RMT_MODE_TX,
			/*.channel = */RMT_CHANNEL_0,
			/*.clk_div = */TIMER_DIVISOR,
			/*.gpio_num = */(gpio_num_t)2,
			/*.mem_block_num = */(uint8_t)(tx_buf_size / 64), // have one rtmem block of 64 32 bit words. (space for 128 bits)
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

		rmt_config(&c1_config);
		auto intr_alloc_flags = ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_LEVEL3;
		auto err = rmt_isr_register(dcc_gen_rmt_isr, NULL, intr_alloc_flags, &s_dcc_driver_intr_handle);
		if(err != ESP_OK) Serial.printf("Failed to install isr: %d\n", err);

		for(int i = 0; i < 1; ++i)
		{
			auto& g = p_rmt_obj[i];
			g.tx_buf_fillstart = 0;
			g.tx_buf_intr_threshold = 32;
			g.tx_intr_count = 0;
			g.last_tx_intr_handled =  0;
			g.channel = (rmt_channel_t)i;
			RMT.apb_conf.fifo_mask = RMT_DATA_MODE_MEM;
			do_fill_buffer(&g, 0, tx_buf_size);
			rmt_set_tx_thr_intr_en(g.channel, true, g.tx_buf_intr_threshold);
			err = rmt_set_tx_intr_en(g.channel, 1);
			if(err != ESP_OK) Serial.printf("Failed to rmt_set_tx_intr_en: %d\n", err);
			err = rmt_set_tx_thr_intr_en(g.channel, 1, g.tx_buf_intr_threshold
			);
			if(err != ESP_OK) Serial.printf("Failed to rmt_set_tx_thr_intr_en: %d\n", err);
			rmt_tx_start(g.channel, true);
		}
    }
    void loop()
    {
		try_fill_buffer(&p_rmt_obj[0]);
	}

}

#endif
