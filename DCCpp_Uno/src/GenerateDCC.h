#ifndef GENERATE_DCC_H
#define GENERATE_DCC_H
//These are defined either in GenerateDCC_avr or GenerateDCC_esp32.cpp
//depending on USE_DCC_GENERATOR_AVR or USE_DCC_GENERATOR_ESP32 (from Config.h)
namespace GenerateDCC{
    void setup();
    void loop();
}
#endif /* end of include guard: GENERATE_DCC_H */
