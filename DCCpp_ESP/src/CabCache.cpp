#include "config.h"
#include "CabCache.h"
#include "DCCpp_ESP.h"
#include "ArduinoJson.h"
#include "AsyncJson.h"
#include <algorithm>

CabCache::CabCache(String const& url)
: mWebsocket { url }
{}
void CabCache::hookUp(AsyncWebServer& server)
{
	server.addHandler(&mWebsocket);
}
void CabCache::update(int id, int speed, int direction)
{
	auto idStr = String(id);
	auto& c = mCache[id];
	if(c.name.length() == 0)
		c.name = String{id};
	c.speed = direction ? speed : -speed;
	pushUpdates();
}

void CabCache::pushUpdates()
{
	DynamicJsonBuffer buf;
	auto& root = buf.createObject();
	root["type"] = "trains";
	for(auto const& t : mCache) {
		auto& train = root.createNestedObject(String{t.first});
		train["speed"] = t.second.speed;
		train["name"] = t.second.name.c_str(); //to save some copying.
		train["id"] = t.first;
	}
	String asStr;
	root.printTo(asStr);
	//Serial.printf("pushing update %s\n", asStr.c_str());
	mWebsocket.textAll(asStr.c_str());
}

void CabCache::handleReq(AsyncWebSocket * server, AsyncWebSocketClient * client,
			   AwsEventType type, void * arg, uint8_t *data, size_t len)
{
	switch(type) {
		case WS_EVT_CONNECT:
			mClientBuf[client->id()] = {};
			break;
		case WS_EVT_DISCONNECT:
			mClientBuf.erase(client->id());
			break;
		case WS_EVT_PONG:
			break;
		case WS_EVT_ERROR:
			//well..
			break;
		case WS_EVT_DATA:
			auto info = reinterpret_cast<AwsFrameInfo const*>(arg);
			auto& buffer = mClientBuf[client->id()];

            // First packet
            if (info->index == 0) {
                if(info->len > MAX_WEBSOCKET_DATA_SIZE)
                    return;
                buffer.clear();// to make sure that the buffer does not contain any junk from old messages.
				buffer.resize(info->len);
            }

            // Store data
            if(info->index + len > buffer.size())
                return;
			std::copy(data, data + len, buffer.begin() + info->index);

            // Last packet
            if (info->index + len == info->len && info->final) {
                handleReq(client, buffer);
                buffer.clear();
            }
			break;
	};
}
void CabCache::handleReq(AsyncWebSocketClient * client, std::string& message)
{
	DynamicJsonBuffer buffer;
	auto& json = buffer.parseObject(const_cast<char*>(message.c_str()));
	if(json["type"] == "command") {
		String msg = json["command"];
		if(msg.length() > 0);
		DCCpp::Server::pushPendingDCCCommand(std::move(msg));
	} else if(json["type"] == "speed"){
		StreamString msg;
		int speed = json["speed"];
		int direction = speed < 0 ? 0 : 1;
		speed = abs(speed);
		msg.printf(PSTR("<t %d %d %d %d>"), json["register"], json["address"], speed, direction);
		DCCpp::Server::pushPendingDCCCommand(std::move(msg));
	}

}

void CabCache::loadFrom(fs::FS& fs, char const* path)
{
	auto f = fs.open(path, "r");
	if(!f || f.isDirectory())
		return;
	DynamicJsonBuffer buf;
	auto& json = buf.parseObject(f);
	for(auto const& c : json) {
		Serial.printf("considering: %s ", c.key);
		c.value.printTo(Serial);
		Serial.printf("\n");
		auto& u = c.value.as<JsonObject const&>();
		if(c.key && u.containsKey("speed")) {
			auto& dest = mCache[atoi(c.key)];
			dest.speed = u["speed"];
			if(char const* name = c.value["name"])
				dest.name = name;
		}
	}
}
