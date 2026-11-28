/*
 * Network settings other than the Wi-Fi credentials (those belong to
 * wifi_prov): MQTT broker, topic, HA discovery, NTP. Stored under net/<key>.
 *
 * Set from the shell, from MQTT commands or from the provisioning portal;
 * read by the MQTT thread, which reconnects when netcfg_generation() moves.
 */

#ifndef PUMP_NETCFG_H_
#define PUMP_NETCFG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct netcfg {
	char mqtt_host[64];   /* "" = MQTT off */
	uint16_t mqtt_port;
	char mqtt_user[32];
	char mqtt_pass[64];
	char topic[32];       /* tele/<topic>/..., cmnd/<topic>/... */
	char disc_prefix[32]; /* HA discovery prefix */
	bool discovery;
	char ntp[64];
};

/* Load (after settings_subsys_init). Defaults the topic to condpump_<id>. */
int netcfg_init(void);

/* Consistent copy, for the MQTT thread. */
void netcfg_get(struct netcfg *out);

/* Keys: mqtt_host mqtt_port mqtt_user mqtt_pass topic disc_prefix discovery
 * ntp. Validates, stores, bumps the generation. -ENOENT / -EINVAL / storage
 * error. */
int netcfg_set(const char *key, const char *value);

/* Current value as text ("****" for the password unless reveal). */
int netcfg_print(const char *key, char *buf, size_t len, bool reveal);

uint32_t netcfg_generation(void);

/* Short hex id from the MAC / hardware id, e.g. "a1b2c3". */
const char *netcfg_device_id(void);

#endif /* PUMP_NETCFG_H_ */
