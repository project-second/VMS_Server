#ifndef __SECURITYUTILS_H__
#define __SECURITYUTILS_H__

#include <string>
#include <sstream>
#include <iomanip>

#include <openssl/evp.h>

std::string md5_hex(const std::string& input);
std::string sha256_hex(const std::string& input);

#endif // __SECURITYUTILS_H__
