#include "Config.h"
#if COMM_INTERFACE == 5
#include "CommInterfaceLocalWeb.h"
#include "SerialCommand.h"
#include <Arduino.h>
#include <Stream.h>
#include <StreamString.h>
#include "AsyncWebSocket.h"
#include "../../DCCpp_ESP/src/config.h"
#include "../../DCCpp_ESP/src/DCCpp_ESP.h"
#include "../../DCCpp_ESP/src/Output.h"
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
#include "../../DCCpp_ESP/src/CabCache.cpp"

namespace {
	StreamString write_to_server, read_from_server;
}
LocalWebInterface::LocalWebInterface()
{
	DCCpp::Server::setup(write_to_server, read_from_server);
	send("<iESP-connect 21 TN_private_W9V7VU_Ext W49K4XWN4344J>\n");
//		"<iESP-start>\n");*/
	send("<iESP-softAP trains>\n");
}
void LocalWebInterface::process()
{
	DCCpp::Server::loop();
	while(read_from_server.available()) {
		auto ch = read_from_server.read();
		if(ch == -1)
			break;
		if (ch == '<') {
			inCommandPayload = true;
			buffer = "";
		} else if (ch == '>') {
			if(buffer.startsWith("iESP AP connected")) {
				Serial.printf("YAy have connection, starting!\n");
				send("<iESP-start>\n");
			} else
				SerialCommand::parse(buffer.c_str());
			buffer = "";
			inCommandPayload = false;
      break;
		} else if(inCommandPayload) {
			buffer += (char)ch;
		}
	}
}
void LocalWebInterface::showConfiguration()
{
	Serial.print("LocalWebInterface\n");
}
void LocalWebInterface::showInitInfo()
{
	auto localAddress = WiFi.localIP();
	CommManager::printf("<N1: %d.%d.%d.%d>", localAddress[0], localAddress[1], localAddress[2], localAddress[3]);
}
void LocalWebInterface::send(const char *buf)
{
	write_to_server.print(buf);
}

#endif
