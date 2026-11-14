// SPDX-License-Identifier: AGPL-3.0-only

#include "vftableinfo.hpp"

#include "globals.hpp"
#include "log.hpp"
#include "rtti_vtable.hpp"

namespace VFTIndexes
{

namespace
{

struct SlotSpec
{
	int& target;
	const char* mapType;
	const char* fnName;
};

} // namespace

void init()
{
#if defined(__x86_64__)
	if (g_modSteamClient.base == 0 || g_modSteamClient.size == 0)
	{
		return;
	}

	const SlotSpec specs[] = {
		{ IClientApps::GetAppData, "14IClientAppsMap", "GetAppData" },
		{ IClientApps::GetAppDataSection, "14IClientAppsMap", "GetAppDataSection" },
		{ IClientApps::RequestAppInfoUpdate, "14IClientAppsMap", "RequestAppInfoUpdate" },
		{ IClientApps::GetDLCCount, "14IClientAppsMap", "GetDLCCount" },
		{ IClientApps::GetDLCDataByIndex, "14IClientAppsMap", "BGetDLCDataByIndex" },
		{ IClientApps::GetAppType, "14IClientAppsMap", "GetAppType" },

		{ IClientCompat::SpecifyCompatTool, "16IClientCompatMap", "SpecifyCompatTool" },
		{ IClientCompat::GetCompatToolName, "16IClientCompatMap", "GetCompatToolName" },

		{ IClientAppManager::InstallApp, "20IClientAppManagerMap", "InstallApp" },
		{ IClientAppManager::UninstallApp, "20IClientAppManagerMap", "UninstallApp" },
		{ IClientAppManager::LaunchApp, "20IClientAppManagerMap", "LaunchApp" },
		{ IClientAppManager::GetAppInstallState, "20IClientAppManagerMap", "GetAppInstallState" },
		{ IClientAppManager::IsAppDlcInstalled, "20IClientAppManagerMap", "IsAppDlcInstalled" },
		{ IClientAppManager::BIsDlcEnabled, "20IClientAppManagerMap", "BIsDlcEnabled" },
		{ IClientAppManager::GetUpdateInfo, "20IClientAppManagerMap", "GetUpdateInfo" },

		{ IClientRemoteStorage::IsCloudEnabledForApp, "23IClientRemoteStorageMap", "IsCloudEnabledForApp" },

		{ IClientUser::BLoggedOn, "14IClientUserMap", "BLoggedOn" },
		{ IClientUser::GetSteamID, "14IClientUserMap", "GetSteamID" },
		{ IClientUser::GetEncryptedAppTicket, "14IClientUserMap", "GetEncryptedAppTicket" },

		{ IClientUtils::GetOfflineMode, "15IClientUtilsMap", "GetOfflineMode" },
		{ IClientUtils::GetAppId, "15IClientUtilsMap", "GetAppID" },
	};

	size_t resolved = 0;
	for (const auto& spec : specs)
	{
		const unsigned int slot = RttiVTable::resolveMapSlot(
			g_modSteamClient, spec.mapType, spec.fnName);
		if (slot != RttiVTable::NO_INDEX)
		{
			const int prev = spec.target;
			spec.target = static_cast<int>(slot);
			++resolved;
			if (g_pLog && prev != spec.target)
			{
				g_pLog->info("RttiVTable: updated %s::%s slot %d -> %d\n",
				             spec.mapType, spec.fnName, prev, spec.target);
			}
		}
	}

	if (g_pLog)
	{
		g_pLog->debug("RttiVTable: resolved %zu/%zu IClient*Map slots\n",
		              resolved, sizeof(specs) / sizeof(specs[0]));
	}
#endif
}

} // namespace VFTIndexes

