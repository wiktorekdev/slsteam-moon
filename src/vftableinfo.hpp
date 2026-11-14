#pragma once

namespace VFTIndexes
{
	void init();

	namespace IClientEngine
	{
		inline int GetClientUser = 7;
	}

	namespace IClientApps
	{
		inline int GetAppData = 0;
		inline int GetAppDataSection = 5;
		inline int RequestAppInfoUpdate = 7;
		inline int GetDLCCount = 8;
		inline int GetDLCDataByIndex = 9;
		inline int GetAppType = 10;
	}

	namespace IClientCompat
	{
		// CCompatManager's Linux i386 vtable.  The 2026-10 client (version
		// 1791918634) removed three methods ahead of these (IClientCompatMap
		// 19 -> 16 slots), moving them from 4/7 to 1/4.  Verified by identical
		// slot bodies and by the IPC method order of IClientCompatMap;
		// test_clientcompat checks both slots against an installed client.
		inline int SpecifyCompatTool = 1;
		inline int GetCompatToolName = 4;
	}

	namespace IClientAppManager
	{
		inline int InstallApp = 0;
		inline int UninstallApp = 1;
		inline int LaunchApp = 2;
		inline int GetAppInstallState = 4;
		inline int IsAppDlcInstalled = 9;
		inline int BIsDlcEnabled = 11;
		inline int GetUpdateInfo = 20;
	}

	namespace IClientRemoteStorage
	{
		inline int IsCloudEnabledForApp = 24;
	}

	namespace IClientUser
	{
		inline int BLoggedOn = 4;
		inline int GetSteamID = 10;
		// Linux i386 IClientUserMap; the wrapper carries IPC hash 0xE0468CB4.
		inline int GetEncryptedAppTicket = 121;
	}

	namespace IClientUtils
	{
		inline int GetOfflineMode = 17;
		inline int GetAppId = 19;
	}
}
