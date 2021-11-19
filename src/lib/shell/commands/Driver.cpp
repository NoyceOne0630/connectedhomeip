/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include <lib/core/CHIPCore.h>
#include <lib/shell/Commands.h>
#if CONFIG_DEVICE_LAYER
#include <platform/CHIPDeviceLayer.h>
#endif
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <lib/shell/Engine.h>
#include <lib/shell/commands/Help.h>
#include <lib/support/CHIPArgParser.hpp>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>

using chip::DeviceLayer::ConnectivityMgr;

namespace chip {
namespace Shell {

static chip::Shell::Engine sShellDriverSubcommands;

int DriverHelpHandler(int argc, char ** argv)
{
    sShellDriverSubcommands.ForEachCommand(PrintCommandHelp, nullptr);
    return 0;
}

static CHIP_ERROR OnLightHandler(int argc, char ** argv)
{
    if (argc != 1)
        return CHIP_ERROR_INVALID_ARGUMENT;
    chip::app::Clusters::OnOff::Attributes::OnOff::Set(atoi(argv[0]), 1);
    return CHIP_NO_ERROR;
}

static CHIP_ERROR OffLightHandler(int argc, char ** argv)
{
    if (argc != 1)
        return CHIP_ERROR_INVALID_ARGUMENT;
    chip::app::Clusters::OnOff::Attributes::OnOff::Set(atoi(argv[0]), 0);
    return CHIP_NO_ERROR;
}


static CHIP_ERROR DriverHandler(int argc, char ** argv)
{
    if (argc == 0)
    {
        DriverHelpHandler(argc, argv);
        return CHIP_NO_ERROR;
    }
    return sShellDriverSubcommands.ExecCommand(argc, argv);
}

void RegisterDriverCommands()
{
    static const shell_command_t sDriverSubCommands[] = {
        { &OnLightHandler, "on", "Usage: driver on endpoint-id" },
        { &OffLightHandler, "off", "Usage: driver off endpoint-id"}
    };

    static const shell_command_t sDriverComand = { &DriverHandler, "driver", "driver" };

    // Register `device` subcommands with the local shell dispatcher.
    sShellDriverSubcommands.RegisterCommands(sDriverSubCommands, ArraySize(sDriverSubCommands));

    // Register the root `device` command with the top-level shell.
    Engine::Root().RegisterCommands(&sDriverComand, 1);
}

} // namespace Shell
} // namespace chip
