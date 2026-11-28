/*
 * Wi-Fi provisioning: station with credentials in settings (wifi/ssid,
 * wifi/pass), captive portal on an open AP "<name>-XXXXXX" at 192.168.4.1.
 *
 * Derived from hello_eink's module. Additions: the portal can carry extra
 * application fields (e.g. the MQTT broker), it times out when stored
 * credentials exist, and it can be requested for the next boot.
 */

#ifndef WIFI_PROV_H_
#define WIFI_PROV_H_

#include <stdbool.h>
#include <stddef.h>
#include <zephyr/kernel.h>

enum wifi_prov_state {
	WIFI_PROV_IDLE = 0,
	WIFI_PROV_CONNECTING,
	WIFI_PROV_CONNECTED,
	WIFI_PROV_DISCONNECTED,
	WIFI_PROV_AP_ACTIVE,
};

/** State-change callback: called from the net management thread. */
typedef void (*wifi_prov_cb_t)(enum wifi_prov_state state, const char *info);

/** An extra input on the portal form, owned by the application. */
struct wifi_prov_field {
	const char *key;   /* form field name */
	const char *label;
	size_t maxlen;
	bool secret;       /* password input; left empty = keep the old value */
};

/** Current value for the form (not called for secret fields). */
typedef void (*wifi_prov_field_get_t)(const char *key, char *buf, size_t len);
/** Value submitted with the form. */
typedef void (*wifi_prov_field_set_t)(const char *key, const char *value);

void wifi_prov_init(wifi_prov_cb_t cb);

/** Optional, before wifi_prov_start(). The array must stay valid. */
void wifi_prov_set_fields(const struct wifi_prov_field *fields, size_t count,
			  wifi_prov_field_get_t get, wifi_prov_field_set_t set);

/**
 * Connect with the stored credentials, or run the portal (see
 * CONFIG_WIFI_PROV_AP_TIMEOUT_S). Blocks until connected: call it from a
 * thread of its own. Later drops are retried by the module's own thread.
 */
void wifi_prov_start(void);

/** Open the portal at the next boot (the caller reboots). */
int wifi_prov_request_portal(void);

/** Store new credentials; NULL keeps the current value. Applied at the
 *  next connection attempt (reboot). */
int wifi_prov_store_credentials(const char *ssid, const char *pass);

enum wifi_prov_state wifi_prov_get_state(void);
const char *wifi_prov_get_ip(void);
const char *wifi_prov_get_ssid(void);        /* current (STA or AP) */
const char *wifi_prov_get_stored_ssid(void);
int wifi_prov_get_rssi(void);                /* dBm, 0 if unknown */
void wifi_prov_clear_credentials(void);

#endif /* WIFI_PROV_H_ */
