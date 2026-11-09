#if defined( SOURCENET_LINUX_PUBLIC )

#include <tier0/dbg.h>

#include <cstdarg>
#include <cstdio>

// The SDK calls these APIs before Plat_ExitProcess on fatal errors.
// The public Linux x86 tier0 library provides the legacy Warning interface.
LoggingChannelID_t LOG_GENERAL = 0;

bool LoggingSystem_IsChannelEnabled( LoggingChannelID_t, LoggingSeverity_t )
{
	return true;
}

LoggingResponse_t LoggingSystem_Log( LoggingChannelID_t, LoggingSeverity_t, const char *format, ... )
{
	char message[2048];
	va_list args;
	va_start( args, format );
	std::vsnprintf( message, sizeof( message ), format, args );
	va_end( args );
	Warning( "%s\n", message );
	return LR_CONTINUE;
}

#endif
