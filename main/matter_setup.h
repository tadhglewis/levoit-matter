#pragma once

#include "purifier_map.h"

#include <esp_err.h>

esp_err_t matter_setup_start();
bool matter_publish(const levoit_matter::MatterView &view);
void matter_factory_reset();
bool matter_network_ready();
