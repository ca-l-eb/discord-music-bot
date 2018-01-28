#ifndef CMD_CALLBACKS_H
#define CMD_CALLBACKS_H

#include <boost/system/error_code.hpp>
#include <functional>

using data_cb = std::function<void(const boost::system::error_code &, const uint8_t *, size_t)>;

using transfer_cb = std::function<void(const boost::system::error_code &, size_t)>;

using error_cb = std::function<void(const boost::system::error_code &)>;

void ignore_transfer(const boost::system::error_code &, size_t);
void print_transfer_info(const boost::system::error_code &e, size_t transferred);

#endif
