#include <spdlog/spdlog.h>

#include "callbacks.h"

void ignore_transfer(const boost::system::error_code &, size_t) {}

void print_transfer_info(const boost::system::error_code &e, size_t transferred)
{
    if (e) {
        SPDLOG_ERROR("Tranfer error: {}", e.message());
    } else {
        SPDLOG_DEBUG("Transferred {} bytes", transferred);
    }
}
