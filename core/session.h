#pragma once

#include <QString>

#include "user.h"

namespace app::core {

class Session {
public:
    static Session& instance();

    void setCurrentUser(const core::User& user);
    bool hasUser() const;
    const core::User& currentUser() const;
    QString actorName() const;
    void clear();

private:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    std::optional<core::User> m_currentUser;
};

} // namespace app::core