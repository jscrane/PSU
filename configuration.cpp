#include <FS.h>
#include <ArduinoJson.h>
#include "configuration.h"
#include "dbg.h"

bool Configuration::read_file(const char *filename) {
	File f = SPIFFS.open(filename, "r");
	if (!f)
		return false;

	DynamicJsonDocument doc(JSON_OBJECT_SIZE(11) + 210);
	auto error = deserializeJson(doc, f);
	f.close();
	if (error) {
		ERR(print(F("config: ")));
		ERR(println(error.c_str()));
		return false;
	}

	configure(doc);
	return true;
}

