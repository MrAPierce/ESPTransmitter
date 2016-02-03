
#include <user_config.h>
#include <SmingCore/SmingCore.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <Adafruit_MCP23017.h>
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
Adafruit_MCP23017 mcp = Adafruit_MCP23017();

HttpServer server;
String ip;
bool stationConnected = false, wifiscanning = false;
Timer connectionTimer, healthTimer;
int dataRateCount = 0;

#ifndef WIFI_SSID
	#define WIFI_SSID "PleaseEnterSSID" // Put you SSID and Password here
	#define WIFI_PWD "PleaseEnterPass"
#endif

void onIndex(HttpRequest &request, HttpResponse &response)
{
	response.sendFile("index.html");
}

void onFile(HttpRequest &request, HttpResponse &response)
{
	String file = request.getPath();
	if (file[0] == '/')
		file = file.substring(1);

	if (file[0] == '.')
		response.forbidden();
	else
	{
		if(file.endsWith(".appcache") == false) {
			response.setCache(86400, true);
		}
		response.sendFile(file);
	}
}

String printable(const String& source) {
   String result="";
   for (unsigned int i=0; i<source.length(); i++) {
      if(source[i]>=32 && source[i] <128) {
    	  result+=source[i];
      }
   }
   return result;
}

void networkScanCompleted(bool succeeded, BssList list)
{
	String data;
	if (succeeded)
	{
		wifiscanning = false;
		StaticJsonBuffer<2048> jsonBuffer;
		JsonObject& networkData = jsonBuffer.createObject();
		networkData["type"] = "wifi";
		networkData["failed"] = false;
		JsonArray& netlist = networkData.createNestedArray("available");
		for (int i = 0; i < list.count(); i++)
		{
			if (list[i].hidden || list[i].ssid.length() == 0) continue;
			String title =  printable(list[i].ssid);
			String current = WifiStation.getConnectionStatusName();
			JsonObject &item = netlist.createNestedObject();
			item["id"] = (int)list[i].getHashId();
			item["title"] = title;
			item["signal"] = list[i].rssi;
			item["hidden"] = list[i].hidden;
			item["encryption"] = list[i].getAuthorizationMethodName();
			item["connected"] = title.compareTo(current) == 0;
		}
		networkData.printTo(data);
	}
	else
	{
		wifiscanning = false;
		StaticJsonBuffer<100> jsonBuffer;
		JsonObject& networkData = jsonBuffer.createObject();
		networkData["type"] = "wifi";
		networkData["failed"] = true;
		networkData.printTo(data);
	}

	WebSocketsList &clients = server.getActiveWebSockets();
	for (int i = 0; i < clients.count(); i++) {
		clients[i].sendString(data);
	}

	data = null;
}

uint16_t convertFrom8To16(uint8_t dataFirst, uint8_t dataSecond) {
    uint16_t dataBoth = 0x0000;
    dataBoth = dataSecond;
    dataBoth = dataBoth << 8;
    dataBoth |= dataFirst;
    return dataBoth;
}

void wsMessageReceived(WebSocket& socket, const String& message)
{
	char* data = (char*)message.c_str();
	Serial.printf("WebSocket message received:\r\n%s\r\n", data);

	StaticJsonBuffer<200> jsonBuffer;
	JsonObject& root = jsonBuffer.parseObject(data);

	if (!root.success())
	{
	  Serial.println("parseObject() failed");
	  return;
	}

	const char* type = root["type"];
	Serial.println(type);

	if(strcmp(type,"sr") == 0) {
		//Serial.println("Received a server request");
		const char* cmd = root["cmd"];

		if(strcmp(cmd,"scanwifi") == 0) {
			if(!wifiscanning) {
				WifiStation.startScan(networkScanCompleted);
				wifiscanning = true;
			}
		} else if(strcmp(cmd,"mcp") == 0) {
//			JsonObject& pinData = root["data"];
//			for(JsonObject::iterator it=pinData.begin(); it!=pinData.end(); ++it)
//			{
//			    int pin = it->key;
//			    int value = it->value;
//
//			    Serial.println(pin + " " + value);
//			}
		}
	}
}

void wsBinaryReceived(WebSocket& socket, uint8_t* data, size_t size)
{
	//TODO, check date rate stability on decent power supply
	//TODO checksum
	//TODO pass defaults and data rate, fail safe on data rate
	for(int i = 0; i < 16; i++) {
		uint16_t value = convertFrom8To16(data[i*2],data[(i*2)+1]);
		//pwm.setPWM(i, 0, value);
	}
	dataRateCount++;
}

void wsConnected(WebSocket& socket) {


}

void onHealthTime() {

	WebSocketsList &clients = server.getActiveWebSockets();
	int clientsCount = clients.count();
	if(clientsCount > 0) {

		StaticJsonBuffer<512> jsonBuffer;
		JsonObject& osData = jsonBuffer.createObject();
		osData["type"] = "os";
		JsonObject& data = osData.createNestedObject("data");
		data["rate"] = dataRateCount;
		data["mem"] = system_get_free_heap_size();
		data["scan"] = wifiscanning;
		String json;
		osData.printTo(json);

		for (int i = 0; i < clientsCount; i++)
		{
			clients[i].sendString(json);
		}
		dataRateCount = 0;
	}
}

void webServer()
{
	server.listen(80);
	server.addPath("/", onIndex);
	server.setDefaultHandler(onFile);

	// Web Sockets configuration
	server.enableWebSockets(true);
	server.setWebSocketConnectionHandler(wsConnected);
	server.setWebSocketBinaryHandler(wsBinaryReceived);
	server.setWebSocketMessageHandler(wsMessageReceived);

	healthTimer.initializeMs(1000, onHealthTime).start();
}

void stationErrorStatus() {
	EStationConnectionStatus status = WifiStation.getConnectionStatus();
	Serial.println("Station connection failed");
	if(status == eSCS_WrongPassword) {
		Serial.println("Invalid password");
	}
	else if(status == eSCS_AccessPointNotFound) {
		Serial.println("Access point not found");
	}
	else{
		Serial.println("Connection error");
		//TODO, make better :/
	}
}

void startServices() {
	webServer();
	Serial.println("=== Transmitter Started =====");
	if(WifiStation.isConnectionFailed()) {
		stationErrorStatus();
		Serial.println("AP Mode enabled");
		ip  = WifiAccessPoint.getIP().toString();
	}
	else
	{
		ip = WifiStation.getIP().toString();
	}
	Serial.println(ip);
	Serial.println("==============================");
}

void wifiConnected() {
	webServer();
	//startServices();
}

void connectOk()
{
	stationConnected = true;
}

void connectFail()
{
	WifiStation.waitConnection(connectOk, 10, connectFail); // Repeat and check again
}

void init()
{
	System.setCpuFrequency(eCF_160MHz);
	Serial.begin(SERIAL_BAUD_RATE); // 115200 by default
	Serial.systemDebugOutput(false); // Enable debug output to serial
	pwm.begin();
	pwm.setPWMFreq(50);

	mcp.begin(0x20);

	for(int i = 0; i < 16; i++) {
		mcp.pinMode(i, OUTPUT);
	}

	System.onReady(wifiConnected);

	WifiAccessPoint.enable(true);
	WifiAccessPoint.config("transmitter","transmitter",AUTH_WPA_WPA2_PSK);

	WifiStation.enable(true);
	WifiStation.config(WIFI_SSID, WIFI_PWD);
	WifiStation.waitConnection(connectOk,30,connectFail);
}
