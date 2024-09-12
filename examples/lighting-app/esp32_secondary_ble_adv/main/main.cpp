/*
 *
 *    Copyright (c) 2021-2023 Project CHIP Authors
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

#include "DeviceCallbacks.h"

#include "AppTask.h"
#include "esp_log.h"
#include "app_wifi.h"
#include "platform/PlatformManager.h"
#include "wifi_provisioning/manager.h"
#include <common/CHIPDeviceManager.h>
#include <common/Esp32AppServer.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "spi_flash_mmap.h"
#else
#include "esp_spi_flash.h"
#endif
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "shell_extension/launch.h"
#include "shell_extension/openthread_cli_register.h"
#include <app/server/Dnssd.h>
#include <app/server/OnboardingCodesUtil.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <platform/ESP32/ESP32Utils.h>

#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

#if CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
#include <platform/ESP32/ESP32DeviceInfoProvider.h>
#else
#include <DeviceInfoProviderImpl.h>
#endif // CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER

#if CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#endif

#include <esp_rmaker_console.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_user_mapping.h>

using namespace ::chip;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceManager;
using namespace ::chip::DeviceLayer;

static const char TAG[] = "light-app";

static AppDeviceCallbacks EchoCallbacks;
static AppDeviceCallbacksDelegate sAppDeviceCallbacksDelegate;
static esp_rmaker_device_t *light_device = nullptr;

namespace {
#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
DeviceLayer::ESP32FactoryDataProvider sFactoryDataProvider;
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

#if CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
DeviceLayer::ESP32DeviceInfoProvider gExampleDeviceInfoProvider;
#else
DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;
#endif // CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER

#if CONFIG_SEC_CERT_DAC_PROVIDER
DeviceLayer::ESP32SecureCertDACProvider gSecureCertDACProvider;
#endif // CONFIG_SEC_CERT_DAC_PROVIDER

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");
ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

chip::Credentials::DeviceAttestationCredentialsProvider * get_dac_provider(void)
{
#if CONFIG_SEC_CERT_DAC_PROVIDER
#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    gSecureCertDACProvider.SetCertificationDeclaration(cdSpan);
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API
    return &gSecureCertDACProvider;
#elif CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    sFactoryDataProvider.SetCertificationDeclaration(cdSpan);
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API
    return &sFactoryDataProvider;
#else  // EXAMPLE_DAC_PROVIDER
    return chip::Credentials::Examples::GetExampleDACProvider();
#endif
}

} // namespace

static void InitServer(intptr_t context)
{
    // Print QR Code URL
    PrintOnboardingCodes(chip::RendezvousInformationFlags(CONFIG_RENDEZVOUS_MODE));

    DeviceCallbacksDelegate::Instance().SetAppDelegate(&sAppDeviceCallbacksDelegate);
    Esp32AppServer::Init(); // Init ZCL Data Model and CHIP App Server AND Initialize device attestation config
}

extern "C" void app_main()
{
    // Initialize the ESP NVS layer.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init() failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default() failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "chip-esp32-light-example starting");
    ESP_LOGI(TAG, "==================================================");

#if CONFIG_ENABLE_CHIP_SHELL
    chip::LaunchShell();
#endif
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    if (Internal::ESP32Utils::InitWiFiStack() != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to initialize WiFi stack");
        return;
    }
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI
    app_wifi_init();

    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *rmaker_node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "Lightbulb");
    if (!rmaker_node) {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    light_device = esp_rmaker_lightbulb_device_create("Light", nullptr, DEFAULT_POWER);
    esp_rmaker_device_add_bulk_cb(light_device, nullptr, NULL);

    esp_rmaker_device_add_param(light_device,
                                esp_rmaker_brightness_param_create(ESP_RMAKER_DEF_BRIGHTNESS_NAME, DEFAULT_BRIGHTNESS));
    esp_rmaker_device_add_param(light_device, esp_rmaker_hue_param_create(ESP_RMAKER_DEF_HUE_NAME, DEFAULT_HUE));
    esp_rmaker_device_add_param(light_device,
                                esp_rmaker_saturation_param_create(ESP_RMAKER_DEF_SATURATION_NAME, DEFAULT_SATURATION));

    esp_rmaker_node_add_device(rmaker_node, light_device);

    esp_rmaker_ota_enable_default();

    esp_rmaker_timezone_service_enable();

    esp_rmaker_schedule_enable();

    err = app_wifi_set_custom_mfg_data(MGF_DATA_DEVICE_TYPE_LIGHT, MFG_DATA_DEVICE_SUBTYPE_LIGHT);

    err = app_wifi_start(POP_TYPE_MAC);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start Wifi. Aborting!!!");
        abort();
    }
    DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

    CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();
    CHIP_ERROR error              = deviceMgr.Init(&EchoCallbacks);
    if (error != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "device.Init() failed: %s", ErrorStr(error));
        return;
    }

#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
    SetCommissionableDataProvider(&sFactoryDataProvider);
#if CONFIG_ENABLE_ESP32_DEVICE_INSTANCE_INFO_PROVIDER
    SetDeviceInstanceInfoProvider(&sFactoryDataProvider);
#endif
#endif

    SetDeviceAttestationCredentialsProvider(get_dac_provider());

    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitServer, reinterpret_cast<intptr_t>(nullptr));

    error = GetAppTask().StartAppTask();
    if (error != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "GetAppTask().StartAppTask() failed : %s", ErrorStr(error));
    }
    bool is_wifi_provisioned = false;
    wifi_prov_mgr_is_provisioned(&is_wifi_provisioned);
    if (is_wifi_provisioned && esp_rmaker_user_node_mapping_get_state() == ESP_RMAKER_USER_MAPPING_DONE) {
        PlatformMgr().LockChipStack();
        chip::DeviceLayer::Internal::BLEMgr().Shutdown();
        PlatformMgr().UnlockChipStack();
    }
}
