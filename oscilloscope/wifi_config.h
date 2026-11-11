#pragma once

/*******************************************************************************
 * PocketDSO — Wi-Fi Network & IP Configuration
 * =============================================================================
 * Enter your local Wi-Fi credentials below.
 * If left as "YOUR_SSID", the oscilloscope boots in offline standalone mode.
 *
 * IP Configuration:
 *   - Pure DHCP (Recommended, WIFI_USE_STATIC_IP = 0):
 *     The oscilloscope requests an IP directly from your router's DHCP server.
 *     It registers on the router's client table as "PocketDSO" and is
 *     accessible by typing the router-assigned IP or http://pocketdso.local.
 *   - Static IP (WIFI_USE_STATIC_IP = 1):
 *     Forces a static IP (default .142). Only use if your router does not
 *     provide DHCP or if you have reserved this IP on your router.
 ******************************************************************************/

// Network credentials (replace with your home / lab Wi-Fi)
#define WIFI_SSID     "WIFI_SSID"
#define WIFI_PASSWORD "WIFI_PASSWORD"

// Static IP preference:
// 0 = Pure DHCP leased from router (RECOMMENDED - visible on router page and accessible)
// 1 = Force static IP ending in .142
#define WIFI_USE_STATIC_IP         0

// Auto-connect to Wi-Fi at boot if valid credentials are provided
#define WIFI_AUTO_CONNECT_AT_BOOT  1

// Default static IP address (Used ONLY when WIFI_USE_STATIC_IP == 1)
#define WIFI_STATIC_IP_OCTET1      192
#define WIFI_STATIC_IP_OCTET2      168
#define WIFI_STATIC_IP_OCTET3      1
#define WIFI_STATIC_IP_OCTET4      142

// Default Gateway, Subnet & DNS (Used ONLY when WIFI_USE_STATIC_IP == 1)
#define WIFI_GATEWAY_OCTET1        192
#define WIFI_GATEWAY_OCTET2        168
#define WIFI_GATEWAY_OCTET3        1
#define WIFI_GATEWAY_OCTET4        1

#define WIFI_SUBNET_OCTET1         255
#define WIFI_SUBNET_OCTET2         255
#define WIFI_SUBNET_OCTET3         255
#define WIFI_SUBNET_OCTET4         0

#define WIFI_DNS_OCTET1            192
#define WIFI_DNS_OCTET2            168
#define WIFI_DNS_OCTET3            1
#define WIFI_DNS_OCTET4            1

// If target static IP is already occupied or reserved on the network:
// Try up to N sequential fallback IPs (.101, .102, etc.)
#define WIFI_FALLBACK_MAX_STEPS    10

// If all static IP candidates fail: Fall back to automatic DHCP leasing
#define WIFI_FALLBACK_TO_DHCP      1

// Connection timeout in milliseconds before falling back to standalone offline mode
#define WIFI_CONNECT_TIMEOUT_MS    10000
