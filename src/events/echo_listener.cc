#include <iostream>

#include "echo_listener.h"

void cmd::discord::echo_listener::handle(gtw_op_recv, const nlohmann::json &json,
                                         const std::string &type)
{
    std::cout << "Type: " << type << " ";
    if (!json.is_null())
        std::cout << "json: " << json;
    std::cout << "\n";
}