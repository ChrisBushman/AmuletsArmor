#ifdef __MWERKS__
/* The Mac OS 9 CodeWarrior build forces C compilation globally (its prefix
   does "#pragma cplusplus off", because AA's uppercase .C sources would
   otherwise wrongly compile as C++). This file is a real C++ unit
   (ipx_client is a class) -- flip C++ back on. No-op on other toolchains. */
#pragma cplusplus on
#endif

#include <stdio.h>
#include <string.h>
#include "ipx_client.h"
#include <time.h>

#if defined(TARGET_UNIX) && defined(AA_REAL_SDL12)
/* Real SDL 1.2 (e.g. Tigerbrew on PPC/Tiger) installs headers at the
   classic bare path, not namespaced under SDL2/ like Homebrew's
   sdl12-compat + SDL2 combo on modern macOS. SDL_net.h pulls in SDL.h
   itself, so it needs the same WIN32-hiding as every other real-SDL-1.2
   include site in this codebase (see Include/OPTIONS.H for why). */
#undef WIN32
#include <SDL_net.h>
#define WIN32 1
#elif defined(TARGET_UNIX)
#include <SDL2/SDL_net.h>
#else
/* Temporarily restore normal alignment for SDL_net.h (which pulls in
   <windows.h>): this project forces 1-byte struct packing globally
   (StructMemberAlignment/#pragma pack(1) below, MSVC's equivalent of
   this codebase's GCC_ATTRIBUTE(packed) used a few lines down), and
   parsing windows.h under that setting breaks several of its own
   internal compile-time size checks (see Build/Windows/VC2013/AA/
   WINPACKFIX.H's comment for the full story). */
#ifdef _MSC_VER
#pragma pack(push, 8)
#endif
#include "SDL_net.h"
#ifdef _MSC_VER
#pragma pack(pop)
#endif
#endif

/* GCC_ATTRIBUTE(packed) (used a few lines down, e.g. struct PackedIP)
   is a no-op under MSVC -- pack(1) is what actually packs those
   structs there. Set only now, after every system/SDL header above
   has already been parsed under normal alignment. */
#ifdef _MSC_VER
#pragma pack(1)
#endif
extern "C" {
#ifdef TARGET_UNIX
/* Skip TICKER.H on macOS: its include chain (GENERAL.H → OPTIONS.H → SDL1)
 * conflicts with SDL2 headers already included above.  Forward-declare only
 * the two symbols ipx_client.cpp uses from that chain. */
typedef unsigned int T_word32;
extern T_word32 TickerGet(void);
#define TICKS_PER_SECOND 70
#else
#include "TICKER.H"
#endif

extern void PacketPrint(void *aData, unsigned int aSize);

#define IPXBUFFERSIZE 1424

#if defined(TARGET_UNIX) && !defined(__MWERKS__)
#define GCC_ATTRIBUTE(x) __attribute__((x))
#else
/* CodeWarrior (and non-GCC) has no __attribute__; the on-wire IPX structs
   below are instead packed with #pragma options align=packed. */
#define GCC_ATTRIBUTE(x) /* attribute not supported */
#endif
#define GCC_UNLIKELY(x) (x)
#define GCC_LIKELY(x) (x)

#define INLINE __forceinline
#define DB_FASTCALL __fastcall

#if defined(_MSC_VER) && (_MSC_VER >= 1400) 
#pragma warning(disable : 4996) 
#endif


/* The internal types */
typedef  unsigned char		Bit8u;
typedef    signed char		Bit8s;
typedef unsigned short		Bit16u;
typedef   signed short		Bit16s;
typedef  unsigned long		Bit32u;
typedef    signed long		Bit32s;
#ifdef TARGET_UNIX
typedef unsigned long long	Bit64u;
typedef   signed long long	Bit64s;
#else
typedef unsigned __int64	Bit64u;
typedef   signed __int64	Bit64s;
#endif
typedef unsigned int		Bitu;
typedef signed int			Bits;

typedef Bit32u PhysPt;
typedef Bit8u * HostPt;
typedef Bit32u RealPt;

typedef Bit32s MemHandle;

#define LOG_MSG printf

#if defined(__MWERKS__)
#pragma options align=packed     /* 1-byte pack the on-wire IPX structs (CW's
                                    equivalent of the GCC_ATTRIBUTE(packed)) */
#endif
struct PackedIP {
	Uint32 host;
	Uint16 port;
} GCC_ATTRIBUTE(packed);

struct nodeType {
	Uint8 node[6];
} GCC_ATTRIBUTE(packed) ;

struct IPXHeader {
	Uint8 checkSum[2];
	Uint8 length[2];
	Uint8 transControl; // Transport control
	Uint8 pType; // Packet type

	struct transport {
		Uint8 network[4];
		union addrtype {
			nodeType byNode;
			PackedIP byIP ;
		} GCC_ATTRIBUTE(packed) addr;
		Uint8 socket[2];
	} dest, src;

	Uint32 counter; //! Special frame counter that is ONLY in A&A.  Used to track order of packets (if order is needed)
} GCC_ATTRIBUTE(packed);
#if defined(__MWERKS__)
#pragma options align=reset      /* end of the packed on-wire IPX structs */
#endif

struct ipxnetaddr {
	Uint8 netnum[4];   // Both are big endian
	Uint8 netnode[6];
} localIpxAddr;

struct packetBuffer {
	Bit8u buffer[IPXBUFFERSIZE];
	Bit16s packetSize;  // Packet size remaining in read
	Bit16s packetRead;  // Bytes read of total packet
	bool inPacket;      // In packet reception flag
	bool connected;		// Connected flag
	bool waitsize;
};

#define CONVIP(hostvar) hostvar & 0xff, (hostvar >> 8) & 0xff, (hostvar >> 16) & 0xff, (hostvar >> 24) & 0xff
#define CONVIPX(hostvar) hostvar[0], hostvar[1], hostvar[2], hostvar[3], hostvar[4], hostvar[5]

static IPaddress ipxServConnIp;			// IPAddress for client connection to server
/* Historically 213 (the DOSBox IPX-tunnel convention), but that's a
   privileged port on Unix-like systems -- moved to an unprivileged
   default matching AAServer's DEFAULT_IPX_PORT so client/server agree
   without needing root. Still overridable via IPXSetPort(). */
static Bit16u udpPort = 21300;

void IPXSetPort(int port) {
    udpPort = (Bit16u)port;
}
static UDPsocket ipxClientSocket;
static int UDPChannel;						// Channel used by UDP connection
static Bit8u recvBuffer[IPXBUFFERSIZE];	// Incoming packet buffer
static Bit8u sendBuffer[IPXBUFFERSIZE];    // Incoming packet buffer
static unsigned char G_destinationAddr[6];
static T_word32 G_ipxFrameCounter = 0;

/* Ticks (TickerGet() units) of the last datagram actually received from
   the server, and of the last keepalive we sent -- used to detect a
   silently-dropped connection (dead NAT mapping, server-side eviction,
   network loss) from the client side, since nothing previously tracked
   this at all. Reset at connect time so a fresh session doesn't
   immediately look stale before any real traffic has flowed. */
static T_word32 G_lastRecvTick = 0;
static T_word32 G_lastKeepaliveSentTick = 0;

/* Test-only: lets a harness (Utils/aa_net_test.c) exercise the connect
   retry/keepalive logic against real simulated loss via SDL_net's own
   SDLNet_UDP_SetPacketLoss(), rather than just trusting it works. Not
   wired into any real gameplay UI/flow -- 0 (the default) is a no-op. */
static int G_simulatedPacketLossPercent = 0;

void IPXSetSimulatedPacketLoss(int percent)
{
    G_simulatedPacketLossPercent = percent;
}

static Bit16u swapByte(Bit16u sockNum) {
	return (((sockNum>> 8)) | (sockNum << 8));
}

/*--------------------------------------------------------------------------*
 * Routine: UnpackIP
 *--------------------------------------------------------------------------*/
/**
 * Utility routine to split apart a PackedIP from the 6 byte value into
 * two parts: The 4 byte IP, and the 2 byte port number.
 *
 * @param ipPack -- Packed IP to split
 * @param ipAddr -- ipAddress structure to fill with IP and port.
 *
 * <!-----------------------------------------------------------------------*/
void UnpackIP(PackedIP ipPack, IPaddress * ipAddr)
{
    ipAddr->host = ipPack.host;
    ipAddr->port = ipPack.port;
}

/*--------------------------------------------------------------------------*
 * Routine: PackIP
 *--------------------------------------------------------------------------*/
/**
 * Utility routine to combine the 4 byte IP, and the 2 byte port number
 * into a single 6 byte number.
 *
 * @param ipPack -- Packed IP to split
 * @param ipAddr -- ipAddress structure to fill with IP and port.
 *
 * <!-----------------------------------------------------------------------*/
void PackIP(IPaddress ipAddr, PackedIP *ipPack)
{
    ipPack->host = ipAddr.host;
    ipPack->port = ipAddr.port;
}

/*--------------------------------------------------------------------------*
 * Routine: _IPXPingAck
 *--------------------------------------------------------------------------*/
/**
 * Because the UDP packets may also be used for special purposes (like ping)
 * handle those special cases here.
 *
 * @param p_data -- Pointer to place to receive data
 * @param size -- Size of returned data (no bigger than IPXBUFFERSIZE)
 *
 * @return Flag, 1=packet returned, else 0
 *
 * <!-----------------------------------------------------------------------*/
static void _IPXPingAck(IPaddress retAddr)
{
	IPXHeader regHeader;
	UDPpacket regPacket;
	int result;

	// Setup the checksum for the ping and size (just the header)
	SDLNet_Write16(0xffff, regHeader.checkSum);
	SDLNet_Write16(sizeof(regHeader), regHeader.length);

	// Set the destination network to be the return address (using network 0)
	SDLNet_Write32(0, regHeader.dest.network);
	PackIP(retAddr, &regHeader.dest.addr.byIP);
	SDLNet_Write16(0x2, regHeader.dest.socket);

	// We are the source (localIpx)
	SDLNet_Write32(0, regHeader.src.network);
	memcpy(regHeader.src.addr.byNode.node, localIpxAddr.netnode, sizeof(regHeader.src.addr.byNode.node));
	SDLNet_Write16(0x2, regHeader.src.socket);

	// This is a ping packet
	regHeader.transControl = 0;
	regHeader.pType = 0x0;

	// Prepare the packet structure for SDL_Net
	regPacket.data = (Uint8 *)&regHeader;
	regPacket.len = sizeof(regHeader);
	regPacket.maxlen = sizeof(regHeader);
	regPacket.channel = UDPChannel;

	// Send the packet.
	result = SDLNet_UDP_Send(ipxClientSocket, regPacket.channel, &regPacket);

	// Report error on failures
	if (result == 0) {
	    LOG_MSG("UDP packet send fail!");
	}
}

/*--------------------------------------------------------------------------*
 * Routine: _IPXPingAck
 *--------------------------------------------------------------------------*/
/**
 * Because the UDP packets may also be used for special purposes (like ping)
 * handle those special cases here.
 *
 * @param p_data -- Pointer to place to receive data
 * @param size -- Size of returned data (no bigger than IPXBUFFERSIZE)
 *
 * @return Flag, 1=packet returned, else 0
 *
 * <!-----------------------------------------------------------------------*/
void IPXSendPacket(char const *p_data, unsigned int size)
{
    UDPpacket regPacket;
    IPXHeader& regHeader = *((IPXHeader *)&sendBuffer);
    int result;
	T_word32 tick = clock();

	regHeader.src.socket[0] = 0x86;
    regHeader.src.socket[1] = 0x9C;
    regHeader.dest.socket[0] = 0x86;
    regHeader.dest.socket[1] = 0x9C;

#if 0
	printf("IPXSendPacket: [");
	for (int i=0; i<size; i++) {
		printf("%02X ", (unsigned char)p_data[i]);
	}
	printf("]\n");
#endif
#if 1
	printf("%02d:%02d:%02d.%03d IPX->", tick/3600000, (tick/60000) % 60, (tick/1000) % 60, tick%1000);
	PacketPrint((void *)p_data, size);
#endif
    if (size >= (IPXBUFFERSIZE - sizeof(IPXHeader))) {
        LOG_MSG("IPX: Packet too big!");
        return;
    }

    // Setup the checksum for the ping and size (just the header)
    SDLNet_Write16(0xffff, regHeader.checkSum);
    SDLNet_Write16(sizeof(regHeader) + size, regHeader.length);

    // Set the destination network to be the return address (using network 0)
    SDLNet_Write32(0, regHeader.dest.network);
    memcpy(regHeader.dest.addr.byNode.node, G_destinationAddr, 6);
    SDLNet_Write16(0x869C, regHeader.dest.socket);

    // We are the source (localIpx)
    SDLNet_Write32(0, regHeader.src.network);
    memcpy(regHeader.src.addr.byNode.node, localIpxAddr.netnode,
            sizeof(regHeader.src.addr.byNode.node));
    SDLNet_Write16(0x869C, regHeader.src.socket);

    // Copy over the data to send
    memcpy(sendBuffer + sizeof(IPXHeader), p_data, size);

    // Add a stamp on the packet number (in the old days, I was afraid
    // that the packets would get out of order and need to be sorted, so
    // I added a frame counter.  This really was stupid and not needed).
    regHeader.counter = G_ipxFrameCounter++;

    // Unspecified type
    regHeader.transControl = 0;
    regHeader.pType = 0x0;

    // Prepare the packet structure for SDL_Net
    // and direct back to the server
    regPacket.data = (Uint8 *)&regHeader;
    regPacket.len = sizeof(regHeader) + size;
    regPacket.maxlen = sizeof(regHeader) + size;
    regPacket.channel = UDPChannel;
    regPacket.address = ipxServConnIp;

    // Send the packet.
    result = SDLNet_UDP_Send(ipxClientSocket, regPacket.channel, &regPacket);

    // Report error on failures
    if (result == 0) {
        LOG_MSG("UDP packet send fail!");
    }
}



/*--------------------------------------------------------------------------*
 * Routine: _IPXHandleSpecialPacket
 *--------------------------------------------------------------------------*/
/**
 * Because the UDP packets may also be used for special purposes (like ping)
 * handle those special cases here.
 *
 * @param p_data -- Pointer to place to receive data
 * @param size -- Size of returned data (no bigger than IPXBUFFERSIZE)
 *
 * @return Flag, 1=packet processed, else 0
 *
 * <!-----------------------------------------------------------------------*/
static int _IPXHandleSpecialPacket(Bit8u *buffer, Bit16s bufSize)
{
	//ECBClass *useECB;
	//ECBClass *nextECB;
	Bit16u *bufword = (Bit16u *)buffer;
	Bit16u useSocket = swapByte(bufword[8]);
	IPXHeader * tmpHeader;
	tmpHeader = (IPXHeader *)buffer;

	// Check to see if ping packet on socket 2
	if (useSocket == 0x2) {
		// Is this a broadcast?
		if((tmpHeader->dest.addr.byIP.host == 0xffffffff) &&
			(tmpHeader->dest.addr.byIP.port == 0xffff)) {
			// Yes.  We should return the ping back to the sender
			IPaddress tmpAddr;

			UnpackIP(tmpHeader->src.addr.byIP, &tmpAddr);
			_IPXPingAck(tmpAddr);

			return 1;
		}
	}

	return 0;
}

/*--------------------------------------------------------------------------*
 * Routine: IPXClientPoll
 *--------------------------------------------------------------------------*/
/**
 * Check if a packet has arrived.  If so, return what was found and return
 * a flag.
 *
 * @param p_data -- Pointer to place to receive data
 * @param size -- Size of returned data (no bigger than IPXBUFFERSIZE)
 *
 * @return Flag, 1=packet returned, else 0
 *
 * <!-----------------------------------------------------------------------*/
int IPXClientPoll(char *p_data, unsigned int *size)
{
	int numrecv;
	UDPpacket inPacket;
	IPXHeader *p_header;
	inPacket.data = (Uint8 *)recvBuffer;
	inPacket.maxlen = IPXBUFFERSIZE;
	inPacket.channel = UDPChannel;
	T_word32 tick = clock();

	// Keep the session alive with a periodic round-trip regardless of
	// what else is happening -- some game states (e.g. real-time 3D
	// dungeon play) don't otherwise generate guaranteed regular traffic,
	// unlike the hard-coded-form UI's own unrelated periodic ping, which
	// only fires in that one mode. This runs every tick via the same
	// chain IPXClientPoll itself is called from (UpdateCmdqueue() ->
	// CmdQUpdateAllReceives() -> ... ), so it's unconditional.
#define IPX_KEEPALIVE_INTERVAL (2 * TICKS_PER_SECOND)
	if (ipxClientSocket
	        && ((TickerGet() - G_lastKeepaliveSentTick) >= IPX_KEEPALIVE_INTERVAL)) {
	    IPXSendKeepalive();
	    G_lastKeepaliveSentTick = TickerGet();
	}

	// Its amazing how much simpler UDP is than TCP
	// Is there a packet to process?
	numrecv = SDLNet_UDP_Recv(ipxClientSocket, &inPacket);
    if (numrecv) {
        // Any successful receive -- special/ack/payload-bearing alike --
        // proves the path to the server is still alive; this is the only
        // peer we ever talk to directly, so it's a sufficient liveness
        // signal on its own (see IPXGetTicksSinceLastRecv()).
        G_lastRecvTick = TickerGet();

        // Is the received packet big enough to be an IPX packet over UDP?
        if (inPacket.len >= sizeof(IPXHeader)) {
            // Get access to the IPX header that is at the start of the UDP
            // packet data.
            p_header = (IPXHeader *)inPacket.data;

            // Process the packet for any special actions (like echo for ping)
            if (_IPXHandleSpecialPacket(inPacket.data, inPacket.len)) {
                // If handled by the special routines, this packet is
                // not for us to process.  Stop here and signal no data packet.
                return 0;
            }

            // Not a special packet.  Looks like one of ours (we catch all
            // the others).
            // Is there data in this packet?
            if (inPacket.len > sizeof(IPXHeader)) {
                // Copy the IPX data payload over
                //
                // *size must be the number of bytes actually copied above
                // (the payload past the stripped IPX header), not the raw
                // inPacket.len this used to report -- that overstated the
                // real size by exactly sizeof(IPXHeader), and the caller
                // (PacketReceiveData) trusted it for its own memcpy into a
                // fixed-size T_packetLong buffer, overflowing it by that
                // same 34 bytes on every packet with a payload.
                *size = inPacket.len - sizeof(IPXHeader);
                memcpy((void *)p_data, (void *)&p_header[1], *size);
#if 1
	printf("%02d:%02d:%02d.%03d IPX<-", tick/3600000, (tick/60000) % 60, (tick/1000) % 60, tick%1000);
	PacketPrint((void *)p_data, *size);
#endif

                // Signal data was returned
                return 1;
            } else {
                // No data payload, no packet
                return 0;
            }
        } else {
            // Packet is too small to be an IPX packet
            // Ignore it.
            LOG_MSG("IPX: Ignore small IPX packet of size %d\n", inPacket.len);
            return 0;
        }
    } else {
        // no data
        return 0;
    }
}

/*--------------------------------------------------------------------------*
 * Routine: IPXGetUniqueAddress
 *--------------------------------------------------------------------------*/
/**
 * Get the unique address to this location (it's just the 6 byte MAC
 * address).
 *
 * @param p_unique -- Place to store the unique address.
 *
 * <!-----------------------------------------------------------------------*/
void IPXGetUniqueAddress(unsigned char address[6])
{
    // Just returned the local IPX's node address (should be MAC-like)
    memcpy(address, localIpxAddr.netnode, 6);
}

/*--------------------------------------------------------------------------*
 * Routine: IPXSetDestinationAddress
 *--------------------------------------------------------------------------*/
/**
 * Declare where we are sending the next packet.
 *
 * @param address -- Address of target (MAC address)
 *
 * <!-----------------------------------------------------------------------*/
void IPXSetDestinationAddress(unsigned char address[6])
{
    // Just returned the local IPX's node address (should be MAC-like)
    memcpy(G_destinationAddr, address, 6);
}

/*--------------------------------------------------------------------------*
 * Routine: IPXGetDestinationAddress
 *--------------------------------------------------------------------------*/
/**
 * Return where we are sending the next packet.
 *
 * @param address -- Address of target (MAC address)
 *
 * <!-----------------------------------------------------------------------*/
unsigned char *IPXGetDestinationAddress(void)
{
    // This is weak, but for now, return the destination address
    return G_destinationAddr;
}

/*--------------------------------------------------------------------------*
 * Routine: IPXSetDestinationAddressAll
 *--------------------------------------------------------------------------*/
/**
 * Set next packet to be broadcast
 *
 * <!-----------------------------------------------------------------------*/
void IPXSetDestinationAddressAll(void)
{
    // Broadcast is all 0xFF's
    memset(G_destinationAddr, 0xFF, 6);
}

/*--------------------------------------------------------------------------*
 * Routine: IBuildRegistrationPacket
 *--------------------------------------------------------------------------*/
/**
 * Fills in a zero-dest/zero-src "echo" packet -- AAServer treats this as a
 * registration request (fresh connect) or a re-registration/keepalive
 * (already-known sender) and always replies with an ack. Shared by
 * IPXConnectToServer()'s initial send/retries and IPXSendKeepalive(), so
 * both build the exact same wire content the server already knows how to
 * handle -- no server-side changes needed to support a keepalive.
 *
 * @param p_header -- Header to fill in and send
 * @param p_packet -- Packet structure to fill in and send
 *
 * @return Result of SDLNet_UDP_Send (0 = failed to send)
 *
 * <!-----------------------------------------------------------------------*/
static int IBuildRegistrationPacket(IPXHeader *p_header, UDPpacket *p_packet)
{
    // Reset checksum to 0xFFFF and set the length to the proper header size
    SDLNet_Write16(0xffff, p_header->checkSum);
    SDLNet_Write16(sizeof(*p_header), p_header->length);

    // An Echo packet with zeroed dest and src is a server registration
    // (or, for an already-known sender, a re-registration/keepalive) packet.
    SDLNet_Write32(0, p_header->dest.network);
    p_header->dest.addr.byIP.host = 0x0;
    p_header->dest.addr.byIP.port = 0x0;
    SDLNet_Write16(0x2, p_header->dest.socket);

    SDLNet_Write32(0, p_header->src.network);
    p_header->src.addr.byIP.host = 0x0;
    p_header->src.addr.byIP.port = 0x0;
    SDLNet_Write16(0x2, p_header->src.socket);
    p_header->transControl = 0;

    p_packet->data = (Uint8 *)p_header;
    p_packet->len = sizeof(*p_header);
    p_packet->maxlen = sizeof(*p_header);
    p_packet->channel = UDPChannel;
    p_packet->address = ipxServConnIp;

    return SDLNet_UDP_Send(ipxClientSocket, p_packet->channel, p_packet);
}

/*--------------------------------------------------------------------------*
 * Routine: IPXSendKeepalive
 *--------------------------------------------------------------------------*/
/**
 * Sends a re-registration/keepalive packet to the server so a session
 * with no other traffic (e.g. sitting idle on a hard-coded UI form, which
 * has its own unrelated periodic ping but only while in that mode) still
 * produces regular round-trips -- both to keep the server's own
 * connection-table timeout from expiring, and so the client has fresh
 * traffic to judge IPXGetTicksSinceLastRecv() against.
 *
 * @return Result of SDLNet_UDP_Send (0 = failed to send)
 *
 * <!-----------------------------------------------------------------------*/
int IPXSendKeepalive(void)
{
    UDPpacket packet;
    IPXHeader header;

    if (!ipxClientSocket)
        return 0;

    return IBuildRegistrationPacket(&header, &packet);
}

/*--------------------------------------------------------------------------*
 * Routine: IPXGetTicksSinceLastRecv
 *--------------------------------------------------------------------------*/
/**
 * @brief Ticks (TickerGet() units) since the last datagram was actually
 * received from the server -- 0 right after a fresh connect. Used by
 * WINDTALK.C's DirectTalkGetLineStatus() to detect a silently-dropped
 * connection instead of always reporting connected.
 *
 * <!-----------------------------------------------------------------------*/
T_word32 IPXGetTicksSinceLastRecv(void)
{
    return TickerGet() - G_lastRecvTick;
}

/*--------------------------------------------------------------------------*
 * Routine: IPXConnectToServer
 *--------------------------------------------------------------------------*/
/**
 * @brief     Attempt to connect to the server using the IPX over UDP
 * system.
 *
 * @param strAddr -- TCP/IP address of server
 *
 * @return Error code, 0=failure, 1=success
 *
 * <!-----------------------------------------------------------------------*/
int IPXConnectToServer(const char *strAddr)
{
	int numsent;
	UDPpacket regPacket;
	IPXHeader regHeader;
    Bits result;
    Bit32u ticks, elapsed;

	// First, determine where the target really is.
	if(SDLNet_ResolveHost(&ipxServConnIp, strAddr, (Bit16u)udpPort)) {
        LOG_MSG("IPX: Unable resolve connection to server\n");
        return 0;
    }

    // Determined the proper IP address of the target
    // Now, generate the MAC address we'll use for this computer.
    // This is made by zeroing out the first two
    // octets and then using the actual IP address for the last 4 octets.
    //
    // This idea is from the IPX over IP implementation as specified in RFC 1234:
    // http://www.faqs.org/rfcs/rfc1234.html
//TODO: Better MAC?

    // Create an anonymous UDP port
    ipxClientSocket = SDLNet_UDP_Open(0);
    if (!ipxClientSocket) {
        LOG_MSG("IPX: Unable to open socket\n");
        return 0;
    }

    // Bind UDP port to address to channel
    UDPChannel = SDLNet_UDP_Bind(ipxClientSocket, -1, &ipxServConnIp);
    if (UDPChannel == -1) {
        LOG_MSG("IPX: Channel not bound!");
        SDLNet_UDP_Close(ipxClientSocket);
        return 0;
    }

#if !defined(macintosh)
    /* SDLNet_UDP_SetPacketLoss is a debug packet-loss simulator absent from
       the classic-Mac SDL_net 1.2.5 (native Open Transport) build. */
    if (G_simulatedPacketLossPercent) {
        SDLNet_UDP_SetPacketLoss(ipxClientSocket, G_simulatedPacketLossPercent);
    }
#endif

    // Send registration echo packet to server.  If server doesn't get
    // this, client will not be registered
    numsent = IBuildRegistrationPacket(&regHeader, &regPacket);

    if(!numsent) {
        // Failed to send packet (didn't even go out!)
        LOG_MSG("IPX: Unable to connect to server: %s\n", SDLNet_GetError());
        SDLNet_UDP_Close(ipxClientSocket);
        return 0;
    }

    // Wait for return packet from server.  Might still get lost.
    // This will contain our IPX address and port num
    //
    // A single registration packet with one 5-second wait used to be all
    // this did -- but that packet (or the server's ack) routinely gets
    // lost on the very first exchange through a fresh NAT/firewall mapping,
    // and with no retry that failed the connection outright. Resend the
    // same registration packet on an interval until either a response
    // arrives or the total window elapses.
#define IPX_CONNECT_RETRY_INTERVAL (1 * TICKS_PER_SECOND)
#define IPX_CONNECT_TOTAL_TIMEOUT  (8 * TICKS_PER_SECOND)
    ticks = TickerGet();
    Bit32u lastSendTicks = ticks;

    while(true) {
        // Has the whole connect window elapsed?
        elapsed = TickerGet() - ticks;
        if(elapsed > IPX_CONNECT_TOTAL_TIMEOUT) {
            // Yes.  Timeout, stop here
            LOG_MSG("Timeout connecting to server at %s\n", strAddr);
            SDLNet_UDP_Close(ipxClientSocket);

            return 0;
        }

        // See if we got a response
        //CALLBACK_Idle();
        result = SDLNet_UDP_Recv(ipxClientSocket, &regPacket);
        if (result != 0) {
            // Yes, got a response on the UDP port.
            // Record the send's information as the net node and number (basically UDP IP and port)
            memcpy(localIpxAddr.netnode, regHeader.dest.addr.byNode.node, sizeof(localIpxAddr.netnode));
            memcpy(localIpxAddr.netnum, regHeader.dest.network, sizeof(localIpxAddr.netnum));
            break;
        }

        // No response yet -- resend the (unmodified) registration packet
        // on an interval rather than silently waiting out the whole window.
        if ((TickerGet() - lastSendTicks) >= IPX_CONNECT_RETRY_INTERVAL) {
            LOG_MSG("IPX: No response yet, retrying registration to %s...\n", strAddr);
            SDLNet_UDP_Send(ipxClientSocket, regPacket.channel, &regPacket);
            lastSendTicks = TickerGet();
        }
    }

    LOG_MSG("IPX: Connected to server.  IPX address is %d:%d:%d:%d:%d:%d\n", CONVIPX(localIpxAddr.netnode));

    // Fresh session: start the last-received clock now rather than at
    // whatever stale value a previous connection left it at, so
    // IPXGetTicksSinceLastRecv() doesn't immediately look stale.
    G_lastRecvTick = TickerGet();
    G_lastKeepaliveSentTick = G_lastRecvTick;

    //incomingPacket.connected = true;
    //TIMER_AddTickHandler(&IPXClientPoll);
    return 1;
}

} // extern C
