#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "wizchip_conf.h" // W5100S/W5500 könyvtárból

// Alapértelmezett konfiguráció
extern const wiz_NetInfo default_net_info;

// Függvények deklarációja
void save_to_flash(const wiz_NetInfo* net_info);
void load_from_flash(wiz_NetInfo* net_info);
#endif // CONFIG_H