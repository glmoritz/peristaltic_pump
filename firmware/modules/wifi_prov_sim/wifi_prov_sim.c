/*
 * wifi_prov stub for native_sim. The network is the host's (offloaded
 * sockets), so "provisioning" is announcing CONNECTED after a short delay.
 * Credentials are kept in RAM only, so the shell/MQTT commands can be tried.
 */

#include "wifi_prov.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wifi_prov_sim, LOG_LEVEL_INF);

#define SIM_CONNECT_DELAY_MS 500
#define SIM_IP_ADDR          "127.0.0.1"

static wifi_prov_cb_t app_cb;
static enum wifi_prov_state state = WIFI_PROV_IDLE;
static char ssid[33] = "native-sim";

void wifi_prov_init(wifi_prov_cb_t cb)
{
	app_cb = cb;
	state = WIFI_PROV_IDLE;
}

void wifi_prov_set_fields(const struct wifi_prov_field *fields, size_t count,
			  wifi_prov_field_get_t get, wifi_prov_field_set_t set)
{
	ARG_UNUSED(fields);
	ARG_UNUSED(count);
	ARG_UNUSED(get);
	ARG_UNUSED(set);
}

void wifi_prov_start(void)
{
	state = WIFI_PROV_CONNECTING;
	k_msleep(SIM_CONNECT_DELAY_MS);
	state = WIFI_PROV_CONNECTED;
	LOG_INF("Simulated Wi-Fi connected (host network)");
	if (app_cb) {
		app_cb(WIFI_PROV_CONNECTED, SIM_IP_ADDR);
	}
}

int wifi_prov_request_portal(void)
{
	LOG_WRN("no portal on native_sim");
	return 0;
}

int wifi_prov_store_credentials(const char *new_ssid, const char *pass)
{
	ARG_UNUSED(pass);
	if (new_ssid) {
		if (new_ssid[0] == '\0' || strlen(new_ssid) >= sizeof(ssid)) {
			return -EINVAL;
		}
		strcpy(ssid, new_ssid);
	}
	return 0;
}

enum wifi_prov_state wifi_prov_get_state(void)
{
	return state;
}

const char *wifi_prov_get_ip(void)
{
	return (state == WIFI_PROV_CONNECTED) ? SIM_IP_ADDR : "";
}

const char *wifi_prov_get_ssid(void)
{
	return ssid;
}

const char *wifi_prov_get_stored_ssid(void)
{
	return ssid;
}

int wifi_prov_get_rssi(void)
{
	return -55;
}

void wifi_prov_clear_credentials(void)
{
}
