# gm_sourcenet

SourceNet exposes parts of the Source engine's networking and Lua file delivery to Garry's Mod. This repository combines the Windows and Linux dedicated-server builds for x86 and x86-64. It is based on [danielga/gm_sourcenet][upstream] and incorporates the Win64 layout work from the [gm_sourcenet64 fork][fork].

## Installation

Copy the module for the **server process** into `garrysmod/lua/bin/` and use `require("sourcenet")` in server Lua.

| SRCDS process | Module |
| --- | --- |
| Windows x86 | `gmsv_sourcenet_win32.dll` |
| Windows x86-64 | `gmsv_sourcenet_win64.dll` |
| Linux x86, Steam `public` | `gmsv_sourcenet_linux.dll` from `sourcenet-linux-public` |
| Linux x86, Steam `x86-64` | `gmsv_sourcenet_linux.dll` from `sourcenet-linux` |
| Linux x86-64 | `gmsv_sourcenet_linux64.dll` |

The Linux files are ELF shared libraries with the extension expected by GMod. Select a file based on SRCDS's architecture even when the host operating system is 64-bit.

## Engine compatibility

The dedicated-server modules check the loaded engine before installing hooks. Windows matches the PE timestamp, image size and checksum. Linux matches the GNU build ID. An unknown build makes `require("sourcenet")` fail with an explicit error. When upgrading SRCDS, compare its channel layout with the corresponding offsets in `source/net.hpp` before adding its build to `source/main.cpp`.

SourceNet supports `svc_Menu` when the engine provides its vtable. `PreNetChannelShutdown` still runs while the channel is live. By `PostNetChannelShutdown`, saved channel and message wrappers can no longer access the closed channel.

## Build

Use [garrysmod_common][common] at commit `069a789df06d675d294961a20e8736aa070bd355` with its submodules initialized. Use the matching [Premake 5.0.0-beta8][premake] build. Set `GARRYSMOD_COMMON` to the absolute path of the dependency checkout.

**Windows**, Visual Studio 2022 with MSVC x86/x64 and the Windows SDK

```powershell
premake5 vs2022
msbuild projects/windows/vs2022/gmsv_sourcenet.vcxproj /m /p:Configuration=Release /p:Platform=Win32
msbuild projects/windows/vs2022/gmsv_sourcenet.vcxproj /m /p:Configuration=Release /p:Platform=x64
```

**Linux**, GCC with `g++-multilib`

```sh
export CC=gcc CXX=g++ AR=gcc-ar NM=gcc-nm RANLIB=gcc-ranlib
premake5 gmake
make -C projects/linux/gmake -j"$(nproc)" config=release_x86 gmsv_sourcenet
make -C projects/linux/gmake -j"$(nproc)" config=release_x86_64 gmsv_sourcenet
```

The Linux x86 Steam `public` branch uses the older `_srv` shared libraries. Install `patchelf` and build its variant separately.

```sh
premake5 --linux-public gmake
make -C projects/linux/public -j"$(nproc)" config=release_x86 gmsv_sourcenet
```

The modules are in the `x86/Release` and `x86_64/Release` subdirectories of the generated projects. GitHub Actions builds all four server targets and the Linux x86 `public` variant.

[upstream]: https://github.com/danielga/gm_sourcenet
[fork]: https://github.com/Vodeninja/gm_sourcenet64
[common]: https://github.com/danielga/garrysmod_common/tree/069a789df06d675d294961a20e8736aa070bd355
[premake]: https://github.com/danielga/garrysmod_common/releases/tag/premake-build%2F5.0.0-beta8
