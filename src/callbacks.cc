#include <callbacks.h>
#include <iostream>

void ignore_transfer(const boost::system::error_code &, size_t) {}

void print_transfer_info(const boost::system::error_code &e, size_t transferred)
{
    if (e) {
        std::cerr << "Transfer error: " << e.message() << "\n";
    } else {
        std::cout << "Transferred " << transferred << " bytes\n";
    }
}
