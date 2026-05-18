#ifndef __DB_RUNTIME_HPP__
#define __DB_RUNTIME_HPP__

#include <string>

#include "DbTemplate.hpp"

namespace AppDb {

void configure(const std::string& conninfo, const std::string& mapper_path);
DbTemplate* instance();

bool insert(const std::string& statement, const DbTemplate::ParamMap& params);
bool update(const std::string& statement, const DbTemplate::ParamMap& params);
bool remove(const std::string& statement, const DbTemplate::ParamMap& params);
DbTemplate::TypedRowSet select(const std::string& statement, const DbTemplate::ParamMap& params);

} // namespace AppDb

#endif // __DB_RUNTIME_HPP__
