#include <iostream>

#include <events/echo_listener.h>

void discord::echo_listener::handle(discord::gateway &, gtw_op_recv, const nlohmann::json &json,
                                    const std::string &type)
{
    std::cout << "Type: " << type << " ";
    if (!json.is_null())
        std::cout << "json: " << json;
    std::cout << "\n";
}
