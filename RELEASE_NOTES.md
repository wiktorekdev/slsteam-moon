# slssteam-moon-64 v4.0

`slssteam-moon-64` is a native 64-bit (`x86_64` / `ELF64`) fork of [swwayps/slsteam-moon](https://github.com/swwayps/slsteam-moon) that integrates [AceSLS/SLSsteam](https://github.com/AceSLS/SLSsteam)'s ELF64 RTTI vtable and dynamic `IClient*Map` hook resolution architecture for modern **`steamrt64`** Linux Steam clients, while retaining `slssteam-moon`'s full LuaTools feature stack.

## Quick Install (on top of LuaTools)

1. Make sure your Steam client is updated to `steamrt64` (not rolled back to `ubuntu12_32`).
2. Install **LuaTools** ([swwayps/luatools-moon](https://github.com/swwayps/luatools-moon)) as normal.
3. Run:
   ```bash
   curl -fsSL https://raw.githubusercontent.com/foodiemonster/slssteam-moon-64/main/install-over-luatools.sh | bash
   ```
   *(Replace `main` with `slsteam-moon` if installing directly from that branch.)*

Your existing `~/.config/SLSsteam/` settings, manifests, and LuaTools files are preserved automatically.

## What's New in v4.0

### AceSLS RTTI VTable & `IClient*Map` Integration
- **ELF64 RTTI VTable Scanner (`src/rtti_vtable.{hpp,cpp}`):** Indexes `.rodata` / `.rodata.str*`, `.data.rel.ro` / `.data.rel.ro.local`, and `.text` in `steamclient.so` and `steamui.so` at startup, then releases cached tables once hook resolution completes.
- **Resilient Virtual Hook Resolution:** `CCMInterface::RecvPkt`, `ISteamMatchmakingPingResponse::ServerResponded`, `CSteamMatchmakingServers::GetServerDetails`, and `CSteamMatchmakingServers::RequestInternetServerList` now resolve by Itanium ABI RTTI vtable slot first, with `pattern_cache` and byte-pattern fallback preserved.
- **Dynamic `IClient*Map` Slot Resolution (`src/vftableinfo.{hpp,cpp}`):** `VFTIndexes` for `IClientApps`, `IClientCompat`, `IClientAppManager`, `IClientRemoteStorage`, `IClientUser`, and `IClientUtils` are resolved dynamically from `IClient*Map` RIP-relative method-name references at startup (automatically catching and fixing `IClientUser::GetEncryptedAppTicket` slot drift `121 -> 122` on 64-bit `steamclient.so`).

### Full `slssteam-moon` LuaTools Stack Preserved
- Keeps `stplug-in` / `luaappids.yaml` support, `AppInfoProvision`, `DepotKey` & `ManifestId` importers, `HotReload`, `OwnerWork`, SteamStub/Steamless integration, and `pattern_cache`.
- Includes all `v3.1` 64-bit struct/hook fixes (`CProtoBufMsgBase`, `CServerPipe`, `CUtlBuffer`, `HServerRequest`, `LD_AUDIT` `la_preinit` DNS deadlock fix, and Ubuntu 22.04 / glibc 2.35 compatibility).

## Credits

- **[swwayps](https://github.com/swwayps) ([slsteam-moon](https://github.com/swwayps/slsteam-moon) / [luatools-moon](https://github.com/swwayps/luatools-moon))** — LuaTools integration, `AppInfoProvision`, `DepotKey` & `ManifestId` importers, `HotReload`, `OwnerWork`, `PatternCache`, SteamStub integration, and protocol enhancements.
- **[AceSLS](https://github.com/AceSLS) and contributors ([AceSLS/SLSsteam](https://github.com/AceSLS/SLSsteam))** — original SLSsteam hook framework, protobuf plumbing, ownership & family-share bypasses, and the RTTI vtable & `IClient*Map` dynamic resolution methods integrated in `v4.0`.

## Known Limitations
- **32-bit `ubuntu12_32` Steam**: Targets `steamrt64` only. Rolled-back 32-bit Steam clients should continue using upstream `swwayps/slsteam-moon`.
- **`ReconcilePin`**: Logs non-fatal warnings for two unlocated 64-bit offsets (`ctx-vector` and `target-local` fixes disabled).
- **`cloud_redirect.so`**: Skipped on `steamrt64` while CloudRedirect remains 32-bit.
