#ifndef __XMLPARSER_H__
#define __XMLPARSER_H__

#include <algorithm>
#include <cctype>
#include <iostream>
#include <vector>

#include "StringHelpers.h"

std::string get_tag_text(const std::string& xml, const std::string& tag_name_no_prefix);

std::vector<std::string> split_ws(const std::string& s);

std::string pick_best_device_service(const std::vector<std::string>& urls);

#endif // __XMLPARSER_H__
