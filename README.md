# WiFi scanning over Bluetooth GATT Service

## Overview

This library provides a way to scan for Wireless networks over Generic Attribute Service (GATT) Bluetooth Low-Energy (BLE) service.

The service is designed to be usable with any generic BLE mobile app that supports GATT, e.g. BLE Scanner ([Android](https://play.google.com/store/apps/details?id=com.macdom.ble.blescanner), [iOS](https://itunes.apple.com/us/app/ble-scanner-4-0/id1221763603)).

*Note*: Default BT configuration is permissive. See https://github.com/mongoose-os-libs/bt-common#security for a better idea.

## Attribute description

The service UUID is `776f37fa-371b-11e8-b467-0ed5f89f718b`.

The service defines 2 characteristics (attributes):

  * `776f3cbe-371b-11e8-b467-0ed5f89f718b` (ctrl) - a read/write/notify attribute for controlling the state.
    * `0` - Stop/Idle
    * `1` - Scan/Scanning
    * `2` - Results ready
  * `776f3f16-371b-11e8-b467-0ed5f89f718b` (data) - a read-only attribute for reading results.  Data is in json format.

## Example - scanning for networks
  * Subscribe to notifications on the `ctrl` attribute
  * Write a `1` to the `ctrl` attribute
  * Wait for a notification
  * Read `data` attribute
    * Example (prettyfied) data:
      ```json
      [
        {
          "ssid": "Beta",
          "bssid": "70:8b:cd:c9:c4:90",
          "auth_mode": "wpa/wpa2-psk",
          "channel": 11,
          "rssi": -85
        },
        {
          "ssid": "Kent-guest",
          "bssid": "22:cf:30:ce:5c:ea",
          "auth_mode": "wpa2-psk",
          "channel": 1,
          "rssi": -88
        },
        {
          "ssid": "Kent",
          "bssid": "22:cf:30:ce:5c:e9",
          "auth_mode":"wpa2-psk",
          "channel": 1,
          "rssi": -90
        }
      ]
      ```