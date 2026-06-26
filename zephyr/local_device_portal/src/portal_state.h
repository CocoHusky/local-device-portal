#ifndef PORTAL_STATE_H
#define PORTAL_STATE_H

#include <stdbool.h>

void portal_state_init(void);

const char *portal_state_host(void);
const char *portal_state_dashboard_url(void);

void portal_state_request_handoff(void);
bool portal_state_take_handoff_request(void);

void portal_state_request_reboot(void);
bool portal_state_take_reboot_request(void);

#endif
