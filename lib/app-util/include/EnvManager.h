#ifndef __ENVMANAGER_H__
#define __ENVMANAGER_H__

#include <cstdlib>
#include <optional>
#include <string>

bool readEnvIntStrict(const char* key, int& out);

std::optional<std::string> readEnvStringStrict(const char* key);

#endif // __ENVMANAGER_H__
