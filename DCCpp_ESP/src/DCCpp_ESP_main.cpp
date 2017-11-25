#include "DCCpp_ESP.h"
#include "config.h"
#include <Arduino.h>
void setup()
{
	SERIAL_LINK_DEV.begin(SERIAL_LINK_SPEED);
	SERIAL_LINK_DEV.flush();
	DCCpp::Server::setup(SERIAL_LINK_DEV, SERIAL_LINK_DEV);
}

void loop()
{
	DCCpp::Server::loop();
}
