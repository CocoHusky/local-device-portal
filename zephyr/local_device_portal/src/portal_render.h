#ifndef PORTAL_RENDER_H
#define PORTAL_RENDER_H

#include <stddef.h>

void portal_render_setup(char *buf, size_t cap);
void portal_render_manual(char *buf, size_t cap, const char *error);
void portal_render_scan(char *buf, size_t cap);
void portal_render_pick(char *buf, size_t cap, const char *ssid,
			const char *error);
void portal_render_success(char *buf, size_t cap, const char *ssid);
void portal_render_handoff(char *buf, size_t cap);
void portal_render_dashboard(char *buf, size_t cap);
void portal_render_reset(char *buf, size_t cap);

#endif
