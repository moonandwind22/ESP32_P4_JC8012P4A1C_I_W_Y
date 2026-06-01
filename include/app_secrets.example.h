#ifndef APP_SECRETS_H
#define APP_SECRETS_H

// Copy this file to include/app_secrets.h and fill in local-only values.
// app_secrets.h is ignored by git so credentials do not land in source control.

#define LONDONBRIEF_WIFI_SSID "YOUR_WIFI_SSID"
#define LONDONBRIEF_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define LONDONBRIEF_TFL_APP_KEY ""

// Production should provide a root CA PEM bundle/string and keep insecure TLS off.
// Temporarily set LONDONBRIEF_LAB_ALLOW_INSECURE_TLS to 1 only for lab debugging.
#define LONDONBRIEF_TLS_ROOT_CA ""
#define LONDONBRIEF_LAB_ALLOW_INSECURE_TLS 0

#endif
