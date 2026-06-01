#ifndef C6_WORKER_SECRETS_H
#define C6_WORKER_SECRETS_H

// Copy this file to c6_worker/include/c6_worker_secrets.h.
// c6_worker_secrets.h is ignored by git so credentials stay local.

#define C6_WORKER_WIFI_SSID "YOUR_WIFI_SSID"
#define C6_WORKER_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define C6_WORKER_TFL_APP_KEY ""

// Production should provide a root CA PEM bundle/string and keep insecure TLS off.
// Temporarily set C6_WORKER_LAB_ALLOW_INSECURE_TLS to 1 only for lab debugging.
#define C6_WORKER_TLS_ROOT_CA ""
#define C6_WORKER_LAB_ALLOW_INSECURE_TLS 0

// UART is only for a future wired/lab transport. The no-board-mod production
// direction is the SDIO snapshot worker path.
#ifndef C6_WORKER_TRANSPORT
#define C6_WORKER_TRANSPORT 2
#endif
#define C6_WORKER_SERIAL_PORT 1
#define C6_WORKER_SERIAL_RX_PIN -1
#define C6_WORKER_SERIAL_TX_PIN -1

#endif
