#pragma once
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <tuple>
#include <unordered_map>

class CabCache
{
	AsyncWebSocket mWebsocket;
	struct CabInfoT {
		String name;
		int speed = 0; // < 0 is reverse.
	};
	//map id => name, speed
	std::unordered_map<int, CabInfoT> mCache;
	std::unordered_map<int, std::string> mClientBuf;
public:
	CabCache(String const& url);
	void hookUp(AsyncWebServer& server);
	void update(int id, int speed, int direction);
	void pushUpdates(int id, AsyncWebSocketClient* client = nullptr); // null for pushing to all.

	void loadFrom(fs::FS& fs, char const* path);
private:
	void handleReq(AsyncWebSocket * server, AsyncWebSocketClient * client,
				   AwsEventType type, void * arg, uint8_t *data, size_t len);
	void handleReq(AsyncWebSocketClient * client, std::string& message);
};
