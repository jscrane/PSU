#ifndef __CONFIGURATION_H__
#define __CONFIGURATION_H__

class Configuration {
public:
  bool read_file(const char *filename);

protected:
  virtual void configure(class JsonObject &root) = 0;
};

#endif
