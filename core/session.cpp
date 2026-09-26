#include "session.h"

#include <optional>

namespace app::core {

Session& Session::instance()
{
    static Session s;
    return s;
}

void Session::setCurrentUser(const core::User& user)
{
    m_currentUser = user;
}

bool Session::hasUser() const
{
    return m_currentUser.has_value();
}

const core::User& Session::currentUser() const
{
    static const core::User emptyUser;
    return m_currentUser.value_or(emptyUser);
}

QString Session::actorName() const
{
    if (m_currentUser.has_value()) {
        return m_currentUser->name;
    }
    return QStringLiteral("desktop");
}

void Session::clear()
{
    m_currentUser.reset();
}

} // namespace app::core