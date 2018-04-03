#include <stdlib.h>

#include "common/cs_dbg.h"

#include "mgos_hal.h"
#include "mgos_wifi.h"

#include "esp32_bt_gatts.h"

/* Note: UUIDs below ar in reverse, because that's how the ESP wants them. */
static const esp_bt_uuid_t mos_wifi_svc_uuid = {
  .len = ESP_UUID_LEN_128,
  .uuid.uuid128 = {
    /* _mOS_WIFI_SVC_ID_, 6977cc94-731c-4d33-8498-4a8842c3d224 */
    0x24, 0xd2, 0xc3, 0x42, 0x88, 0x4a, 0x98, 0x84, 0x33, 0x4d, 0x1c, 0x73,
    0x94, 0xcc, 0x77, 0x69,
  }
};

static const esp_bt_uuid_t mos_wifi_ctrl_uuid = {
  .len = ESP_UUID_LEN_128,
  .uuid.uuid128 = {
    /* _mOS_WIFI_ctrl__0, d9e67e00-164d-4577-a8fb-d778406e06db */
    0xdb, 0x06, 0x6e, 0x40, 0x78, 0xd7, 0xfb, 0xa8, 0x77, 0x45, 0x4d, 0x16,
    0x00, 0x7e, 0xe6, 0xd9,
  }
};
static uint16_t mos_wifi_ctrl_ah;

static const esp_bt_uuid_t mos_wifi_data_uuid = {
  .len = ESP_UUID_LEN_128,
  .uuid.uuid128 = {
    /* _mOS_WIFI_data__1, 8cb22813-75f0-4f03-967f-0c289543a82d */
    0x2d, 0xa8, 0x43, 0x95, 0x28, 0x0c, 0x7f, 0x96, 0x03, 0x4f, 0x0f, 0x75,
    0x13, 0x28, 0xb2, 0x8c,
  }
};
static uint16_t mos_wifi_data_ah;

const esp_gatts_attr_db_t mos_wifi_gatt_db[5] = {
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
    (uint8_t *) &char_prop_read_write}},
  {{ESP_GATT_RSP_BY_APP},
   {ESP_UUID_LEN_128, (uint8_t *) mos_wifi_ctrl_uuid.uuid.uuid128,
    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 0, 0, NULL}},

  /* data */ 
  {{ESP_GATT_AUTO_RSP},
   {ESP_UUID_LEN_16, (uint8_t *) &char_decl_uuid, ESP_GATT_PERM_READ, 1, 1,
    (uint8_t *) &char_prop_read}},
  {{ESP_GATT_RSP_BY_APP},
   {ESP_UUID_LEN_128, (uint8_t *) mos_wifi_data_uuid.uuid.uuid128,
    ESP_GATT_PERM_READ, 0, 0, NULL}},
};

enum bt_wifi_svc_state {
  BT_WIFI_STATE_IDLE = 0,
  BT_WIFI_STATE_SCANNING = 1,
  BT_WIFI_STATE_RESULTS = 2,
};

struct bt_wifi_svc_data_s {
  enum bt_wifi_svc_state state;
  int num_res;
  struct mgos_wifi_scan_result *res;
  int idx;
};
static struct bt_wifi_svc_data_s svc_data;

static void wifi_scan_cb(int num_res, struct mgos_wifi_scan_result *res,
                         void *arg)
{
  if (svc_data.state != BT_WIFI_STATE_SCANNING) return;

  if (num_res <= 0) {
    if (svc_data.res) {
      free(svc_data.res);
      svc_data.res = NULL;
    }
    svc_data.state = BT_WIFI_STATE_IDLE;
    return;
  }

  if (svc_data.res) {
    svc_data.res = realloc(svc_data.res,
                            sizeof(struct mgos_wifi_scan_result) * num_res);
  } else {
    svc_data.res = malloc(sizeof(struct mgos_wifi_scan_result) * num_res);
  }

  if (svc_data.res == NULL) {
    svc_data.state = BT_WIFI_STATE_IDLE;
    return;
  }

  memcpy(svc_data.res, res, sizeof(struct mgos_wifi_scan_result) * num_res);
  svc_data.num_res = num_res;
  svc_data.state = BT_WIFI_STATE_RESULTS;
  svc_data.idx = 0;

  (void) arg;
}

static bool mgos_bt_svc_wifi_ev(struct esp32_bt_session *bs,
                                esp_gatts_cb_event_t ev,
                                esp_ble_gatts_cb_param_t *ep)
{
  bool ret = false;
  struct esp32_bt_connection *bc = NULL;
  if (bs != NULL) { /* CREAT_ATTR_TAB is not associated with any session. */
    bc = bs->bc;
  }

  switch (ev) {
    case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
      const struct gatts_add_attr_tab_evt_param *p = &ep->add_attr_tab;
      mos_wifi_ctrl_ah = p->handles[2];
      mos_wifi_data_ah = p->handles[4];
      break;
    }
    case ESP_GATTS_READ_EVT: {
      const struct gatts_read_evt_param *p = &ep->read;
      if (svc_data.state != BT_WIFI_STATE_RESULTS &&
          p->handle == mos_wifi_data_ah)
        break;

      if (p->handle == mos_wifi_ctrl_ah) {
        esp_gatt_rsp_t rsp = {.attr_value = {.handle = mos_wifi_ctrl_ah,
                                             .offset = 0,
                                             .len = 1,
                                             .value = {'0' + svc_data.state}}};
        esp_ble_gatts_send_response(bc->gatt_if, bc->conn_id, p->trans_id,
                                    ESP_GATT_OK, &rsp);
        ret = true;
      } else if (p->handle == mos_wifi_data_ah) {
        if (!svc_data.res) break;
        uint16_t len = sizeof(struct mgos_wifi_scan_result) * svc_data.num_res;
        if (p->offset > len) break;
        uint16_t to_send = bc->mtu - 1;
        if (len - p->offset < to_send) {
          to_send = len - p->offset;
        }
        esp_gatt_rsp_t rsp = {.attr_value = {.handle = mos_wifi_data_ah,
                                             .offset = p->offset,
                                             .len = to_send}};
        memcpy(rsp.attr_value.value,
               ((unsigned char *)svc_data.res) + p->offset, to_send);
        esp_ble_gatts_send_response(bc->gatt_if, bc->conn_id, p->trans_id,
                                    ESP_GATT_OK, &rsp);
        ret = true;
      }
    }
    case ESP_GATTS_WRITE_EVT: {
      const struct gatts_write_evt_param *p = &ep->write;
      if (p->handle == mos_wifi_ctrl_ah) {
        if (p->value[0] == '0' && svc_data.state == BT_WIFI_STATE_SCANNING) {
          svc_data.state = BT_WIFI_STATE_IDLE;
        } else if (p->value[0] == '0' &&
                   svc_data.state == BT_WIFI_STATE_RESULTS) {
          svc_data.state = BT_WIFI_STATE_IDLE;
          if (svc_data.res) {
            free(svc_data.res);
            svc_data.res = NULL;
          }
        } else if (p->value[0] == '1' &&
                   svc_data.state == BT_WIFI_STATE_IDLE) {
          svc_data.state = BT_WIFI_STATE_SCANNING;
          mgos_wifi_scan(wifi_scan_cb, NULL);
        } else if (p->value[0] == '1' &&
                   svc_data.state == BT_WIFI_STATE_RESULTS) {
          if (svc_data.res) {
            free(svc_data.res);
            svc_data.res = NULL;
          }
          svc_data.state = BT_WIFI_STATE_SCANNING;
          mgos_wifi_scan(wifi_scan_cb, NULL);
        }
        ret = true;
      }
    }
    default:
      break;
  }
  return ret;
}

bool mgos_bt_service_wifi_init(void) {
  if (mgos_sys_config_get_bt_wifi_svc_enable()) {
    memset(&svc_data, 0, sizeof(svc_data));
    mgos_bt_gatts_register_service(mos_wifi_gatt_db,
                                   ARRAY_SIZE(mos_wifi_gatt_db),
                                   mgos_bt_svc_wifi_ev);
  }
  return true;
}
