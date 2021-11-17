/*
 *
 *    Copyright (c) 2020-2021 Project CHIP Authors
 *    Copyright (c) 2018 Nest Labs, Inc.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
/* this file behaves like a config.h, comes first */
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/ConnectivityManager.h>

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
#include <platform/internal/GenericConnectivityManagerImpl_BLE.cpp>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/internal/GenericConnectivityManagerImpl_Thread.cpp>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <platform/internal/GenericConnectivityManagerImpl_WiFi.cpp>
#endif

#include <esp_wifi.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/internal/BLEManager.h>

using namespace ::chip;
using namespace ::chip::Inet;
using namespace ::chip::System;
using namespace ::chip::TLV;
using namespace ::chip::app::Clusters::GeneralDiagnostics;

namespace chip {
namespace DeviceLayer {

ConnectivityManagerImpl ConnectivityManagerImpl::sInstance;
// ==================== ConnectivityManager Platform Internal Methods ====================

CHIP_ERROR ConnectivityManagerImpl::_Init()
{
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    GenericConnectivityManagerImpl_Thread<ConnectivityManagerImpl>::_Init();
#endif
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    InitWiFi();
#endif
    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::_OnPlatformEvent(const ChipDeviceEvent * event)
{
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    GenericConnectivityManagerImpl_Thread<ConnectivityManagerImpl>::_OnPlatformEvent(event);
#endif
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    OnWiFiPlatformEvent(event);
#endif
}

static InterfaceType GetInterfaceType(const char* if_desc)
{
    if (strncmp(if_desc, "ap", strnlen(if_desc, 2)) == 0 || strncmp(if_desc, "sta", strnlen(if_desc, 3)) == 0)
        return InterfaceType::EMBER_ZCL_INTERFACE_TYPE_WI_FI;
    if (strncmp(if_desc, "openthread", strnlen(if_desc, 10)) == 0)
        return InterfaceType::EMBER_ZCL_INTERFACE_TYPE_THREAD;
    if (strncmp(if_desc, "eth", strnlen(if_desc, 3)) == 0)
        return InterfaceType::EMBER_ZCL_INTERFACE_TYPE_ETHERNET;
    return InterfaceType::EMBER_ZCL_INTERFACE_TYPE_UNSPECIFIED;
}

CHIP_ERROR ConnectivityManagerImpl::_GetNetworkInterfaces(NetworkInterface ** netifpp)
{
    esp_netif_t * netif = esp_netif_next(NULL);
    NetworkInterface * head = NULL;
    if (netif == NULL)
    {
        ChipLogError(DeviceLayer, "Failed to get network interfaces");
    }
    else
    {
        for (esp_netif_t * ifa =  netif; ifa != NULL; ifa = esp_netif_next(ifa))
        {
            NetworkInterface * ifp = new NetworkInterface();
            strncpy(ifp->Name, esp_netif_get_ifkey(ifa), Inet::InterfaceId::kMaxIfNameLength);
            ifp->Name[Inet::InterfaceId::kMaxIfNameLength - 1] = '\0';
            ifp->name                            = CharSpan(ifp->Name, strlen(ifp->Name));
            ifp->fabricConnected                 = true;
            ifp->type                            = GetInterfaceType(esp_netif_get_desc(ifa));
            ifp->offPremiseServicesReachableIPv4 = false;
            ifp->offPremiseServicesReachableIPv6 = false;
            if (esp_netif_get_mac(ifa, ifp->MacAddress) != ESP_OK)
            {
                ChipLogError(DeviceLayer, "Failed to get network hardware address");
            }
            else
            {
                ifp->hardwareAddress = ByteSpan(ifp->MacAddress, 6);
            }
            ifp->Next = head;
            head      = ifp;
        }
    }
    *netifpp = head;
    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::_ReleaseNetworkInterfaces(NetworkInterface * netifp)
{
    while (netifp)
    {
        NetworkInterface * del = netifp;
        netifp                 = netifp->Next;
        delete del;
    }
}

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
uint8_t MapAuthModeToSecurityType(wifi_auth_mode_t authmode)
{
    switch (authmode)
    {
    case WIFI_AUTH_WEP:
        return 1;
    case WIFI_AUTH_WPA_PSK:
        return 2;
    case WIFI_AUTH_WPA2_PSK:
        return 3;
    case WIFI_AUTH_WPA3_PSK:
        return 4;
    default:
        return 0;
    }
}

uint8_t GetWiFiVersionFromAPRecord(wifi_ap_record_t ap_info)
{
    if (ap_info.phy_11n)
        return 4; // WiFi Version 4
    else if (ap_info.phy_11g)
        return 3; // WiFi Version 3
    else if (ap_info.phy_11b)
        return 1; // WiFi Version 1
    else
        return 0; // ESP Platform doesn't support 5G Hz WiFi Version (WiFi 5 & WiFi 2)
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiBssid(ByteSpan& Bssid)
{
    static uint8_t wifi_bssid[6];
    wifi_ap_record_t ap_info;
    esp_err_t err;

    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK)
    {
        memcpy(wifi_bssid, ap_info.bssid, 6);
    }
    Bssid = wifi_bssid;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiSecurityType(uint8_t & securityType)
{
    securityType = 0;
    wifi_ap_record_t ap_info;
    esp_err_t err;

    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK)
    {
        securityType = MapAuthModeToSecurityType(ap_info.authmode);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiVersion(uint8_t & wifiVersion)
{
    wifiVersion = 0;
    wifi_ap_record_t ap_info;
    esp_err_t err;
    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK)
    {
        wifiVersion = GetWiFiVersionFromAPRecord(ap_info);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiChannelNumber(uint16_t & channelNumber)
{
    channelNumber = 0;
    wifi_ap_record_t ap_info;
    esp_err_t err;

    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK)
    {
        channelNumber = ap_info.primary;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiRssi(int8_t & rssi)
{
    rssi = 0;
    wifi_ap_record_t ap_info;
    esp_err_t err;

    err = esp_wifi_sta_get_ap_info(&ap_info);

    if (err == ESP_OK)
    {
        rssi = ap_info.rssi;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiBeaconLostCount(uint32_t & beaconLostCount)
{
    beaconLostCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiCurrentMaxRate(uint64_t & currentMaxRate)
{
    currentMaxRate = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiPacketMulticastRxCount(uint32_t & packetMulticastRxCount)
{
    packetMulticastRxCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiPacketMulticastTxCount(uint32_t & packetMulticastTxCount)
{
    packetMulticastTxCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiPacketUnicastRxCount(uint32_t & packetUnicastRxCount)
{
    packetUnicastRxCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiPacketUnicastTxCount(uint32_t & packetUnicastTxCount)
{
    packetUnicastTxCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_GetWiFiOverrunCount(uint64_t & overrunCount)
{
    overrunCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_ResetWiFiNetworkDiagnosticsCounts()
{
    return CHIP_NO_ERROR;
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

} // namespace DeviceLayer
} // namespace chip
