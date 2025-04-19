#ifndef CONFIG_H
#define CONFIG_H

// Függvények deklarációja
void save_to_flash(const wiz_NetInfo* net_info);
void load_from_flash(wiz_NetInfo* net_info);

#endif // CONFIG_H