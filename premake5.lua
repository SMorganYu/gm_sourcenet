PROJECT_GENERATOR_VERSION = 3

newoption({
	trigger = "gmcommon",
	description = "Sets the path to the garrysmod_common (https://github.com/danielga/garrysmod_common) directory",
	value = "path to garrysmod_common directory"
})

newoption({
	trigger = "linux-public",
	description = "Build the Linux x86 server module for the Steam public branch"
})

local linuxPublic = _OPTIONS["linux-public"]
assert(not linuxPublic or os.target() == "linux", "--linux-public requires a Linux target")

local gmcommon = assert(_OPTIONS.gmcommon or os.getenv("GARRYSMOD_COMMON"),
	"you didn't provide a path to your garrysmod_common (https://github.com/danielga/garrysmod_common) directory")
include(gmcommon)

CreateWorkspace({
	name = "sourcenet",
	abi_compatible = true,
	path = linuxPublic and "projects/linux/public" or nil
})
	CreateProject({serverside = true})
		IncludeLuaShared()
		IncludeHelpersExtended()
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeSDKLZMA()
		IncludeScanning()
		IncludeDetouring()
		files({"source/server/*.cpp", "source/server/*.hpp"})
		if linuxPublic then
			filter({"system:linux", "architecture:x86"})
				defines("SOURCENET_LINUX_PUBLIC")
				postbuildcommands({
					'patchelf --replace-needed libtier0.so libtier0_srv.so --replace-needed libvstdlib.so libvstdlib_srv.so "%{cfg.buildtarget.abspath}"'
				})
			filter({})
		end

	CreateProject({serverside = false})
		IncludeLuaShared()
		IncludeHelpersExtended()
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeScanning()
		IncludeDetouring()
