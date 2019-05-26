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
#include <X9C.h>

#include "Configuration.h"
#include "dbg.h"
#include "rssi.h"
#include "Stator.h"

#define SWITCH  D3

Adafruit_INA219 ina219;
TwoWire wire;
TFT_eSPI tft;
X9C x9c;

#define X9C_CS	D0
#define X9C_UD	RX
#define X9C_INC	D4

#define SDA	D2
#define SCL	D1

MDNSResponder mdns;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
DNSServer dnsServer;

bool debug;
unsigned bgcolor;

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

static RSSI rssi(tft, 5);
const int rssi_error = 31;

static float shuntvoltage, busvoltage, current_mA, loadvoltage, power_mW;
static int wr = rssi_error, tv;
static SimpleTimer timers;

static Stator<bool> swtch;

static void draw_rssi() {
	if (wr != rssi_error) {
		int r = wr;
		const int t[] = {-90, -80, -70, -67, -40};
		rssi.update(updater([r, t](int i)->bool { return r > t[i]; }));
	}
}

static void pad(int16_t &last, int16_t x, int16_t y) {
	if (last > x)
		tft.fillRect(x, y, last - x, tft.fontHeight(), TFT_BLUE);
	last = x;
}

static void draw_vi() {
	char buf[32];
	static int16_t last[7];

	tft.setTextFont(0);
	int16_t y = 1;
	tft.setCursor(0, y);

	if (WiFi.status() == WL_CONNECTED)
		strlcpy(buf, cfg.ssid, sizeof(buf));
	else
		strlcpy(buf, "Not connected", sizeof(buf));
	pad(last[0], tft.drawString(buf, 0, y), y);

	y += tft.fontHeight();
	snprintf(buf, sizeof(buf), "Bus: %4.2fV", busvoltage);
	pad(last[1], tft.drawString(buf, 0, y), y);

	y += tft.fontHeight();
	snprintf(buf, sizeof(buf), "Shunt: %4.2fmV", shuntvoltage);
	pad(last[2], tft.drawString(buf, 0, y), y);

	y += tft.fontHeight();
	snprintf(buf, sizeof(buf), "Target: %4.1fV", cfg.presets[tv]);
	pad(last[3], tft.drawString(buf, 0, y), y);

	y += tft.fontHeight();
	tft.setTextFont(4);
	snprintf(buf, sizeof(buf), "%4.2fV", loadvoltage);
	pad(last[4], tft.drawString(buf, 0, y), y);

	y += tft.fontHeight();
	snprintf(buf, sizeof(buf), "%4.2fmA", current_mA);
	pad(last[5], tft.drawString(buf, 0, y), y);

	y += tft.fontHeight();
	snprintf(buf, sizeof(buf), "%4.1fmW", power_mW);
	pad(last[6], tft.drawString(buf, 0, y), y);
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
	debug = cfg.debug;
	bgcolor = debug? TFT_RED: TFT_BLUE;

	tft.init();
	tft.setTextColor(TFT_WHITE, bgcolor);
	tft.fillScreen(bgcolor);
	tft.setCursor(0, 0);
	tft.setRotation(3);

	x9c.begin(X9C_CS, X9C_INC, X9C_UD);

	rssi.colors(TFT_WHITE, bgcolor);
	rssi.init(tft.width() - 21, 0, 20, 20);

	WiFi.mode(WIFI_STA);
	WiFi.hostname(cfg.hostname);

	if (*cfg.ssid) {
		WiFi.setAutoReconnect(true);
		WiFi.begin(cfg.ssid, cfg.password);
	}

	if (WiFi.softAP(cfg.hostname)) {
		DBG(print(F("Connect to SSID: ")));
		DBG(print(cfg.hostname));
		DBG(println(F(" to configure WIFI")));
		dnsServer.start(53, "*", WiFi.softAPIP());
	} else
		ERR(println(F("Error starting softAP")));

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
		ERR(println(F("Error starting mDNS")));

	attachInterrupt(SWITCH, []() { swtch=true; }, FALLING);

	wire.begin(SDA, SCL);
	ina219.begin(&wire);

	timers.setInterval(UPDATE_RSSI, draw_rssi);
	timers.setInterval(UPDATE_VI, draw_vi);
}

void loop() {

	mdns.update();
	server.handleClient();
	dnsServer.processNextRequest();

	if (WiFi.status() == WL_CONNECTED)
		wr = WiFi.RSSI();
	else {
		int i = (millis() / 500);
		rssi.update(updater([i](int b) { return i % 5 == b; }));
		delay(500);
	}

	shuntvoltage = ina219.getShuntVoltage_mV();
	busvoltage = ina219.getBusVoltage_V();
	current_mA = ina219.getCurrent_mA();
	power_mW = ina219.getPower_mW();
	loadvoltage = busvoltage + (shuntvoltage / 1000);

	if (swtch && swtch.changedAfter(500)) {
		tv++;
		if (tv == sizeof(cfg.presets) / sizeof(cfg.presets[0]) || cfg.presets[tv] == 0.0)
			tv = 0;
		draw_vi();
	}
	swtch = false;

	float diff = (busvoltage - cfg.presets[tv]) / busvoltage;
	if (fabs(diff) > 0.015)
		x9c.trimPot(1, diff < 0? X9C_DOWN: X9C_UP);

	timers.run();
}
