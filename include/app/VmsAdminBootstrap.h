#ifndef VMS_ADMIN_BOOTSTRAP_H
#define VMS_ADMIN_BOOTSTRAP_H

#include <string>

class VmsAdminBootstrap {
public:
    enum class Status {
        Created,
        AdminAlreadyExists,
        LoginIdAlreadyExists,
        AdminRoleMissing,
        InternalError
    };

    Status createAdmin(
        const std::string& loginId,
        const std::string& name,
        const std::string& password) const;
};

#endif // VMS_ADMIN_BOOTSTRAP_H
