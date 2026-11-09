#include "gametags.hpp"

#include <GarrysMod/FunctionPointers.hpp>
#include <GarrysMod/ModuleLoader.hpp>

#include <detouring/classproxy.hpp>

#include <strtools.h>
#include <networkstringtabledefs.h>
#include <steam/steam_gameserver.h>

class CBaseServer;
#if !defined( __linux__ )
class CSteam3Server : public CSteamGameServerAPIContext { };
#endif

namespace GameTags
{
	class CBaseServerProxy : Detouring::ClassProxy<CBaseServer, CBaseServerProxy>
	{
	public:
		static void Initialize( GarrysMod::Lua::ILuaBase *LUA )
		{
			RecalculateTags_original = FunctionPointers::CBaseServer_RecalculateTags( );
			if( RecalculateTags_original == nullptr )
				LUA->ThrowError( "unable to find CBaseServer::RecalculateTags" );

#if defined( __linux__ )
			// Linux engines can obtain the interface directly from Steam.
			static const SourceSDK::ModuleLoader steam_api( "steam_api" );
			get_game_server = reinterpret_cast<GetGameServer>( steam_api.GetSymbol( "SteamAPI_SteamGameServer_v015" ) );
			if( get_game_server == nullptr )
				LUA->ThrowError( "unable to find SteamGameServer015" );
#else
			FunctionPointers::Steam3Server_t Steam3Server = FunctionPointers::Steam3Server( );
			if( Steam3Server == nullptr )
				LUA->ThrowError( "unable to find Steam3Server" );

			gameserver_context = Steam3Server( );
			if( gameserver_context == nullptr )
				LUA->ThrowError( "unable to load CSteamGameServerAPIContext interface" );
#endif
		}

		void RecalculateTags( )
		{
#if defined( __linux__ )
			ISteamGameServer *gameserver = get_game_server( );
#else
			ISteamGameServer *gameserver = gameserver_context->SteamGameServer( );
#endif
			if( gameserver != nullptr )
				gameserver->SetGameTags( gametags_substitute.c_str( ) );
		}

		static bool HookRecalculateTags( )
		{
			return Hook( RecalculateTags_original, &CBaseServerProxy::RecalculateTags );
		}

		static bool UnHookRecalculateTags( )
		{
			return UnHook( RecalculateTags_original );
		}

		LUA_FUNCTION_STATIC_MEMBER( SetGameTags )
		{
			if( LUA->IsType( 1, GarrysMod::Lua::Type::STRING ) )
				gametags_substitute = LUA->GetString( 1 );
			else
				gametags_substitute.clear( );

			LUA->PushBool(
				!gametags_substitute.empty( ) ?
				HookRecalculateTags( ) :
				UnHookRecalculateTags( )
			);
			return 1;
		}

	private:
		static FunctionPointers::CBaseServer_RecalculateTags_t RecalculateTags_original;
#if defined( __linux__ )
		using GetGameServer = ISteamGameServer *( * )( );
		static GetGameServer get_game_server;
#else
		static CSteam3Server *gameserver_context;
#endif
		static std::string gametags_substitute;
	};

	FunctionPointers::CBaseServer_RecalculateTags_t
		CBaseServerProxy::RecalculateTags_original = nullptr;
#if defined( __linux__ )
	CBaseServerProxy::GetGameServer CBaseServerProxy::get_game_server = nullptr;
#else
	CSteam3Server *CBaseServerProxy::gameserver_context = nullptr;
#endif
	std::string CBaseServerProxy::gametags_substitute;

	void PreInitialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		CBaseServerProxy::Initialize( LUA );
	}

	void Initialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		LUA->PushCFunction( CBaseServerProxy::SetGameTags );
		LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, "SetGameTags" );
	}

	void Deinitialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		CBaseServerProxy::UnHookRecalculateTags( );

		LUA->PushNil( );
		LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, "SetGameTags" );
	}
}
