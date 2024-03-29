#include <stdlib.h>

#include "common/cs_dbg.h"
#include "common/json_utils.h"
#include "common/mg_str.c"

#include "mgos_hal.h"
#include "mgos_wifi.h"

#include "esp32_bt_gatts.h"
#include "rom/ets_sys.h"

#include "mbuf_util.h"

/* Note: UUIDs below ar in reverse, because that's how the ESP wants them. */
static const esp_bt_uuid_t mos_wifi_svc_uuid = {
  .len = ESP_UUID_LEN_128,
  .uuid.uuid128 = {
    /* 776f37fa-371b-11e8-b467-0ed5f89f718b */
    0x8b, 0x71, 0x9f, 0xf8, 0xd5, 0x0e, 0x67, 0xb4, 0xe8, 0x11, 0x1b, 0x37,
    0xfa, 0x37, 0x6f, 0x77,
  },
};

static const esp_bt_uuid_t mos_wifi_ctrl_uuid = {
  .len = ESP_UUID_LEN_128,
  .uuid.uuid128 = {
    /* 776f3cbe-371b-11e8-b467-0ed5f89f718b */
    0x8b, 0x71, 0x9f, 0xf8, 0xd5, 0x0e, 0x67, 0xb4, 0xe8, 0x11, 0x1b, 0x37,
    0xbe, 0x3c, 0x6f, 0x77,
  },
};
static uint16_t mos_wifi_ctrl_ah;
static uint16_t mos_wifi_ctrl_cc_ah;

static const esp_bt_uuid_t mos_wifi_data_uuid = {
  .len = ESP_UUID_LEN_128,
  .uuid.uuid128 = {
    /* 776f3f16-371b-11e8-b467-0ed5f89f718b */
    0x8b, 0x71, 0x9f, 0xf8, 0xd5, 0x0e, 0x67, 0xb4, 0xe8, 0x11, 0x1b, 0x37,
    0x16, 0x3f, 0x6f, 0x77,
  },
};
static uint16_t mos_wifi_data_ah;

static const uint8_t char_prop_rwn = ESP_GATT_CHAR_PROP_BIT_WRITE |
                                     ESP_GATT_CHAR_PROP_BIT_READ |
                                     ESP_GATT_CHAR_PROP_BIT_NOTIFY;

const esp_gatts_attr_db_t mos_wifi_gatt_db[6] = {
  {
    .attr_control = {.auto_rsp = ESP_GATT_AUTO_RSP},
    .att_desc = {
      .uuid_length = ESP_UUID_LEN_16,
      .uuid_p = (uint8_t *) &primary_service_uuid,
      .perm = ESP_GATT_PERM_READ,
      .max_length = ESP_UUID_LEN_128,
      .length = ESP_UUID_LEN_128,
      .value = (uint8_t *) mos_wifi_svc_uuid.uuid.uuid128,
    },
  },

  /* ctrl */ 
  {{ESP_GATT_AUTO_RSP},
   {ESP_UUID_LEN_16, (uint8_t *) &char_decl_uuid, ESP_GATT_PERM_READ, 1, 1,
    (uint8_t *) &char_prop_rwn}},
  {{ESP_GATT_RSP_BY_APP},
   {ESP_UUID_LEN_128, (uint8_t *) mos_wifi_ctrl_uuid.uuid.uuid128,
    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 0, 0, NULL}},
  {{ESP_GATT_RSP_BY_APP},
   {ESP_UUID_LEN_16, (uint8_t *) &char_client_config_uuid,
    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 0, 0, NULL}},

  /* data */ 
  {{ESP_GATT_AUTO_RSP},
   {ESP_UUID_LEN_16, (uint8_t *) &char_decl_uuid, ESP_GATT_PERM_READ, 1, 1,
    (uint8_t *) &char_prop_read}},
  {{ESP_GATT_RSP_BY_APP},
   {ESP_UUID_LEN_128, (uint8_t *) mos_wifi_data_uuid.uuid.uuid128,
    ESP_GATT_PERM_READ, 0, 0, NULL}},
};

struct bt_wifi_svc_data {
  bool notify;
  struct esp32_bt_connection *bc;
};

enum wifi_state {
  WIFI_STATE_IDLE = 0,
  WIFI_STATE_SCANNING = 1,
  WIFI_STATE_RESULTS = 2,
};

struct wifi_data {
  enum wifi_state state;
  struct mbuf results;
};
static struct wifi_data wifi = {0};

static void wifi_scan_cb(int num_res, struct mgos_wifi_scan_result *res,
                         void *arg)
{
  if (wifi.state != WIFI_STATE_SCANNING) return;

  struct bt_wifi_svc_data *sd = (struct bt_wifi_svc_data *) arg;

  mbuf_append_str(&wifi.results, "[");

  for (int i = 0; i < num_res; i++, res++) {
    struct mg_str ssid = mg_mk_str(res->ssid);
    char *auth_mode;

    switch (res->auth_mode) {
      case MGOS_WIFI_AUTH_MODE_OPEN:
        auth_mode = "open";
        break;
      case MGOS_WIFI_AUTH_MODE_WEP:
        auth_mode = "wep";
        break;
      case MGOS_WIFI_AUTH_MODE_WPA_PSK:
        auth_mode = "wpa-psk";
        break;
      case MGOS_WIFI_AUTH_MODE_WPA2_PSK:
        auth_mode = "wpa2-psk";
        break;
      case MGOS_WIFI_AUTH_MODE_WPA_WPA2_PSK:
        auth_mode = "wpa/wpa2-psk";
        break;
      case MGOS_WIFI_AUTH_MODE_WPA2_ENTERPRISE:
        auth_mode = "wpa2-enterprise";
        break;
      default:
        auth_mode = "unknown";
        break;
    }

    mbuf_append_str(&wifi.results, "{\"ssid\":");
    mg_json_emit_str(&wifi.results, ssid, 1);
    mbuf_append_fmt(&wifi.results, ",\"bssid\":\"" MACSTR "\"",
                    MAC2STR(res->bssid));
    mbuf_append_fmt(&wifi.results, ",\"auth_mode\":\"%s\"", auth_mode);
    mbuf_append_fmt(&wifi.results, ",\"channel\":%d", res->channel);
    mbuf_append_fmt(&wifi.results, ",\"rssi\":%d}%s", res->rssi,
                    (i + 1 < num_res) ? "," : "");
  }

  mbuf_append_str(&wifi.results, "]");
  mbuf_trim(&wifi.results);

  wifi.state = WIFI_STATE_RESULTS;
  if (sd->notify) {
    esp_ble_gatts_send_indicate(sd->bc->gatt_if, sd->bc->conn_id,
                                mos_wifi_ctrl_ah, 1, (uint8_t *) "2", false);
  }
}

static bool mgos_bt_svc_wifi_ev(struct esp32_bt_session *bs,
                                esp_gatts_cb_event_t ev,
                                esp_ble_gatts_cb_param_t *ep)
{
  bool ret = false;
  struct bt_wifi_svc_data *sd = NULL;
  struct esp32_bt_connection *bc = NULL;
  if (bs != NULL) { /* CREAT_ATTR_TAB is not associated with any session. */
    bc = bs->bc;
    sd = (struct bt_wifi_svc_data *) bs->user_data;
  }

  switch (ev) {
    case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
      const struct gatts_add_attr_tab_evt_param *p = &ep->add_attr_tab;
      mos_wifi_ctrl_ah = p->handles[2];
      mos_wifi_ctrl_cc_ah = p->handles[3];
      mos_wifi_data_ah = p->handles[5];
      break;
    }
    case ESP_GATTS_CONNECT_EVT: {
      sd = (struct bt_wifi_svc_data *) calloc(1, sizeof(*sd));
      if (sd == NULL) break;
      sd->bc = bs->bc;
      bs->user_data = sd;
      break;
    }
    case ESP_GATTS_READ_EVT: {
      const struct gatts_read_evt_param *p = &ep->read;
      if (p->handle == mos_wifi_ctrl_ah) {
        esp_gatt_rsp_t rsp = {.attr_value = {.handle = mos_wifi_ctrl_ah,
                                             .offset = 0,
                                             .len = 1,
                                             .value = {'0' + wifi.state}}};
        esp_ble_gatts_send_response(bc->gatt_if, bc->conn_id, p->trans_id,
                                    ESP_GATT_OK, &rsp);
        ret = true;
      } else if (p->handle == mos_wifi_data_ah) {
        if (wifi.state != WIFI_STATE_RESULTS) break;
        if (p->offset > wifi.results.len) break;
        uint16_t to_send = bc->mtu - 1;
        if (wifi.results.len - p->offset < to_send) {
          to_send = wifi.results.len - p->offset;
        }
        esp_gatt_rsp_t rsp = {.attr_value = {.handle = mos_wifi_data_ah,
                                             .offset = p->offset,
                                             .len = to_send}};
        memcpy(rsp.attr_value.value, (uint8_t *)wifi.results.buf + p->offset,
               to_send);
        esp_ble_gatts_send_response(bc->gatt_if, bc->conn_id, p->trans_id,
                                    ESP_GATT_OK, &rsp);
        ret = true;
      }
    }
    case ESP_GATTS_WRITE_EVT: {
      const struct gatts_write_evt_param *p = &ep->write;
      if (p->handle == mos_wifi_ctrl_ah && p->len == 1) {
        if (p->value[0] == '0') {
          mbuf_free(&wifi.results);
          wifi.state = WIFI_STATE_IDLE;
          ret = true;
        } else if (p->value[0] == '1') {
          mbuf_free(&wifi.results);
          mgos_wifi_scan(wifi_scan_cb, sd);
          wifi.state = WIFI_STATE_SCANNING;
          ret = true;
        }
      } else if (p->handle == mos_wifi_ctrl_cc_ah && p->len == 2) {
        uint16_t value = p->value[1] << 8 | p->value[0];
        sd->notify = !!(value & 0x01);
      }
      break;
    }
    case ESP_GATTS_DISCONNECT_EVT: {
      if (sd != NULL) {
        free(sd);
        bs->user_data = NULL;
      }
      wifi.state = WIFI_STATE_IDLE;
      break;
    }
    default:
      break;
  }
  return ret;
}

bool mgos_bt_service_wifi_init(void) {
  if (mgos_sys_config_get_bt_wifi_svc_enable()) {
    mbuf_init(&wifi.results, 0);
    mgos_bt_gatts_register_service(mos_wifi_gatt_db,
                                   ARRAY_SIZE(mos_wifi_gatt_db),
                                   mgos_bt_svc_wifi_ev);
  }
  return true;
}
