/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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
#include <DeviceInfoProviderImpl.h>
#include <OTAConfig.h>
#include <app/server/Dnssd.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <platform/bouffalolab/common/PlatformManagerImpl.h>
#include <system/SystemClock.h>

#if HEAP_MONITORING
#include <MemMonitoring.h>
#endif

#if CONFIG_ENABLE_CHIP_SHELL
#include <ChipShellCollection.h>
#include <lib/shell/Engine.h>
#endif

#if PW_RPC_ENABLED
#include <PigweedLogger.h>
#include <Rpc.h>
#endif
#if CONFIG_ENABLE_CHIP_SHELL || PW_RPC_ENABLED
#include <uart.h>
#endif

#if CONFIG_BOUFFALOLAB_FACTORY_DATA_ENABLE || CONFIG_BOUFFALOLAB_FACTORY_DATA_TEST
#include <platform/bouffalolab/common/FactoryDataProvider.h>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <NetworkCommissioningDriver.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/OpenThread/OpenThreadUtils.h>
#include <platform/ThreadStackManager.h>
#include <platform/bouffalolab/common/ThreadStackManagerImpl.h>
#include <utils_list.h>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_ETHERNET || CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <bl_route_hook.h>
#include <lwip/netif.h>
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI && CHIP_DEVICE_LAYER_TARGET_BL602
#include <wifi_mgmr_ext.h>
#endif
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI && CHIP_DEVICE_LAYER_TARGET_BL702
#include <platform/bouffalolab/BL702/wifi_mgmr_portable.h>
#endif
#if CHIP_DEVICE_CONFIG_ENABLE_ETHERNET
#include <platform/bouffalolab/BL702/EthernetInterface.h>
#endif
#endif

#include <AppTask.h>
#include <plat.h>

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
namespace {
chip::app::Clusters::NetworkCommissioning::Instance
    sWiFiNetworkCommissioningInstance(0 /* Endpoint Id */, &(NetworkCommissioning::BLWiFiDriver::GetInstance()));
}
#endif

#if CONFIG_BOUFFALOLAB_FACTORY_DATA_ENABLE || CONFIG_BOUFFALOLAB_FACTORY_DATA_TEST
namespace {
FactoryDataProvider sFactoryDataProvider;
}
#endif

static chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;

CHIP_ERROR PlatformManagerImpl::PlatformInit(void)
{
    chip::RendezvousInformationFlags rendezvousMode(chip::RendezvousInformationFlag::kOnNetwork);
#if CONFIG_BOUFFALOLAB_FACTORY_DATA_ENABLE || CONFIG_BOUFFALOLAB_FACTORY_DATA_TEST
    CHIP_ERROR retFactoryData = sFactoryDataProvider.Init();
#endif

#if PW_RPC_ENABLED
    PigweedLogger::pw_init();
#elif CONFIG_ENABLE_CHIP_SHELL
    AppTask::StartAppShellTask();
#endif

#if HEAP_MONITORING
    MemMonitoring::startHeapMonitoring();
#endif

    ChipLogProgress(NotSpecified, "Initializing CHIP stack");
    ReturnLogErrorOnFailure(PlatformMgr().InitChipStack());

    chip::DeviceLayer::ConnectivityMgr().SetBLEDeviceName(CHIP_BLE_DEVICE_NAME);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD

#if CONFIG_ENABLE_CHIP_SHELL
    cmd_otcli_init();
#endif

    ChipLogProgress(NotSpecified, "Initializing OpenThread stack");
    ReturnLogErrorOnFailure(ThreadStackMgr().InitThreadStack());

#if CHIP_DEVICE_CONFIG_THREAD_FTD
    ReturnLogErrorOnFailure(ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_Router));
#else
    ReturnLogErrorOnFailure(ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_MinimalEndDevice));
#endif

#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    ReturnLogErrorOnFailure(sWiFiNetworkCommissioningInstance.Init());
#endif

    // Initialize device attestation config
#if CONFIG_BOUFFALOLAB_FACTORY_DATA_ENABLE || CONFIG_BOUFFALOLAB_FACTORY_DATA_TEST
    if (CHIP_NO_ERROR == retFactoryData)
    {
        SetDeviceInstanceInfoProvider(&sFactoryDataProvider);
        SetDeviceAttestationCredentialsProvider(&sFactoryDataProvider);
        SetCommissionableDataProvider(&sFactoryDataProvider);
    }
    else
    {
        ChipLogError(NotSpecified, "sFactoryDataProvider.Init() failed");
    }
#else
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());
#endif

    chip::DeviceLayer::PlatformMgr().LockChipStack();

#if CHIP_DEVICE_CONFIG_ENABLE_EXTENDED_DISCOVERY
    chip::app::DnssdServer::Instance().SetExtendedDiscoveryTimeoutSecs(EXT_DISCOVERY_TIMEOUT_SECS);
#endif

    chip::DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

    static CommonCaseDeviceServerInitParams initParams;
    (void) initParams.InitializeStaticResourcesBeforeServerInit();

    ReturnLogErrorOnFailure(chip::Server::GetInstance().Init(initParams));

    gExampleDeviceInfoProvider.SetStorageDelegate(&chip::Server::GetInstance().GetPersistentStorage());

    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    ChipLogProgress(NotSpecified, "Starting OpenThread task");
    // Start OpenThread task
    ReturnLogErrorOnFailure(ThreadStackMgrImpl().StartThreadTask());
#endif

    ConfigurationMgr().LogDeviceConfig();

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
    rendezvousMode.Set(chip::RendezvousInformationFlag::kBLE);
#endif
    PrintOnboardingCodes(rendezvousMode);

    PlatformMgr().AddEventHandler(AppTask::ChipEventHandler, 0);

#if PW_RPC_ENABLED
    chip::rpc::Init();
#endif

    vTaskResume(GetAppTask().sAppTaskHandle);

    return CHIP_NO_ERROR;
}
