#include <iostream>

#include <events/hello_responder.h>

discord::hello_responder::hello_responder(discord::api &api) : api{api} {}

void discord::hello_responder::handle(discord::gateway &, gtw_op_recv, const nlohmann::json &json,
                                      const std::string &type)
{
    if (json.is_null())
        return;
    if (type == "MESSAGE_CREATE") {
        std::string channel = json["channel_id"];
        std::string content = json["content"];
        std::string user = json["author"]["username"];

        std::cout << user << ": " << content << " in " << channel << "\n";

        if (content.find("hi") != std::string::npos) {
            /*
            auto success = api->send_message(channel, "hey " + user);
            if (success == api_result::success) {
                std::cout << "Success!\n";
            } else if (success == api_result::failure) {
                std::cout << "Failure\n";
            } else if (success == api_result::rate_limited) {
                std::cout << "Rate limited\n";
            }
             */
        }
    }
}
