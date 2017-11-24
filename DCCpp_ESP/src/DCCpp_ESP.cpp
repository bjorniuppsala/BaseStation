/**********************************************************************
 DCCppESP.ino
 COPYRIGHT (c) 2017 Mike Dunston
 Part of DCC++ BASE STATION for the Arduino / ESP8266
 **********************************************************************/
#ifdef ESP32
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#endif
#include <algorithm>
#include <ArduinoOTA.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <TaskScheduler.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <StringArray.h>
#include "config.h"
#include "DCCpp_ESP.h"
#include "Queue.h"
#include "PowerDistrict.h"
#include "Sensor.h"
#include "Output.h"
#include "Turnout.h"
#include "ProgramRequest.h"

#define UNUSED(x) (void)(x)
namespace {
WiFiServer DCCppServer(DCCPP_CLIENT_PORT);
WiFiClient DCCppClients[MAX_DCCPP_CLIENTS];
AsyncWebServer webServer(80);
AsyncWebSocket webSocket("/ws");

Queue<String> DCCppPendingCommands(64);  // to be sent over SERIAL_LINK§
String currentDCCppCommand = "";

bool inDCCPPCommand = false;

const char * espBuildTime = __DATE__ " " __TIME__ " GMT";

Scheduler taskScheduler;
void powerDistrictStatusCallback();
void sensorStatusCallback();
void outputStatusCallback();
void turnoutStatusCallback();
void programmingTaskCleanupCallback();
Task powerDistrictStatusTask(TASK_SECOND, TASK_FOREVER, &powerDistrictStatusCallback, &taskScheduler);
Task sensorStatusTask(TASK_SECOND, TASK_FOREVER, &sensorStatusCallback, &taskScheduler);
Task outputStatusTask(TASK_SECOND, TASK_FOREVER, &outputStatusCallback, &taskScheduler);
Task turnoutStatusTask(TASK_SECOND, TASK_FOREVER, &turnoutStatusCallback, &taskScheduler);
Task progammingTaskPurge(TASK_MINUTE, TASK_FOREVER, &programmingTaskCleanupCallback, &taskScheduler);

bool forceRefreshSensors = false;

LinkedList<PowerDistrict *> powerDistricts(
		[](PowerDistrict *node) {delete node;});
LinkedList<Sensor *> sensors([](Sensor *node) {delete node;});
LinkedList<Output *> outputs([](Output *node) {delete node;});
LinkedList<Turnout *> turnouts([](Turnout *node) {delete node;});
LinkedList<ProgramRequest *> progRequests(
		[](ProgramRequest *node) {delete node;});

void powerDistrictStatusCallback() {
	if (powerDistrictStatusTask.isFirstIteration()) {
		DCCppPendingCommands.push(F("<p>"));
	} else if (powerDistrictStatusTask.getRunCounter() % 15 == 0) {
		// every 15sec request refresh of power district current status
		for (const auto& node : powerDistricts) {
			DCCppPendingCommands.push(
					String(F("<c ")) + node->getDistrictName()
							+ String(F(">")));
		}
	}
}

void programmingTaskCleanupCallback() {
	// if the request is over 5min drop it.
	Queue<ProgramRequest *> toRemove;
	for (const auto& node : progRequests) {
		if (node->getAge() - millis() > 300000L) {
			toRemove.push(node);
		}
	}
	while(toRemove.count()) {
		progRequests.remove(toRemove.pop());
	}
}

void sensorStatusCallback() {
	if (sensorStatusTask.isFirstIteration() || forceRefreshSensors) {
		// for first iteration (or force refresh) we need to gather what sensors are defined
		// note using S to trigger verbose output rather than Q which doesn't print everything
		DCCppPendingCommands.push(F("<S>"));
		forceRefreshSensors = false;
		sensors.free();
	} else if (sensorStatusTask.getRunCounter() % 30 == 0) {
		// every 30sec request refresh of sensor status
		DCCppPendingCommands.push(F("<Q>"));
	}
}

void outputStatusCallback() {
	if (outputStatusTask.isFirstIteration()
		|| outputStatusTask.getRunCounter() % 30 == 0) {
		// every 30sec request refresh of sensor status
		// note using Z to trigger verbose output rather than Q which doesn't print everything
		DCCppPendingCommands.push(F("<Z>"));
	}
}

void turnoutStatusCallback() {
	if (turnoutStatusTask.isFirstIteration()
			|| turnoutStatusTask.getRunCounter() % 30 == 0) {
		// every 30sec request refresh of turnout status
		DCCppPendingCommands.push(F("<T>"));
	}
}

struct WebSocketClient {
	bool used;
	uint32_t id;
	String partialCommand;
};
WebSocketClient webSocketClients[MAX_WEBSOCKET_CLIENTS];

void onWSEvent(AsyncWebSocket * server, AsyncWebSocketClient * client,
		AwsEventType type, void * arg, uint8_t *data, size_t len) {
	UNUSED(server);
	UNUSED(arg);
	if (type == WS_EVT_CONNECT) {
		int clientIndex = -1;
		for (int index = 0; index < MAX_WEBSOCKET_CLIENTS; index++) {
			if (!webSocketClients[index].used && clientIndex == -1) {
				webSocketClients[index].used = true;
				webSocketClients[index].id = client->id();
				clientIndex = index;
			}
		}
		client->printf("Welcome. Server Ready %u:%u", client->id(),
				clientIndex);
	} else if (type == WS_EVT_DISCONNECT) {
		for (int index = 0; index < MAX_WEBSOCKET_CLIENTS; index++) {
			if (webSocketClients[index].id == client->id()) {
				webSocketClients[index].used = false;
				webSocketClients[index].partialCommand = "";
			}
		}
	} else if (type == WS_EVT_DATA) {
		int clientIndex = 0;
		for (int index = 0; index < MAX_WEBSOCKET_CLIENTS; index++) {
			if (webSocketClients[index].id == client->id()) {
				clientIndex = index;
			}
		}
		String message = String((char *) data);
		message[len] = '\0';
		webSocketClients[clientIndex].partialCommand += message;
		size_t baseOffs = 0;
		for (size_t index = 0;
				index <= webSocketClients[clientIndex].partialCommand.length();
				index++) {
			if (webSocketClients[clientIndex].partialCommand[baseOffs] == '<'
					&& webSocketClients[clientIndex].partialCommand[index]
							== '>') {
				DCCppPendingCommands.push(
						webSocketClients[clientIndex].partialCommand.substring(
								baseOffs, index + 1));
				baseOffs = index + 1;
			}
		}
		webSocketClients[clientIndex].partialCommand.remove(0, baseOffs);
	}
}

void handleNotFound(AsyncWebServerRequest *request) {
	request->send(404, F("text/plain"), F("FileNotFound"));
}

void handleESPInfo(AsyncWebServerRequest *request) {
	auto jsonResponse = new AsyncJsonResponse(true);
	JsonArray& root = jsonResponse->getRoot();
	#ifndef ESP32
	root.createNestedObject()[F("id")] = F("ESP Core");
	root.get<JsonObject&>(0)[F("value")] = String(ESP.getCoreVersion());
	root.createNestedObject()[F("id")] = F("ESP Boot Loader");
	root.get<JsonObject&>(1)[F("value")] = String(ESP.getBootVersion());
	#endif
	root.createNestedObject()[F("id")] = F("ESP SDK");
	root.get<JsonObject&>(2)[F("value")] = String(ESP.getSdkVersion());
	root.createNestedObject()[F("id")] = F("Available Heap Space");
	root.get<JsonObject&>(3)[F("value")] = String(ESP.getFreeHeap());
	#ifndef ESP32
	root.createNestedObject()[F("id")] = F("Available Sketch Space");
	root.get<JsonObject&>(4)[F("value")] = String(ESP.getFreeSketchSpace());
	#endif
	root.createNestedObject()[F("id")] = F("ESP Chip ID");
	root.get<JsonObject&>(5)[F("value")] =
	#ifndef ESP32
	 String(ESP.getChipId(), HEX);
	#else
	String(static_cast<uint32_t>(ESP.getEfuseMac() >> 32), HEX) +
	String(static_cast<uint32_t>(ESP.getEfuseMac() & 0xffffffff), HEX);
	#endif
	root.createNestedObject()[F("id")] = F("CPU Speed");
	root.get<JsonObject&>(6)[F("value")] = String(ESP.getCpuFreqMHz());
	root.createNestedObject()[F("id")] = F("Flash Size");
	root.get<JsonObject&>(7)[F("value")] = String(ESP.getFlashChipSize());
	root.createNestedObject()[F("id")] = F("ESP-DCC++ Build");
	root.get<JsonObject&>(8)[F("value")] = espBuildTime;
	jsonResponse->setLength();
	request->send(jsonResponse);
}

void handleProgrammer(AsyncWebServerRequest *request) {
	auto jsonResponse = new AsyncJsonResponse();
	#ifndef ESP32
	int callbackNumber = ESP.getChipId() / 32767;
	#else
	int callbackNumber = static_cast<int>(ESP.getEfuseMac() & 0xffff);
	#endif
	int callbackSubNumber = millis() % 32767;
	if (request->method() == HTTP_GET) {
		// get status of request
		for (const auto& node : progRequests) {
			if (node->getCallbackNumber() == request->arg("callback").toInt()
					&& node->getCallbackSubNumber()
							== request->arg("subNumber").toInt()) {
				node->toJSON(jsonResponse->getRoot());
				jsonResponse->setCode(200);
			}
		}
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_POST) {
		// new programmer request
		if (request->arg("action") == F("queryCV")) {
			if (request->arg("target") == "true") {
				jsonResponse->setCode(405);
			} else {
				JsonObject &node = jsonResponse->getRoot();
				node[F("callback")] = callbackNumber;
				node[F("subNumber")] =  callbackSubNumber;
				progRequests.add(
						new ProgramRequest(request->arg(F("cv")).toInt(),
								callbackNumber, callbackSubNumber));
				DCCppPendingCommands.push(
						String(F("<R ")) + request->arg(F("cv"))
								+ String(F(" ")) + String(callbackNumber)
								+ String(F(" ")) + String(callbackSubNumber)
								+ String(F(">")));
				jsonResponse->setCode(202);
			}
		} else if (request->arg("action") == F("setCV")) {
			if (request->arg("target") == "true") {
				DCCppPendingCommands.push(
						String(F("<w ")) + request->arg(F("loco"))
								+ String(F(" ")) + request->arg(F("cv"))
								+ String(F(" ")) + request->arg(F("value"))
								+ String(F(">")));
				jsonResponse->setCode(200);
			} else {
				DCCppPendingCommands.push(
						String(F("<W ")) + request->arg(F("cv"))
								+ String(F(" ")) + request->arg(F("value"))
								+ String(F(" ")) + String(callbackNumber)
								+ String(F(" ")) + String(callbackSubNumber)
								+ String(F(">")));
				JsonObject &node = jsonResponse->getRoot();
				node[F("callback")] = callbackNumber;
				node[F("subNumber")] =  callbackSubNumber;
				progRequests.add(
						new ProgramRequest(request->arg(F("cv")).toInt(),
								callbackNumber, callbackSubNumber));
				jsonResponse->setCode(202);
			}
		} else if (request->arg("action") == F("setCVBit")) {
			if (request->arg("target") == "true") {
				DCCppPendingCommands.push(
						String(F("<b ")) + request->arg(F("loco"))
								+ String(F(" ")) + request->arg(F("cv"))
								+ String(F(" ")) + request->arg(F("bit"))
								+ String(F(" ")) + request->arg(F("bitValue")) == "true" ? "1" : "0"
								+ String(F(">")));
				jsonResponse->setCode(200);
			} else {
				DCCppPendingCommands.push(
						String(F("<W ")) + request->arg(F("cv"))
								+ String(F(" ")) + request->arg(F("bit"))
								+ String(F(" ")) + request->arg(F("bitValue")) == "true" ? "1" : "0"
								+ String(F(" ")) + String(callbackNumber)
								+ String(F(" ")) + String(callbackSubNumber)
								+ String(F(">")));
				JsonObject &node = jsonResponse->getRoot();
				node[F("callback")] = callbackNumber;
				node[F("subNumber")] =  callbackSubNumber;
				progRequests.add(
						new ProgramRequest(request->arg(F("cv")).toInt(),
								callbackNumber, callbackSubNumber));
				jsonResponse->setCode(202);
			}
		}
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_DELETE) {
		// cleanup request
		ProgramRequest *foundNode = NULL;
		for (const auto& node : progRequests) {
			if (node->getCallbackNumber() == request->arg("callback").toInt()
					&& node->getCallbackSubNumber()
							== request->arg("callbackSubNumber").toInt()) {
				foundNode = node;
			}
		}
		if (foundNode != NULL) {
			progRequests.remove(foundNode);
			jsonResponse->setCode(200);
			jsonResponse->setLength();
			request->send(jsonResponse);
		} else {
			request->send(404, F("text/plain"), F("Programmer Request not found"));
		}
	}
}

void handleConfig(AsyncWebServerRequest *request) {
	if (request->method() == HTTP_POST) {
		DCCppPendingCommands.push(F("<E>"));
	} else if (request->method() == HTTP_DELETE) {
		DCCppPendingCommands.push(F("<e>"));
		sensors.free();
		outputs.free();
		turnouts.free();
	}
	request->send(202, F("application/json"), F("[]"));
}

void handlePowerStatus(AsyncWebServerRequest *request) {
	auto jsonResponse = new AsyncJsonResponse(true);
	JsonArray &array = jsonResponse->getRoot();
	for (const auto& node : powerDistricts) {
		node->toJSON(array.createNestedObject());
	}
	jsonResponse->setLength();
	request->send(jsonResponse);
}

void handleTurnouts(AsyncWebServerRequest *request) {
	auto jsonResponse = new AsyncJsonResponse(true);
	if (request->method() == HTTP_GET) {
		JsonArray &array = jsonResponse->getRoot();
		for (const auto& node : turnouts) {
			node->toJSON(array.createNestedObject());
		}
		jsonResponse->setCode(200);
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_POST) {
		DCCppPendingCommands.push(
				String(F("<T ")) + request->arg(F("id")) + String(F(" "))
						+ request->arg(F("address")) + String(F(" "))
						+ request->arg(F("subAddress")) + String(F(">")));
		turnouts.add(
				new Turnout(request->arg(F("id")).toInt(),
						request->arg(F("address")).toInt(),
						request->arg(F("subAddress")).toInt()));
		jsonResponse->setCode(202);
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_PUT) {
		if (request->arg(F("state")) == F("true")) {
			DCCppPendingCommands.push(
					String(F("<T ")) + request->arg(F("id"))
							+ String(F(" 1>")));
		} else {
			DCCppPendingCommands.push(
					String(F("<T ")) + request->arg(F("id"))
							+ String(F(" 0>")));
		}
		jsonResponse->setCode(200);
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_DELETE) {
		Turnout *foundNode = NULL;
		for (const auto& node : turnouts) {
			if (node->getId() == request->arg(F("id")).toInt()) {
				foundNode = node;
			}
		}
		if (foundNode != NULL) {
			turnouts.remove(foundNode);
			DCCppPendingCommands.push(
					String(F("<T ")) + request->arg(F("id")) + String(F(">")));
			jsonResponse->setCode(202);
		} else {
			request->send(404, F("text/plain"), F("Turnout not found"));
		}
	}
}

void handleOutputs(AsyncWebServerRequest *request) {
	auto jsonResponse = new AsyncJsonResponse(true);
	if (request->method() == HTTP_GET) {
		JsonArray &array = jsonResponse->getRoot();
		for (const auto& node : outputs) {
			node->toJSON(array.createNestedObject());
		}
		jsonResponse->setCode(200);
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_PUT) {
		DCCppPendingCommands.push(
				String(F("<Z ")) + request->arg(F("id")) + String(F(" "))
						+ request->arg(F("state")) + String(F(">")));
		jsonResponse->setCode(202);
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_POST) {
		DCCppPendingCommands.push(
				String(F("<Z ")) + request->arg(F("id")) + String(F(" "))
						+ request->arg(F("pin")) + String(F(" "))
						+ request->arg(F("invert")) + String(F(">")));
		outputs.add(
				new Output(request->arg(F("id")).toInt(),
						request->arg(F("pin")).toInt(),
						request->arg(F("invert")) == F("true"), false));
		jsonResponse->setCode(202);
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_DELETE) {
		Output *foundNode = NULL;
		for (const auto& node : outputs) {
			if (node->getId() == request->arg(F("id")).toInt()) {
				foundNode = node;
			}
		}
		if (foundNode != NULL) {
			outputs.remove(foundNode);
			DCCppPendingCommands.push(
					String(F("<Z ")) + request->arg(F("id")) + String(F(">")));
			jsonResponse->setCode(202);
			jsonResponse->setLength();
			request->send(jsonResponse);
		} else {
			request->send(404, F("text/plain"), F("Output not found"));
		}
	}
}

void handleSensors(AsyncWebServerRequest *request) {
	auto jsonResponse = new AsyncJsonResponse(true);
	if (request->method() == HTTP_GET) {
		JsonArray &array = jsonResponse->getRoot();
		for (const auto& node : sensors) {
			node->toJSON(array.createNestedObject());
		}
		jsonResponse->setCode(200);
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_POST) {
		DCCppPendingCommands.push(
				String(F("<S ")) + request->arg(F("id")) + String(F(" "))
						+ request->arg(F("pin")) + String(F(" "))
						+ request->arg(F("pullUp")) == F("true") ?
						String(F("1>")) : String(F("0>")));
		sensors.add(
				new Sensor(request->arg(F("id")).toInt(),
						request->arg(F("pin")).toInt(),
						false, request->arg(F("pullUp")) == F("true")));
		jsonResponse->setCode(202);
		jsonResponse->setLength();
		request->send(jsonResponse);
	} else if (request->method() == HTTP_DELETE) {
		Sensor *foundNode = NULL;
		for (const auto& node : sensors) {
			if (node->getId() == request->arg(F("id")).toInt()) {
				foundNode = node;
			}
		}
		if (foundNode != NULL) {
			sensors.remove(foundNode);
			DCCppPendingCommands.push(
					String(F("<S ")) + request->arg(F("id")) + String(F(">")));
			jsonResponse->setCode(202);
			jsonResponse->setLength();
			request->send(jsonResponse);
		} else {
			request->send(404, F("text/plain"), F("Sensor not found"));
		}
	}
}
}

namespace DCCpp {
    namespace Server {
		Stream* dccpp_stream;
		void setup(Stream& theStream)
		{
			dccpp_stream = &theStream;
			SPIFFS.begin();
		#ifndef ESP32
			WiFi.setAutoConnect(false);
			WiFi.hostname(HOSTNAME);
		#else
			WiFi.getMode();
			WiFi.mode(WIFI_MODE_STA);
			WiFi.setAutoConnect(false);
			WiFi.setHostname(HOSTNAME);
		#endif
			currentDCCppCommand.reserve(128);

			webSocket.onEvent(onWSEvent);
			webServer.addHandler(&webSocket);
			webServer.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setLastModified(espBuildTime);
			webServer.on("/espinfo", HTTP_GET, &handleESPInfo);
			webServer.on("/dccpp/programmer", HTTP_GET | HTTP_POST | HTTP_DELETE,
					&handleProgrammer);
			webServer.on("/dccpp/config", HTTP_POST, &handleConfig);
			webServer.on("/dccpp/powerStatus", HTTP_GET, &handlePowerStatus);
			webServer.on("/dccpp/turnouts",
					HTTP_GET | HTTP_POST | HTTP_DELETE | HTTP_PUT, &handleTurnouts);
			webServer.on("/dccpp/sensors", HTTP_GET | HTTP_POST | HTTP_DELETE,
					&handleSensors);
			webServer.on("/dccpp/outputs",
					HTTP_GET | HTTP_POST | HTTP_DELETE | HTTP_PUT, &handleOutputs);
			webServer.onNotFound(&handleNotFound);

			DCCppServer.setNoDelay(true);

			MDNS.addService("http", "tcp", 80);
			MDNS.addService("dccpp_web", "http", 80);
			MDNS.addService("dccpp", "tcp", 2560);
			// TBD : WiThrottle support
			//MDNS.addService("_withrottle", "tcp", 81);
			//MDNS.addServiceTxt("_withrottle", "tcp", "jmri", "4.5.7");
			dccpp_stream->println(F("<iESP-DCC++ init>"));
		}

		void loop() {
			taskScheduler.execute();
			#ifndef ESP32
			if (DCCppServer.status() == LISTEN) {
			#else
			if (DCCppServer) {
			#endif
				if (DCCppServer.hasClient()) {
					for (int i = 0; i < MAX_DCCPP_CLIENTS; i++) {
						if (!DCCppClients[i] || !DCCppClients[i].connected()) {
							if (DCCppClients[i]) {
								DCCppClients[i].stop();
							}
							DCCppClients[i] = DCCppServer.available();
							continue;
						}
					}
					DCCppServer.available().stop();
				}
				//check clients for data
				for (int i = 0; i < MAX_DCCPP_CLIENTS; i++) {
					if (DCCppClients[i] && DCCppClients[i].connected()) {
						if (DCCppClients[i].available()) {
							static std::vector<uint8_t> buffer;
							auto len = DCCppClients[i].available();
							auto read_dest = buffer.insert(buffer.end(), len + 1, 0);
							auto added = DCCppClients[i].read(&*read_dest, len);
							buffer.erase(read_dest + added, buffer.end()); //to handle if we got < len
							//now, parse whats in buffer:
							auto s = buffer.begin();
							auto consumed = buffer.begin();
							for(; s != buffer.end();) {
								s = std::find(s, buffer.end(), '<');
								auto e = std::find(s, buffer.end(), '>');
								if(s != buffer.end() && e != buffer.end()) {
									*e = 0;
									String str(reinterpret_cast<char*>(&*s));
									str += '>';
									DCCppPendingCommands.push(std::move(str));
									consumed = e;
								}
								s = e;
							}
							buffer.erase(buffer.begin(), consumed); // drop everything we used from the buffer.
						}
					}
				}
			}
			//check UART for data
			while (dccpp_stream->available()) {
				char ch = dccpp_stream->read();
				if (!inDCCPPCommand && ch == '<') {
					inDCCPPCommand = true;
				}
				if (inDCCPPCommand) {
					currentDCCppCommand += ch;
				}
				if (inDCCPPCommand && ch == '>') {
					inDCCPPCommand = false;
					if (currentDCCppCommand.startsWith(F("<iESP-"))) {
						if (currentDCCppCommand.indexOf(F("connect")) > 0) {
							size_t firstSpace = currentDCCppCommand.indexOf(' ');
							size_t secondSpace = currentDCCppCommand.indexOf(' ',
									firstSpace + 1);
							long ssidLength = currentDCCppCommand.substring(
									firstSpace + 1, secondSpace).toInt();
							firstSpace = secondSpace++;
							secondSpace += ssidLength;
							String ssid = currentDCCppCommand.substring(firstSpace + 1,
									secondSpace);
							String password = currentDCCppCommand.substring(
									secondSpace + 1, currentDCCppCommand.length() - 1);
							WiFi.setAutoReconnect(false);
							dccpp_stream->print(F("<iESP connecting to "));
							dccpp_stream->print(ssid);
							dccpp_stream->println(F(">"));
							WiFi.begin(ssid.c_str(), password.c_str());
							bool connecting = true;
							// ~30sec timeout
							int connectTimeout = 120;
							while (connecting && connectTimeout) {
								connectTimeout--;
								delay(250);
								switch (WiFi.status()) {
								case WL_CONNECTED:
									dccpp_stream->print(F("<iESP connected "));
									dccpp_stream->print(WiFi.localIP());
									dccpp_stream->println(F(">"));
									WiFi.setAutoReconnect(true);
									connecting = false;
									break;
								case WL_CONNECT_FAILED:
									dccpp_stream->println(F("<iESP connect failed>"));
									connecting = false;
									break;
								case WL_DISCONNECTED:
									// ignoring this as we get this status until it either succeeds or fails.
									break;
								case WL_NO_SSID_AVAIL:
									dccpp_stream->println(F("<iESP AP not found>"));
									connecting = false;
									break;
								default:
									dccpp_stream->print(F("<iESP status "));
									dccpp_stream->print(WiFi.status());
									dccpp_stream->println(F(">"));
									break;
								}
							}
							if (WiFi.status() != WL_CONNECTED && !connectTimeout) {
								dccpp_stream->println(F("<iESP connect timeout>"));
							}
						} else if (currentDCCppCommand.indexOf(F("start")) > 0) {
							DCCppServer.begin();
							webServer.begin();
							MDNS.begin(HOSTNAME);
							taskScheduler.enableAll();
							dccpp_stream->println(F("<iESP ready>"));
						} else if (currentDCCppCommand.indexOf(F("stop")) > 0) {
							taskScheduler.disableAll();
							DCCppServer.stop();
							dccpp_stream->println(F("<iESP shutdown>"));
						} else if (currentDCCppCommand.indexOf(F("reset")) > 0) {
							ESP.restart();
						} else if (currentDCCppCommand.indexOf(F("ip")) > 0) {
							dccpp_stream->print(F("<iESP ip "));
							if (WiFi.status() == WL_CONNECTED) {
								dccpp_stream->print(WiFi.localIP());
							} else {
								dccpp_stream->print(F("0.0.0.0"));
							}
							dccpp_stream->println(F(">"));
						} else if (currentDCCppCommand.indexOf(F("scan")) > 0) {
							int networkCount = WiFi.scanNetworks();
							for (int net = 0; net < networkCount; net++) {
								dccpp_stream->print(F("<iESP-network: "));
								dccpp_stream->print(WiFi.SSID(net));
								dccpp_stream->println(F(">"));
							}
						} else if (currentDCCppCommand.indexOf(F("status")) > 0) {
							dccpp_stream->print(F("<iESP-status "));
							dccpp_stream->print(WiFi.status());
							dccpp_stream->print(F(" "));
							if (WiFi.status() == WL_CONNECTED) {
								dccpp_stream->print(WiFi.localIP());
							} else {
								dccpp_stream->print(F("0.0.0.0"));
							}
							dccpp_stream->print(F(" "));
		#ifndef ESP32
							dccpp_stream->print(DCCppServer.status());
		#else
							dccpp_stream->print(DCCppServer ? "1" : "0");
		#endif
							dccpp_stream->println(F(">"));
						}
					} else if (currentDCCppCommand.startsWith(F("<a "))) {
						// parse current details so we can display it
						if (currentDCCppCommand.lastIndexOf(' ') > 2) {
							int powerUsage = currentDCCppCommand.substring(2,
									currentDCCppCommand.lastIndexOf(' ')).toInt();
							String districtName = currentDCCppCommand.substring(
									currentDCCppCommand.lastIndexOf(' '),
									currentDCCppCommand.length() - 1);
							districtName.trim();
							//dccpp_stream->printf("DETECTED: %s / %d", districtName.c_str(), powerUsage);
							bool districtFound = false;
							for (const auto& node : powerDistricts) {
								if (node->getDistrictName() == districtName) {
									districtFound = true;
									node->setPowerUsage(powerUsage);
								}
							}
							if (!districtFound) {
								powerDistricts.add(
										new PowerDistrict(districtName, true,
												powerUsage));
							}
							//} else {
							//	dccpp_stream->printf("UNABLE TO PARSE: %s", currentDCCppCommand.c_str());
						}
					} else if (currentDCCppCommand.startsWith(F("<p"))) {
						// parse power district status
						bool districtOn = currentDCCppCommand[2] == '1';
						bool districtOverPower = (currentDCCppCommand[2] == '2');
						String districtName = currentDCCppCommand.substring(
								currentDCCppCommand.lastIndexOf(' '),
								currentDCCppCommand.length() - 1);
						districtName.trim();
						bool districtFound = false;
						for (const auto& node : powerDistricts) {
							if (node->getDistrictName() == districtName) {
								districtFound = true;
								node->setOverCurrent(districtOverPower);
								node->setCurrentState(districtOn);
							}
						}
						if (!districtFound) {
							powerDistricts.add(
									new PowerDistrict(districtName, districtOn, 0,
											districtOverPower));
						}
					} else if (currentDCCppCommand.equals(F("<0>"))) {
						// mark track power status as off
						for (const auto& node : powerDistricts) {
							node->setCurrentState(false);
						}
					} else if (currentDCCppCommand.equals(F("<1>"))) {
						// mark track power status as on
						for (const auto& node : powerDistricts) {
							node->setCurrentState(true);
							node->setOverCurrent(false);
						}
					} else if (currentDCCppCommand.startsWith(F("<q "))
							|| currentDCCppCommand.startsWith(F("<Q "))) {
						// sensor parsing
						int firstSpace = currentDCCppCommand.indexOf(' ');
						if (currentDCCppCommand.lastIndexOf(' ') > 2) {
							// <Q ID PIN PULLUP>
							int secondSpace = currentDCCppCommand.indexOf(' ',
									firstSpace + 1);
							int thirdSpace = currentDCCppCommand.indexOf(' ',
									secondSpace + 1);
							int sensorID = currentDCCppCommand.substring(firstSpace,
									secondSpace).toInt();
							int sensorPin = currentDCCppCommand.substring(secondSpace,
									thirdSpace).toInt();
							bool sensorPullUp =
									currentDCCppCommand.substring(thirdSpace,
											currentDCCppCommand.length() - 1).toInt()
											== 1;
							bool sensorFound = false;
							for (const auto& node : sensors) {
								if (node->getId() == sensorID) {
									sensorFound = true;
								}
							}
							if (!sensorFound) {
								sensors.add(
										new Sensor(sensorID, sensorPin, false,
												sensorPullUp));
							}
						} else {
							// <q ID> or <Q ID>
							int sensorID = currentDCCppCommand.substring(firstSpace,
									currentDCCppCommand.length() - 1).toInt();
							bool sensorActive = currentDCCppCommand[1] == 'Q';
							bool sensorFound = false;
							for (const auto& node : sensors) {
								if (node->getId() == sensorID) {
									sensorFound = true;
									node->setActive(sensorActive);
								}
							}
							if (!sensorFound) {
								forceRefreshSensors = true;
							}
						}
					} else if (currentDCCppCommand.startsWith(F("<H "))) {
						// turnout parsing
						int firstSpace = currentDCCppCommand.indexOf(' ');
						int secondSpace = currentDCCppCommand.indexOf(' ',
								firstSpace + 1);
						int turnoutID = currentDCCppCommand.substring(firstSpace,
								secondSpace).toInt();
						if (currentDCCppCommand.lastIndexOf(' ') > secondSpace) {
							int thirdSpace = currentDCCppCommand.indexOf(' ',
									secondSpace + 1);
							int fourthSpace = currentDCCppCommand.indexOf(' ',
									thirdSpace + 1);
							int address = currentDCCppCommand.substring(secondSpace + 1,
									thirdSpace).toInt();
							int subAddress = currentDCCppCommand.substring(
									thirdSpace + 1, fourthSpace).toInt();
							bool state = currentDCCppCommand.substring(fourthSpace + 1,
									currentDCCppCommand.length() - 1).toInt() == 1;
							bool foundTurnout = false;
							for (const auto& node : turnouts) {
								if (node->getId() == turnoutID) {
									node->setThrown(state);
									foundTurnout = true;
								}
							}
							if (!foundTurnout) {
								turnouts.add(
										new Turnout(turnoutID, address, subAddress,
												state));
							}
						} else {
							bool state = currentDCCppCommand.substring(secondSpace + 1,
									currentDCCppCommand.length() - 1).toInt() == 1;
							for (const auto& node : turnouts) {
								if (node->getId() == turnoutID) {
									node->setThrown(state);
								}
							}
						}
					} else if (currentDCCppCommand.startsWith(F("<Y "))) {
						// outputs parsing
						int firstSpace = currentDCCppCommand.indexOf(' ');
						int secondSpace = currentDCCppCommand.indexOf(' ',
								firstSpace + 1);
						int outputID = currentDCCppCommand.substring(firstSpace,
								secondSpace).toInt();
						if (currentDCCppCommand.lastIndexOf(' ') > secondSpace) {
							int thirdSpace = currentDCCppCommand.indexOf(' ',
									secondSpace + 1);
							int fourthSpace = currentDCCppCommand.indexOf(' ',
									thirdSpace + 1);
							int pinID = currentDCCppCommand.substring(secondSpace,
									thirdSpace).toInt();
							bool inverted = currentDCCppCommand.substring(
									thirdSpace + 1, fourthSpace).toInt() == 1;
							bool active = currentDCCppCommand.substring(fourthSpace + 1,
									currentDCCppCommand.length() - 1).toInt() == 1;
							bool foundOutput = false;
							for (const auto& node : outputs) {
								if (node->getId() == outputID) {
									node->setActive(active);
									foundOutput = true;
								}
							}
							if (!foundOutput) {
								outputs.add(
										new Output(outputID, pinID, inverted, active));
							}
						} else {
							bool active = currentDCCppCommand.substring(secondSpace + 1,
									currentDCCppCommand.length() - 1).toInt() == 1;
							for (const auto& node : outputs) {
								if (node->getId() == outputID) {
									node->setActive(active);
								}
							}
						}
					} else if (currentDCCppCommand.startsWith(F("<r"))) {
						// <rCALLBACK|CALLBACKSUB|CV Value>
						// <rCALLBACK|CALLBACKSUB|CV VALUE>
						// <rCALLBACK|CALLBACKSUB|CV BIT VALUE>
						int firstPipe = currentDCCppCommand.indexOf('|');
						int secondPipe = currentDCCppCommand.indexOf('|',
								firstPipe + 1);
						int lastSpace = currentDCCppCommand.lastIndexOf(' ');
						int callbackNumber =
								currentDCCppCommand.substring(2, firstPipe).toInt();
						int callbackSubNumber = currentDCCppCommand.substring(
								firstPipe + 1, secondPipe).toInt();
						for (const auto& node : progRequests) {
							if (node->getCallbackNumber() == callbackNumber
									&& node->getCallbackSubNumber()
											== callbackSubNumber) {
								node->setValue(
										currentDCCppCommand.substring(lastSpace + 1,
												currentDCCppCommand.length() - 1).toInt());
							}
						}
					}

				#ifndef ESP32
					if (DCCppServer.status() == LISTEN) {
				#else
					if (DCCppServer) {
				#endif
						for (int i = 0; i < MAX_DCCPP_CLIENTS; i++) {
							if (DCCppClients[i] && DCCppClients[i].connected()) {
								DCCppClients[i].print(currentDCCppCommand);
								delay(1);
							}
						}
						webSocket.textAll(currentDCCppCommand);
					}
					currentDCCppCommand = "";
				}
			}
			// drain the queued up commands
			while (DCCppPendingCommands.count() > 0) {
				dccpp_stream->print(DCCppPendingCommands.pop());
			}
		}
	}
}
