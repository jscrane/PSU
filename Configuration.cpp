#include <FS.h>
#include <ArduinoJson.h>
#include "Configuration.h"

bool Configuration::read_file(const char *filename) {
  File f = SPIFFS.open(filename, "r");
  if (!f)
    return false;

  DynamicJsonBuffer json(JSON_OBJECT_SIZE(11) + 210);
  JsonObject &root = json.parseObject(f);
  f.close();
  if (!root.success())
    return false;

  configure(root);
  return true;
}

