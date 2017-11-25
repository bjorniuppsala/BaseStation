#pragma once
#include "Stream.h"
namespace DCCpp {
    namespace Server{
        void setup(Stream& read_from_dccpp, Print& write_to_dccpp);
        void loop();
    }
}
