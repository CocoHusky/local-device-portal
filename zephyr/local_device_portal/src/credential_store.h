#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <stdbool.h>

void credential_store_init(void);
void credential_store_save(const char *ssid, const char *pass);
void credential_store_clear(void);

const char *credential_store_ssid(void);
const char *credential_store_pass(void);
bool credential_store_has_ssid(void);

#endif
