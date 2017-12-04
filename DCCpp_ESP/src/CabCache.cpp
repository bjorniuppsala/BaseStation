#include "config.h"
#include "CabCache.h"
#include "ArduinoJson.h"
#include "AsyncJson.h"

CabCache::CabCache(String const& url)
: mCacheBuffer { 128 }
, mCache { mCacheBuffer.createObject() }
, mEventSource { url }
{}
void CabCache::hookUp(AsyncWebServer& server)
{
	mEventSource.onConnect([](AsyncEventSourceClient *client){ Serial.printf("Yay! a client! \n");});
	mEventSource.setFilter([](AsyncWebServerRequest *client) {
		 Serial.printf("Filering client! conntype=%s method = %s\n",
		  client->requestedConnTypeToString(), client->methodToString()); return true;});
	server.addHandler(&mEventSource);
}
void CabCache::update(int id, int speed, int direction)
{
	auto idStr = String(id);
	auto exists = mCache.containsKey(idStr);
	JsonObject& item = exists ? mCache.get<JsonObject>(idStr) : mCache.createNestedObject(idStr);
	if(!exists)
		item["id"] = item["name"] = String(id);
	item["speed"] = String(speed);
	item["direction"] = String(direction);
	String str;
	item.printTo(str);
//	Serial.printf("item is now existed:: %d %s\n", exists, str.c_str());
	pushUpdates();
}

void CabCache::pushUpdates()
{
	String asStr;
	mCache.printTo(asStr);
	//Serial.printf("pushing update %s\n", asStr.c_str());
	mEventSource.send(asStr.c_str());
}

void CabCache::handleReq(AsyncWebSocket * server, AsyncWebSocketClient * client,
			   AwsEventType type, void * arg, uint8_t *data, size_t len)
{
}
