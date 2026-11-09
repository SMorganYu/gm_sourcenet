#pragma once

#include <bitbuf.h>
#include <netadr.h>
#include <utlvector.h>
#include <inetchannel.h>

#include <cstddef>
#include <cstdint>

// 0 == regular, 1 == file stream
#define MAX_STREAMS 2

// Flow control bytes per second limits
#define MAX_RATE 20000
#define MIN_RATE 1000

// Default data rate
#define DEFAULT_RATE 10000

// NETWORKING INFO

// Number of bits to use for message type
#define NET_MESSAGE_BITS 6

// This is the packet payload without any header bytes (which are attached for actual sending)
#define	NET_MAX_PAYLOAD 80000

#define NET_FRAMES_BACKUP 64		// must be power of 2

// This is the payload plus any header info (excluding UDP header)

// Packet header is:
//  4 bytes of outgoing seq
//  4 bytes of incoming seq
//  1 byte of padding and flags
//  and for each stream
// {
//  byte (on/off)
//  int (fragment id)
//  short (startpos)
//  short (length)
// }
#define HEADER_BYTES ( 8 + MAX_STREAMS * 9 )

// Pad this to next higher 16 byte boundary
// This is the largest packet that can come in/out over the wire, before processing the header
//  bytes will be stripped by the networking channel layer
#define NET_MAX_MESSAGE PAD_NUMBER( ( NET_MAX_PAYLOAD + HEADER_BYTES ), 16 )

// NOTE: Above should be 96k, may need tweaking

typedef enum
{
	NS_CLIENT,
	NS_SERVER
} netsrc_t;

#define MAX_FRAGMENT_SIZE 256

#define MAX_SUBCHANNELS 8

#define MAX_FILE_SIZE 0x3FFFFFF

#define FRAG_NORMAL_STREAM 0
#define FRAG_FILE_STREAM 1

// max length of a filesystem pathname
#define MAX_OSPATH 260

typedef void *FileHandle_t;

typedef struct subchannel_s
{
	int32_t frag_ofs[MAX_STREAMS];	// offset of fragments to send (0x00, 0x04)
	int32_t frag_num[MAX_STREAMS];	// amount of fragments to send (0x08, 0x0C)
	int32_t sequence;				// outgoing seq. at time of sending (0x10)
	int32_t state;					// 0 = idle, 1 = needs sending, 2 = ready?, 3 = ??? (0x14)
	int32_t index;					// channel index (0-7) (0x18)
} subchannel_t;

typedef struct netpacket_s
{
	netadr_t from;				// sender IP (0x00)
	int32_t source;				// received source (0x0C)
	double received;			// received time (0x10)
	uint8_t *data;				// pointer to raw packet data (0x18)
	bf_read message;			// easy bitbuf data access (0x1C/28)
	int32_t size;				// size in bytes (0x40/64)
	int32_t wiresize;			// size in bytes before decompression (0x44/68)
	bool stream;				// was send as stream (0x48/72)
	struct netpacket_s *pNext;	// for internal use, should be NULL in public (0x4C/76)
} netpacket_t;

#if defined( SOURCENET_SERVER ) && ( defined( _WIN32 ) || defined( __linux__ ) )
namespace NetChannelLayout
{
	// Offsets in the supported dedicated engines, selected at module load.
#if defined( _WIN64 ) || defined( __x86_64__ )
	constexpr size_t reliable = 0x28, unreliable = 0x58, voice = 0x88;
	constexpr size_t address = 0xC4, connected = 0xE0, waiting = 0xF8;
	constexpr size_t incoming = 0x138, subchannels = 0x3B8, fileMode = 0x57C;
	constexpr size_t name = 0x2744, messages = 0x2778;
#elif defined( _WIN32 )
	constexpr size_t reliable = 0x20, unreliable = 0x44, voice = 0x68;
	constexpr size_t address = 0x98, connected = 0xB0, waiting = 0xC8;
	constexpr size_t incoming = 0xF0, subchannels = 0x350, fileMode = 0x514;
	constexpr size_t name = 0x26D4, messages = 0x2700;
#else
	constexpr size_t reliable = 0x20, unreliable = 0x44, voice = 0x68;
	constexpr size_t address = 0x98, connected = 0xB0, waiting = 0xC4;
	constexpr size_t incoming = 0xEC, subchannels = 0x34C, fileMode = 0x510;
	constexpr size_t name = 0x26C8, messages = 0x26F4;
#endif
}
#endif

class CNetChan : public INetChannel
{
public:
	typedef struct dataFragments_s
	{
		FileHandle_t hfile;			// file transfer handle (0x0)
		char filename[MAX_PATH];	// file transfer name (0x4)
		uint8_t *buffer;			// buffer transfer (0x108)
		uint32_t bytes;				// transfer bytes (0x10C)
		uint32_t bits;				// transfer bits (0x110)
		uint32_t transferid;		// file transfer ID (0x114)
		bool compressed;			// fragments are compressed (0x118)
		uint32_t actualsize;		// size when decompressed (0x11C)
		bool stream;				// send as stream (0x120)
		int32_t total;				// total amount of fragments to be sent (0x124)
		int32_t progress;			// amount of fragments sent so far (0x128)
		int32_t num;				// number of fragments to send when next possible (0x12C)
	} dataFragments_t;

	// Client's now store the command they sent to the server and the entire results set of
	//  that command.
	typedef struct netframe_s
	{
		// Data received from server
		float time;			// net_time received/send
		int size;			// total size in bytes
		float latency;		// raw ping for this packet, not cleaned. set when acknowledged otherwise -1.
		float avg_latency;	// averaged ping for this packet
		bool valid;			// false if dropped, lost, flushed
		int choked;			// number of previously chocked packets
		int dropped;
		float m_flInterpolationAmount;
		unsigned short msggroups[INetChannelInfo::TOTAL];	// received bytes for each message group
	} netframe_t;

	typedef struct
	{
		float nextcompute;	// Time when we should recompute k/sec data
		float avgbytespersec;	// average bytes/sec
		float avgpacketspersec;// average packets/sec
		float avgloss;		// average packet loss [0..1]
		float avgchoke;		// average packet choke [0..1]
		float avglatency;		// average ping, not cleaned
		float latency;		// current ping, more accurate also more jittering
		int totalpackets;	// total processed packets
		int totalbytes;		// total processed bytes
		int currentindex;		// current frame index
		netframe_t frames[NET_FRAMES_BACKUP]; // frame history
		netframe_t *currentframe;	// current frame
	} netflow_t;

#if defined( SOURCENET_SERVER ) && ( defined( _WIN32 ) || defined( __linux__ ) )
	uint8_t channelHeader[NetChannelLayout::reliable - sizeof( INetChannel )];
	bf_write m_StreamReliable;
	uint8_t reliableStorage[NetChannelLayout::unreliable - NetChannelLayout::reliable - sizeof( bf_write )];
	bf_write m_StreamUnreliable;
	uint8_t unreliableStorage[NetChannelLayout::voice - NetChannelLayout::unreliable - sizeof( bf_write )];
	bf_write m_StreamVoice;
	uint8_t voiceAndSocketStorage[NetChannelLayout::address - NetChannelLayout::voice - sizeof( bf_write )];
	netadr_t remote_address;
	float last_received;
	double connect_time;
	int32_t m_Rate;
#if defined( _WIN32 ) || defined( __x86_64__ )
	uint8_t ratePadding[4];
#endif
	double clearTime;
	CUtlVector<dataFragments_t *> m_WaitingList[MAX_STREAMS];
	dataFragments_t m_ReceiveList[MAX_STREAMS];
	subchannel_s m_SubChannels[MAX_SUBCHANNELS];
	uint8_t extraSubChannelsAndFileState[NetChannelLayout::fileMode - NetChannelLayout::subchannels - sizeof( m_SubChannels )];
	bool m_bFileBackgroundTranmission;
	bool m_bUseCompression;
	uint8_t flowState[NetChannelLayout::name - NetChannelLayout::fileMode - 2];
	char m_Name[32];
	uint32_t m_ChallengeNr;
	float m_Timeout;
	uint8_t messageHandlerState[NetChannelLayout::messages - NetChannelLayout::name - 40];
	CUtlVector<INetMessage *> m_NetMessages;
#else
	bool m_bProcessingMessages;
	bool m_bShouldDelete;

	// last send outgoing sequence number
	int32_t m_nOutSequenceNr;
	// last received incoming sequnec number
	int32_t m_nInSequenceNr;
	// last received acknowledge outgoing sequnce number
	int32_t m_nOutSequenceNrAck;

	// state of outgoing reliable data (0/1) flip flop used for loss detection
	int32_t m_nOutReliableState;
	// state of incoming reliable data
	int32_t m_nInReliableState;

	//number of choked packets
	int32_t m_nChokedPackets;

	// Reliable data buffer, send which each packet (or put in waiting list)
	bf_write m_StreamReliable;
	CUtlMemory<byte> m_ReliableDataBuffer;

	// unreliable message buffer, cleared which each packet
	bf_write m_StreamUnreliable;
	CUtlMemory<byte> m_UnreliableDataBuffer;

	bf_write m_StreamVoice;
	CUtlMemory<byte> m_VoiceDataBuffer;

	// don't use any vars below this (only in net_ws.cpp)

	// NS_SERVER or NS_CLIENT index, depending on channel.
	int32_t m_Socket;
	// TCP socket handle
	int32_t m_StreamSocket;

	// max size of reliable payload in a single packet
	uint32_t m_MaxReliablePayloadSize;

	// Address this channel is talking to.
	netadr_t remote_address;

	// For timeouts.  Time last message was received.
	float last_received;
	// Time when channel was connected.
	double connect_time;

	// Bandwidth choke
	// Bytes per second
	int32_t m_Rate;
	// If realtime > cleartime, free to send next packet
	double m_fClearTime;

	// waiting list for reliable data and file transfer
	CUtlVector<dataFragments_t *> m_WaitingList[MAX_STREAMS];
	// receive buffers for streams
	dataFragments_t m_ReceiveList[MAX_STREAMS];
	subchannel_s m_SubChannels[MAX_SUBCHANNELS];

	// increasing counter with each file request
	uint32_t m_FileRequestCounter;
	// if true, only send 1 fragment per packet
	bool m_bFileBackgroundTranmission;
	// if true, larger reliable data will be bzip compressed
	bool m_bUseCompression;

	// TCP stream state maschine:
	// true if TCP is active
	bool m_StreamActive;
	// STREAM_CMD_*
	int32_t m_SteamType;
	// each blob send of TCP as an increasing ID
	int32_t m_StreamSeqNr;
	// total length of current stream blob
	int32_t m_StreamLength;
	// length of already received bytes
	int32_t m_StreamReceived;
	// if receiving file, this is it's name
	char m_SteamFile[MAX_OSPATH];
	// Here goes the stream data (if not file). Only allocated if we're going to use it.
	CUtlMemory<byte> m_StreamData;

	// packet history
	netflow_t m_DataFlow[MAX_FLOWS];
	// total bytes for each message group
	int32_t m_MsgStats[INetChannelInfo::TOTAL];

	// packets lost before getting last update (was global net_drop)
	int32_t m_PacketDrop;

	// channel name
	char m_Name[32];

	// unique, random challenge number
	uint32_t m_ChallengeNr;

	// in seconds
	float m_Timeout;

	// who registers and processes messages
	INetChannelHandler *m_MessageHandler;
	// list of registered message
	CUtlVector<INetMessage *> m_NetMessages;
	// if != NULL points to a recording/playback demo object
	IDemoRecorder *m_DemoRecorder;
	int32_t m_nQueuedPackets;

	float m_flInterpolationAmount;
	float m_flRemoteFrameTime;
	float m_flRemoteFrameTimeStdDeviation;
	int32_t m_nMaxRoutablePayloadSize;

	int32_t m_nSplitPacketSequence;
#endif
};

#if defined( SOURCENET_SERVER ) && ( defined( _WIN32 ) || defined( __linux__ ) )
static_assert( sizeof( netadr_t ) == 0x14, "network address size" );
static_assert( sizeof( subchannel_s ) == 0x1C, "subchannel stride" );
static_assert( offsetof( CNetChan, m_StreamReliable ) == NetChannelLayout::reliable, "reliable stream" );
static_assert( offsetof( CNetChan, m_StreamUnreliable ) == NetChannelLayout::unreliable, "unreliable stream" );
static_assert( offsetof( CNetChan, m_StreamVoice ) == NetChannelLayout::voice, "voice stream" );
static_assert( offsetof( CNetChan, remote_address ) == NetChannelLayout::address, "channel address" );
static_assert( offsetof( CNetChan, last_received ) == NetChannelLayout::address + 0x14, "last received time" );
static_assert( offsetof( CNetChan, connect_time ) == NetChannelLayout::connected, "connect time" );
static_assert( offsetof( CNetChan, m_Rate ) == NetChannelLayout::connected + 8, "channel rate" );
static_assert( offsetof( CNetChan, m_WaitingList ) == NetChannelLayout::waiting, "waiting lists" );
static_assert( offsetof( CNetChan, m_ReceiveList ) == NetChannelLayout::incoming, "receive lists" );
static_assert( offsetof( CNetChan, m_SubChannels ) == NetChannelLayout::subchannels, "subchannels" );
static_assert( offsetof( CNetChan, m_bFileBackgroundTranmission ) == NetChannelLayout::fileMode, "file mode" );
static_assert( offsetof( CNetChan, m_bUseCompression ) == NetChannelLayout::fileMode + 1, "compression mode" );
static_assert( offsetof( CNetChan, m_Name ) == NetChannelLayout::name, "channel name" );
static_assert( offsetof( CNetChan, m_Timeout ) == NetChannelLayout::name + 36, "channel timeout" );
static_assert( offsetof( CNetChan, m_NetMessages ) == NetChannelLayout::messages, "net message vector" );
static_assert( sizeof( CNetChan::dataFragments_t ) == ( sizeof( void * ) == 8 ? 0x140 : 0x130 ), "fragment stride" );
static_assert( offsetof( CNetChan::dataFragments_t, buffer ) == ( sizeof( void * ) == 8 ? 0x110 : 0x108 ), "fragment buffer" );
static_assert( offsetof( CNetChan::dataFragments_t, total ) == ( sizeof( void * ) == 8 ? 0x130 : 0x124 ), "fragment count" );
#endif
