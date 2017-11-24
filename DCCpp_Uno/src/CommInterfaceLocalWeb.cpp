#include "Config.h"
#if COMM_INTERFACE == 5
#include "CommInterfaceLocalWeb.h"
#include <Arduino.h>
#include <Stream.h>
#include <StreamString.h>
#include "AsyncWebSocket.h"
#include "../../DCCpp_ESP/src/config.h"
#include "../../DCCpp_ESP/src/DCCpp_ESP.h"
#include "../../DCCpp_ESP/src/Output.h"
#include "../../DCCpp_ESP/src/PacketRegister.h"
#include "../../DCCpp_ESP/src/PowerDistrict.h"
#include "../../DCCpp_ESP/src/ProgramRequest.h"
#include "../../DCCpp_ESP/src/Queue.h"
#include "../../DCCpp_ESP/src/Sensor.h"

#include "../../DCCpp_ESP/src/DCCpp_ESP.cpp"
#include "../../DCCpp_ESP/src/Output.cpp"
#include "../../DCCpp_ESP/src/PowerDistrict.cpp"
#include "../../DCCpp_ESP/src/ProgramRequest.cpp"
#include "../../DCCpp_ESP/src/Sensor.cpp"
#include "../../DCCpp_ESP/src/Turnout.cpp"

namespace {
}
LocalWebInterface::LocalWebInterface()
{

	StreamString s;
	DCCpp::Server::setup(s);
}
void LocalWebInterface::process()
{}
void LocalWebInterface::showConfiguration()
{}
void LocalWebInterface::showInitInfo()
{}
void LocalWebInterface::send(const char *buf)
{}

#endif
