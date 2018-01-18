#include <discord/member.h>

bool discord::operator<(const member &lhs, const member &rhs)
{
    return lhs.user < rhs.user;
}

void discord::to_json(nlohmann::json &json, const discord::member &m)
{
    json = {{"user", m.user}, {"nick", m.nick}, {"joined_at", m.joined_at}};
}

void discord::from_json(const nlohmann::json &json, discord::member &m)
{
    m.user = json.at("user").get<discord::user>();
    m.joined_at = json.at("joined_at").get<std::string>();
    auto nick = json.find("nick");
    if (nick != json.end() && !nick.value().is_null())
        m.nick = nick.value();
}
