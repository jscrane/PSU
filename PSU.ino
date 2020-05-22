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

#include "configuration.h"
#include "dbg.h"
#include "label.h"
#include "rssi.h"
#include "stator.h"
#include "smoother.h"

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
const unsigned fgcolor = TFT_CYAN;

class config: public Configuration {
public:
	char ssid[33];
	char password[33];
	char hostname[17];
	bool debug;
	float presets[10];

	void configure(JsonDocument &doc);
} cfg;

void config::configure(JsonDocument &o) {
	strlcpy(ssid, o[F("ssid")] | "", sizeof(ssid));
	strlcpy(password, o[F("password")] | "", sizeof(password));
	strlcpy(hostname, o[F("hostname")] | "", sizeof(hostname));
	debug = o[F("debug")] | false;
	const JsonArray &p = o[F("presets")];
	if (!p.isNull())
		for (int i = 0; i < p.size() && i < sizeof(presets) / sizeof(presets[0]); i++)
			presets[i] = p[i][F("preset")];
}

static const char *config_file = "/config.json";
static const unsigned long UPDATE_RSSI = 500, UPDATE_VI = 250, SAMPLE_VI = 50;
static const unsigned long SWITCH_INTERVAL = 1000;
static const unsigned long UPDATE_CONNECT = 500, CONNECT_TIME = 30000;

static RSSI rssi(tft, 5);
const int rssi_error = 31;
const size_t N = UPDATE_VI / SAMPLE_VI;

static Smoother<N> shunt_mV, bus_V, current_mA, power_mW;
static Label status(tft), bus(tft), shunt(tft), target(tft), V(tft), I(tft), W(tft);
static int tv;
static SimpleTimer timers;
static int connectTimer;

static Stator<bool> swtch;

void ICACHE_RAM_ATTR switch_handler() { swtch = true; }

static void draw_rssi() {
	int r = WiFi.RSSI();
	if (r != rssi_error) {
		const int t[] = {-90, -80, -70, -67, -40};
		rssi.update(updater([r, t](int i)->bool { return r > t[i]; }));
	}
}

static void draw_vi() {
	bus.printf("Bus: %4.2fV", bus_V.get());
	shunt.printf("Shunt: %4.2fmV", shunt_mV.get());
	target.printf("Target: %4.1fV", cfg.presets[tv]);
	V.printf("%4.2fV", bus_V.get() + shunt_mV.get() / 1000);
	I.printf("%4.2fmA", current_mA.get());
	W.printf("%4.1fmW", power_mW.get());
}

static void sample_vi() {
	shunt_mV.add(ina219.getShuntVoltage_mV());
	bus_V.add(ina219.getBusVoltage_V());
	current_mA.add(ina219.getCurrent_mA());
	power_mW.add(ina219.getPower_mW());

	float diff = (bus_V.get() - cfg.presets[tv]) / bus_V.get();
	if (fabs(diff) > 0.015)
		x9c.trimPot(1, diff < 0? X9C_DOWN: X9C_UP);
}

static void captive_portal() {
	WiFi.mode(WIFI_AP);
	if (WiFi.softAP(cfg.hostname)) {
		status.printf("SSID: %s", cfg.hostname);
		DBG(print(F("Connect to SSID: ")));
		DBG(print(cfg.hostname));
		DBG(println(F(" to configure WIFI")));
		dnsServer.start(53, "*", WiFi.softAPIP());
	} else {
		status.draw("Error starting softAP");
		ERR(println(F("Error starting softAP")));
	}
}

static void connecting() {
	if (WiFi.status() == WL_CONNECTED) {
		status.draw(cfg.ssid);
		timers.disable(connectTimer);
		timers.setInterval(UPDATE_RSSI, draw_rssi);
		return;
	}
	unsigned now = millis();
	if (now > CONNECT_TIME) {
		timers.disable(connectTimer);
		captive_portal();
		return;
	}
	const char busy[] = "|/-\\";
	int i = (now / UPDATE_RSSI);
	status.printf("Connecting %c", busy[i % 4]);
	rssi.update(updater([i](int b) { return i % 5 == b; }));
}

void setup() {
	Serial.begin(TERMINAL_SPEED);
	Serial.println(F("Booting!"));
	Serial.println(F(VERSION));

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
	bgcolor = debug? TFT_RED: TFT_NAVY;

	tft.init();
	tft.setTextColor(fgcolor, bgcolor);
	tft.fillScreen(bgcolor);
	tft.setCursor(0, 0);
	tft.setRotation(3);

	int y = 1;
	status.setPosition(0, y);
	status.setColor(fgcolor, bgcolor);
	y += status.setFont(1);

	bus.setPosition(0, y);
	bus.setColor(fgcolor, bgcolor);
	y += bus.setFont(1);

	shunt.setPosition(0, y);
	shunt.setColor(fgcolor, bgcolor);
	y += shunt.setFont(1);

	target.setPosition(0, y);
	target.setColor(fgcolor, bgcolor);
	y += target.setFont(1);

	V.setPosition(0, y);
	V.setColor(TFT_GREEN, bgcolor);
	y += V.setFont(4);

	I.setPosition(0, y);
	I.setColor(TFT_YELLOW, bgcolor);
	y += I.setFont(4);

	W.setPosition(0, y);
	W.setColor(TFT_PINK, bgcolor);
	y += W.setFont(4);

	rssi.setColor(TFT_WHITE, bgcolor);
	rssi.setBounds(tft.width() - 21, 0, 20, 20);

	x9c.begin(X9C_CS, X9C_INC, X9C_UD);

	WiFi.mode(WIFI_STA);
	WiFi.hostname(cfg.hostname);

	if (*cfg.ssid) {
		WiFi.setAutoReconnect(true);
		WiFi.begin(cfg.ssid, cfg.password);
	} else
		captive_portal();

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

	attachInterrupt(digitalPinToInterrupt(SWITCH), switch_handler, RISING);

	wire.begin(SDA, SCL);
	ina219.begin(&wire);

	timers.setInterval(UPDATE_VI, draw_vi);
	timers.setInterval(SAMPLE_VI, sample_vi);
	connectTimer = timers.setInterval(UPDATE_CONNECT, connecting);
}

void loop() {
	mdns.update();
	server.handleClient();
	dnsServer.processNextRequest();
	timers.run();

	if (swtch && swtch.changedAfter(SWITCH_INTERVAL)) {
		tv++;
		if (tv == sizeof(cfg.presets) / sizeof(cfg.presets[0]) || cfg.presets[tv] == 0.0)
			tv = 0;
		draw_vi();
	}
	swtch = false;
}
