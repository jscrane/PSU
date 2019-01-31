#include <Wire.h>
#include <Adafruit_INA219.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <SimpleTimer.h>

#include "Configuration.h"
#include "dbg.h"
#include "rssi.h"

#define SWITCH  D3

Adafruit_INA219 ina219;
TFT_eSPI tft;

MDNSResponder mdns;
WiFiClient wifiClient;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
DNSServer dnsServer;

bool debug;

class config: public Configuration {
public:
	char ssid[33];
	char password[33];
	char hostname[17];
	bool debug;
	float presets[10];

	void configure(JsonObject &o);
} cfg;

void config::configure(JsonObject &o) {
	strlcpy(ssid, o[F("ssid")] | "", sizeof(ssid));
	strlcpy(password, o[F("password")] | "", sizeof(password));
	strlcpy(hostname, o[F("hostname")] | "", sizeof(hostname));
	debug = o[F("debug")] | false;
	JsonArray &p = o[F("presets")];
	if (p.success())
		for (int i = 0; i < p.size() && i < sizeof(presets) / sizeof(presets[0]); i++)
			presets[i] = p.get<float>(i);
}

static const char *config_file = "/config.json";
static const unsigned long UPDATE_RSSI = 1000, UPDATE_VI = 250;

static volatile bool swtch;
static bool connected;
static RSSI rssi(tft, 5);

static float shuntvoltage, busvoltage, current_mA, loadvoltage, power_mW;
static int wr, tv;
static SimpleTimer timers;

static void draw_rssi() {
	if (wr != 31) {
		int r = wr;
		const int t[] = {-90, -80, -70, -67, -40};
		rssi.update(updater([r, t](int i)->bool { return r > t[i]; }));
	}
}

static void draw_vi() {
	tft.setCursor(0, 1);
	tft.setTextFont(0);
	tft.printf("Bus: %4.2fV\r\n", busvoltage);
	tft.printf("Shunt: %4.2fmV\r\n", shuntvoltage);
	tft.printf("Target: %4.1fv\r\n", cfg.presets[tv]);

	tft.setTextFont(2);
	tft.printf("%4.2fV\r\n", loadvoltage);
	tft.printf("%4.2fmA\r\n", current_mA);
	tft.printf("%4.1fmW\r\n", power_mW);
}

void setup() {
	Serial.begin(115200);
	Serial.println(F("Booting!"));

	bool result = SPIFFS.begin();
	if (!result) {
		ERR(print(F("SPIFFS: ")));
		ERR(println(result));
		return;
	}

	if (!cfg.read_file(config_file)) {
		ERR(print(F("config!")));
		return;
	}

	pinMode(SWITCH, INPUT_PULLUP);
	debug = digitalRead(SWITCH) == LOW || cfg.debug;

	int bg = debug? TFT_RED: TFT_BLUE;

	tft.init();
	tft.setTextColor(TFT_WHITE, bg);
	tft.fillScreen(bg);
	tft.setCursor(0, 0);
	tft.setRotation(3);

	rssi.colors(TFT_WHITE, bg);
	rssi.init(tft.width() - 21, 0, 20, 20);

	WiFi.mode(WIFI_STA);
	WiFi.hostname(cfg.hostname);
	if (*cfg.ssid) {
		WiFi.setAutoReconnect(true);
		WiFi.begin(cfg.ssid, cfg.password);
		for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
			delay(500);
			DBG(print('.'));
			rssi.update(updater([i](int b) { return i % 5 == b; }));
		}
		connected = WiFi.status() == WL_CONNECTED;
	}

	server.on("/config", HTTP_POST, []() {
		if (server.hasArg("plain")) {
			String body = server.arg("plain");
			File f = SPIFFS.open(config_file, "w");
			f.print(body);
			f.close();
			server.send(200);
			ESP.restart();
		} else
			server.send(400, "text/plain", "No body!");
	});
	server.serveStatic("/", SPIFFS, "/index.html");
	server.serveStatic("/config", SPIFFS, config_file);
	server.serveStatic("/js/transparency.min.js", SPIFFS, "/transparency.min.js");
	server.serveStatic("/info.png", SPIFFS, "/info.png");

	httpUpdater.setup(&server);
	server.begin();

	if (mdns.begin(cfg.hostname, WiFi.localIP())) {
		DBG(println(F("mDNS started")));
		mdns.addService("http", "tcp", 80);
	} else
		ERR(println(F("Error starting MDNS")));

	if (!connected) {
		WiFi.softAP(cfg.hostname);
		DBG(print(F("Connect to SSID: ")));
		DBG(print(cfg.hostname));
		DBG(println(F(" to configure WIFI")));
		dnsServer.start(53, "*", WiFi.softAPIP());
	} else {
		DBG(println());
		DBG(print(F("Connected to ")));
		DBG(println(cfg.ssid));
		DBG(println(WiFi.localIP()));
	}

	attachInterrupt(SWITCH, []() { swtch=true; }, FALLING);
	ina219.begin();

	timers.setInterval(UPDATE_RSSI, draw_rssi);
	timers.setInterval(UPDATE_VI, draw_vi);
}

void loop() {

	mdns.update();
	server.handleClient();

	if (!connected)
		dnsServer.processNextRequest();

	wr = WiFi.RSSI();

	shuntvoltage = ina219.getShuntVoltage_mV();
	busvoltage = ina219.getBusVoltage_V();
	current_mA = ina219.getCurrent_mA();
	power_mW = ina219.getPower_mW();
	loadvoltage = busvoltage + (shuntvoltage / 1000);
	
	if (swtch) {
		swtch = false;
		tv++;
		if (tv == sizeof(cfg.presets) / sizeof(cfg.presets[0]) || cfg.presets[tv] == 0.0)
			tv = 0;
		draw_vi();
	}

	timers.run();
}
