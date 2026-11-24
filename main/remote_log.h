#pragma once

#include <cstddef>

void remote_log_install();
size_t remote_log_copy(char *out, size_t max);
size_t remote_log_capacity();
