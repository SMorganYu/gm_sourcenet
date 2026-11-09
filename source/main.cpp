#include "main.hpp"
#include "net.hpp"
#include "hooks.hpp"
#include "sn_bf_write.hpp"
#include "sn_bf_read.hpp"
#include "netchannel.hpp"
#include "netchannelhandler.hpp"
#include "subchannel.hpp"
#include "datafragments.hpp"
#include "filehandle.hpp"
#include "ucharptr.hpp"
#include "netadr.hpp"
#include "networkstringtablecontainer.hpp"
#include "networkstringtable.hpp"
#include "gameeventmanager.hpp"
#include "gameevent.hpp"
#include "netmessage.hpp"

#if defined SOURCENET_SERVER

#include "server/datapack.hpp"
#include "server/gametags.hpp"

#endif

#include <GarrysMod/InterfacePointers.hpp>

#include <interface.h>
#include <eiface.h>
#include <cdll_int.h>
#include <iserver.h>

#if defined( _WIN32 ) && defined( SOURCENET_SERVER )
#include <Windows.h>

struct WindowsEngineBuild
{
	DWORD timestamp;
	DWORD imageSize;
	DWORD checksum;
	size_t serverOffset;
};

static const WindowsEngineBuild *FindSupportedEngine( const void *module )
{
	static constexpr WindowsEngineBuild builds[] = {
#if defined( _WIN64 )
		{ 0x6AEB7331, 0xE53000, 0x57D685, 0 },
		{ 0x6ADFBD6C, 0xE53000, 0x574FC4, 0 },
#else
		{ 0x6AEB711D, 0xB89000, 0x43CE2E, 0 },
		{ 0x6ADFBAB5, 0xB89000, 0x432973, 0 },
		// Public Win32 has no sv symbol and does not match the SDK signature.
		{ 0x6AAC319D, 0xBAC000, 0x4784AB, 0x6F53A8 },
#endif
	};

	if( module == nullptr )
		return nullptr;

	const auto *base = static_cast<const uint8_t *>( module );
	const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>( base );
	if( dos->e_magic != IMAGE_DOS_SIGNATURE )
		return nullptr;
	const auto *pe = reinterpret_cast<const IMAGE_NT_HEADERS *>( base + dos->e_lfanew );
	if( pe->Signature != IMAGE_NT_SIGNATURE )
		return nullptr;

	for( const auto &build : builds )
	{
		if( pe->FileHeader.TimeDateStamp == build.timestamp &&
			pe->OptionalHeader.SizeOfImage == build.imageSize &&
			pe->OptionalHeader.CheckSum == build.checksum )
			return &build;
	}
	return nullptr;
}

#elif defined( __linux__ ) && defined( SOURCENET_SERVER )
#include <cstring>
#include <link.h>

static int FindEngineBuildId( dl_phdr_info *info, size_t, void *context )
{
	const auto *engine = static_cast<const link_map *>( context );
	if( info->dlpi_addr != engine->l_addr )
		return 0;

	for( ElfW(Half) i = 0; i < info->dlpi_phnum; ++i )
	{
		const auto &header = info->dlpi_phdr[i];
		if( header.p_type != PT_NOTE )
			continue;

		const auto *note = reinterpret_cast<const uint8_t *>( info->dlpi_addr + header.p_vaddr );
		size_t remaining = header.p_filesz;
		while( remaining >= sizeof( ElfW(Nhdr) ) )
		{
			ElfW(Nhdr) descriptor;
			std::memcpy( &descriptor, note, sizeof( descriptor ) );
			note += sizeof( descriptor );
			remaining -= sizeof( descriptor );
			const size_t nameSize = ( static_cast<size_t>( descriptor.n_namesz ) + 3 ) & ~size_t( 3 );
			const size_t dataSize = ( static_cast<size_t>( descriptor.n_descsz ) + 3 ) & ~size_t( 3 );
			if( nameSize > remaining || dataSize > remaining - nameSize )
				break;

			if( descriptor.n_type == NT_GNU_BUILD_ID && descriptor.n_namesz == 4 &&
				descriptor.n_descsz == 20 && std::memcmp( note, "GNU", 4 ) == 0 )
			{
				const auto *id = note + nameSize;
#if defined( __x86_64__ )
				const char buildId[] = "\x73\x2e\x92\x84\x34\x56\x30\xef\x1e\x94\x0f\x92\x80\x18\x83\x3b\x19\xad\x55\x9a";
#elif defined( SOURCENET_LINUX_PUBLIC )
				const char buildId[] = "\x9d\x5b\x5a\xbf\xaf\x9e\x92\x49\x3b\x3e\x33\x11\xe5\xaa\x85\x5b\x5b\xd1\xc7\x20";
#else
				const char buildId[] = "\xff\x92\x54\xe6\x70\xb2\x42\xcf\xfa\x23\x9d\x1f\xa3\xac\xbb\x59\x81\x59\x34\x27";
#endif
				return std::memcmp( id, buildId, sizeof( buildId ) - 1 ) == 0;
			}

			note += nameSize + dataSize;
			remaining -= nameSize + dataSize;
		}
	}

	return 0;
}

static bool IsSupportedEngine( void *module )
{
	link_map *engine = nullptr;
	return module != nullptr && dlinfo( module, RTLD_DI_LINKMAP, &engine ) == 0 &&
		engine != nullptr && dl_iterate_phdr( FindEngineBuildId, engine ) == 1;
}
#endif

namespace global
{
	static bool loaded = false;

	const char *tostring_format = "%s: %p";

	GarrysMod::Lua::ILuaBase *lua = nullptr;

	const SourceSDK::FactoryLoader engine_loader( "engine" );
	const SourceSDK::ModuleLoader client_loader( "client" );
	const SourceSDK::ModuleLoader server_loader( "server" );

	IVEngineServer *engine_server = nullptr;
	IVEngineClient *engine_client = nullptr;
	IServer *server = nullptr;

	bool is_dedicated = true;

	LUA_FUNCTION( index )
	{
		LUA->GetMetaTable( 1 );
		LUA->Push( 2 );
		LUA->RawGet( -2 );
		if( !LUA->IsType( -1, GarrysMod::Lua::Type::NIL ) )
			return 1;

		LUA->GetFEnv( 1 );
		LUA->Push( 2 );
		LUA->RawGet( -2 );
		return 1;
	}

	LUA_FUNCTION( newindex )
	{
		LUA->GetFEnv( 1 );
		LUA->Push( 2 );
		LUA->Push( 3 );
		LUA->RawSet( -3 );
		return 0;
	}

	LUA_FUNCTION( GetTable )
	{
		LUA->GetFEnv( 1 );
		return 1;
	}

	void CheckType( GarrysMod::Lua::ILuaBase *LUA, int32_t index, int32_t type, const char *nametype )
	{
		if( !LUA->IsType( index, type ) )
			LUA->TypeError( index, nametype );
	}

	static void Initialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		LUA->CreateTable( );

		LUA->PushString( "sourcenet 1.1.17" );
		LUA->SetField( -2, "Version" );

		// version num follows LuaJIT style, xxyyzz
		LUA->PushNumber( 10117 );
		LUA->SetField( -2, "VersionNum" );

		LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, "sourcenet" );

		loaded = true;
	}

	static void Deinitialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		LUA->PushNil( );
		LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, "sourcenet" );

		loaded = false;
	}

}

GMOD_MODULE_OPEN( )
{
#if defined( _WIN32 ) && defined( SOURCENET_SERVER )
	auto *engineModule = static_cast<uint8_t *>( global::engine_loader.GetModule( ) );
	const auto *engineBuild = FindSupportedEngine( engineModule );
	if( engineBuild == nullptr )
		LUA->ThrowError( "SourceNet: unsupported engine build for this CNetChan layout" );
#elif defined( __linux__ ) && defined( SOURCENET_SERVER )
	if( !IsSupportedEngine( global::engine_loader.GetModule( ) ) )
		LUA->ThrowError( "SourceNet: unsupported engine build for this CNetChan layout" );
#endif

	global::lua = LUA;

	global::engine_server = InterfacePointers::VEngineServer( );
	if( global::engine_server == nullptr )
		LUA->ThrowError( "failed to retrieve server engine interface" );

	global::is_dedicated = global::engine_server->IsDedicatedServer( );
	if( !global::is_dedicated )
	{
		global::engine_client = InterfacePointers::VEngineClient( );
		if( global::engine_client == nullptr )
			LUA->ThrowError( "failed to retrieve client engine interface" );
	}

#if defined( _WIN32 ) && defined( SOURCENET_SERVER )
	if( engineBuild->serverOffset != 0 )
		global::server = reinterpret_cast<IServer *>( engineModule + engineBuild->serverOffset );
	else
#endif
		global::server = InterfacePointers::Server( );

	if( global::server == nullptr )
		LUA->ThrowError( "failed to locate IServer" );

#if defined SOURCENET_SERVER

	DataPack::PreInitialize( LUA );
	GameTags::PreInitialize( LUA );

#endif

	NetMessage::PreInitialize( LUA );
	Hooks::PreInitialize( LUA );

	sn_bf_write::Initialize( LUA );
	sn_bf_read::Initialize( LUA );
	NetChannel::Initialize( LUA );
	subchannel::Initialize( LUA );
	dataFragments::Initialize( LUA );
	FileHandle::Initialize( LUA );
	UCHARPTR::Initialize( LUA );
	netadr::Initialize( LUA );
	NetChannelHandler::Initialize( LUA );
	NetworkStringTableContainer::Initialize( LUA );
	NetworkStringTable::Initialize( LUA );
	GameEventManager::Initialize( LUA );
	GameEvent::Initialize( LUA );

#if defined SOURCENET_SERVER

	DataPack::Initialize( LUA );
	GameTags::Initialize( LUA );

#endif

	NetMessage::Initialize( LUA );
	Hooks::Initialize( LUA );
	global::Initialize( LUA );

	return 0;
}

GMOD_MODULE_CLOSE( )
{
	if( !global::loaded )
		return 0;

	sn_bf_write::Deinitialize( LUA );
	sn_bf_read::Deinitialize( LUA );
	NetChannel::Deinitialize( LUA );
	subchannel::Deinitialize( LUA );
	dataFragments::Deinitialize( LUA );
	FileHandle::Deinitialize( LUA );
	UCHARPTR::Deinitialize( LUA );
	netadr::Deinitialize( LUA );
	NetChannelHandler::Deinitialize( LUA );
	NetworkStringTableContainer::Deinitialize( LUA );
	NetworkStringTable::Deinitialize( LUA );
	GameEventManager::Deinitialize( LUA );
	GameEvent::Deinitialize( LUA );

#if defined SOURCENET_SERVER

	DataPack::Deinitialize( LUA );
	GameTags::Deinitialize( LUA );

#endif

	NetMessage::Deinitialize( LUA );
	Hooks::Deinitialize( LUA );
	global::Deinitialize( LUA );

	return 0;
}
