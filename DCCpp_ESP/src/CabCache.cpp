#include "config.h"
#include "CabCache.h"
#include "ArduinoJson.h"
#include "AsyncJson.h"

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
