#pragma once
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <map>

class CabCache
{
	DynamicJsonBuffer mCacheBuffer;
	JsonObject& mCache;

	AsyncEventSource mEventSource;

public:
	CabCache(String const& url);
	void hookUp(AsyncWebServer& server);
	void update(int id, int speed, int direction);
	void pushUpdates();
private:
	void handleReq(AsyncWebSocket * server, AsyncWebSocketClient * client,
				   AwsEventType type, void * arg, uint8_t *data, size_t len);
};