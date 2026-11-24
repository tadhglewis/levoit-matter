#pragma once

void device_info_install();
void device_info_on_local_config_write(void (*handler)(bool disabled));
bool device_info_set_local_config_disabled(bool disabled);
const char *device_info_product_url();
