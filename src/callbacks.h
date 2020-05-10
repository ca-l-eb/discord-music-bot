#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <boost/system/error_code.hpp>
#include <functional>
#include <nlohmann/json.hpp>

using data_cb = std::function<void(const boost::system::error_code &, const uint8_t *, size_t)>;
using transfer_cb = std::function<void(const boost::system::error_code &, size_t)>;
using error_cb = std::function<void(const boost::system::error_code &)>;
using json_cb = std::function<void(const boost::system::error_code &, const nlohmann::json &)>;
using void_cb = std::function<void()>;

void ignore_transfer(const boost::system::error_code &, size_t);
void print_transfer_info(const boost::system::error_code &e, size_t transferred);

#endif
