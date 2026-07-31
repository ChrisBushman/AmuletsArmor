/*-------------------------------------------------------------------------*
 * File:  aa_net_test.c
 *-------------------------------------------------------------------------*/
/**
 * Standalone network reliability test harness for the AAServer/AmuletsArmor
 * IPX-over-UDP client transport (Source/Win32/ipx_client.cpp). Reuses that
 * real code directly rather than reimplementing the wire protocol, so a
 * connect/soak/stress run here exercises the exact same handshake, retry,
 * keepalive, and disconnect-detection logic the real game client uses --
 * without needing SDL video, asset loading, or a mouse device, none of
 * which the real game client's main() can currently be run without.
 *
 * Modes:
 *   aa-net-test connect <host> <port>
 *   aa-net-test soak    <host> <port> <seconds>
 *   aa-net-test stress  <host> <port> <count>
 *
 * <!-----------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Matches ipx_client.cpp's own SDL_net.h include selection exactly, since
   this harness needs to agree with it on which SDL_net/SDL headers (and
   therefore which UDPsocket/IPaddress ABI) are in play. */
#if defined(TARGET_UNIX) && defined(AA_REAL_SDL12)
#undef WIN32
#include <SDL_net.h>
#define WIN32 1
#elif defined(TARGET_UNIX)
#include <SDL2/SDL_net.h>
#else
#include <SDL_net.h>
#endif

#include "ipx_client.h"

typedef unsigned int T_word32;
#define TICKS_PER_SECOND 70

/* ipx_client.cpp calls this on every send/receive purely for debug
   logging (see PacketPrint's real implementation in PACKETPR.C, which
   this harness deliberately does not link -- it pulls in CMDQUEUE.H/
   SYNCPACK.H and the rest of the app-level engine, none of which this
   transport-layer harness needs). */
void PacketPrint(void *aData, unsigned int aSize)
{
    (void)aData;
    (void)aSize;
}

/* ipx_client.cpp's TARGET_UNIX branch forward-declares TickerGet() rather
   than pulling in the real TICKER.H/GENERAL.H chain (which conflicts with
   SDL2 headers) -- the real implementation is tied to the game's SDL
   timer. This harness provides its own, scaled to the same
   TICKS_PER_SECOND the real one uses (all of ipx_client.cpp's
   retry/timeout/keepalive math is expressed in those units), built on
   SDL_GetTicks() rather than POSIX clock_gettime()/CLOCK_MONOTONIC --
   the latter doesn't exist on Mac OS X 10.4 (added in 10.12), one of
   this harness's real targets (the PowerBook G4). SDL_GetTicks() is
   already a hard dependency here either way and works identically
   across every platform this project supports. */
T_word32 TickerGet(void)
{
    return (T_word32)(((unsigned long long)SDL_GetTicks() * TICKS_PER_SECOND) / 1000ULL);
}

static void Usage(const char *argv0)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s [--loss=N] connect <host> <port>\n"
        "  %s [--loss=N] soak    <host> <port> <seconds>\n"
        "  %s [--loss=N] stress  <host> <port> <count>\n"
        "\n"
        "--loss=N simulates N%% packet loss on the client's own outbound\n"
        "traffic (via SDL_net's SDLNet_UDP_SetPacketLoss) -- exercises the\n"
        "connect retry logic against real loss instead of assuming it works.\n",
        argv0, argv0, argv0);
}

/* Scans argv for a leading "--loss=N", removes it in place if found, and
   returns the percentage (0 if not given). Checked once in main() before
   dispatching to a command, so it works uniformly across every mode. */
static int ParseLossFlag(int *argc, char **argv)
{
    int percent = 0;
    int w = 0;

    for (int r = 0; r < *argc; r++) {
        if (strncmp(argv[r], "--loss=", 7) == 0) {
            percent = atoi(argv[r] + 7);
        } else {
            argv[w++] = argv[r];
        }
    }
    *argc = w;

    return percent;
}

static int InitNet(void)
{
    if (SDL_Init(0) == -1) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 0;
    }
    if (SDLNet_Init() == -1) {
        fprintf(stderr, "SDLNet_Init: %s\n", SDLNet_GetError());
        return 0;
    }
    return 1;
}

/* One connect attempt, timed. Returns 1 on success, 0 on failure. */
static int DoConnect(const char *host, int port, double *outSeconds)
{
    T_word32 start, end;
    int ok;

    IPXSetPort(port);
    start = TickerGet();
    ok = IPXConnectToServer(host);
    end = TickerGet();

    if (outSeconds)
        *outSeconds = (double)(end - start) / TICKS_PER_SECOND;

    return ok;
}

static int CmdConnect(int argc, char **argv)
{
    double elapsed;
    int ok;

    if (argc < 4) {
        Usage(argv[0]);
        return 1;
    }
    if (!InitNet())
        return 1;

    ok = DoConnect(argv[2], atoi(argv[3]), &elapsed);
    if (ok) {
        printf("CONNECT OK in %.2fs\n", elapsed);
        return 0;
    } else {
        printf("CONNECT FAILED after %.2fs\n", elapsed);
        return 1;
    }
}

static int CmdSoak(int argc, char **argv)
{
    T_word32 startTick, nowTick, lastStatusTick;
    int soakSeconds;
    char buffer[2048];
    unsigned int length;
    int wasTimedOut = 0;

    if (argc < 5) {
        Usage(argv[0]);
        return 1;
    }
    if (!InitNet())
        return 1;

    soakSeconds = atoi(argv[4]);

    if (!DoConnect(argv[2], atoi(argv[3]), NULL)) {
        printf("CONNECT FAILED, aborting soak\n");
        return 1;
    }
    printf("CONNECT OK, soaking for %ds -- watching for a silent drop "
           "(threshold matches WINDTALK.C's DirectTalkGetLineStatus: 10s)\n",
           soakSeconds);

    startTick = TickerGet();
    lastStatusTick = startTick;
    for (;;) {
        nowTick = TickerGet();
        if ((nowTick - startTick) >= (T_word32)(soakSeconds * TICKS_PER_SECOND))
            break;

        IPXClientPoll(buffer, &length);

        {
            T_word32 sinceRecv = IPXGetTicksSinceLastRecv();
            int timedOut = (sinceRecv > (10 * TICKS_PER_SECOND));
            if (timedOut != wasTimedOut) {
                printf("[%.1fs] line status -> %s (silence: %.2fs)\n",
                       (double)(nowTick - startTick) / TICKS_PER_SECOND,
                       timedOut ? "TIMED_OUT" : "CONNECTED",
                       (double)sinceRecv / TICKS_PER_SECOND);
                wasTimedOut = timedOut;
            }
            if ((nowTick - lastStatusTick) >= (5 * TICKS_PER_SECOND)) {
                printf("[%.1fs] alive, last recv %.2fs ago\n",
                       (double)(nowTick - startTick) / TICKS_PER_SECOND,
                       (double)sinceRecv / TICKS_PER_SECOND);
                lastStatusTick = nowTick;
            }
        }
    }

    printf("SOAK COMPLETE (%s at end)\n", wasTimedOut ? "TIMED_OUT" : "CONNECTED");
    return wasTimedOut ? 1 : 0;
}

static int CmdStress(int argc, char **argv)
{
    int count, i, successes = 0;
    double elapsed, totalElapsed = 0.0;

    if (argc < 5) {
        Usage(argv[0]);
        return 1;
    }
    if (!InitNet())
        return 1;

    count = atoi(argv[4]);

    for (i = 0; i < count; i++) {
        int ok = DoConnect(argv[2], atoi(argv[3]), &elapsed);
        totalElapsed += elapsed;
        printf("[%d/%d] %s in %.2fs\n", i + 1, count,
               ok ? "OK" : "FAILED", elapsed);
        if (ok)
            successes++;
    }

    printf("STRESS COMPLETE: %d/%d succeeded (%.1f%%), avg %.2fs\n",
           successes, count, 100.0 * successes / count, totalElapsed / count);

    return (successes == count) ? 0 : 1;
}

int main(int argc, char **argv)
{
    int lossPercent = ParseLossFlag(&argc, argv);
    if (lossPercent)
        IPXSetSimulatedPacketLoss(lossPercent);

    if (argc < 2) {
        Usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "connect") == 0)
        return CmdConnect(argc, argv);
    if (strcmp(argv[1], "soak") == 0)
        return CmdSoak(argc, argv);
    if (strcmp(argv[1], "stress") == 0)
        return CmdStress(argc, argv);

    Usage(argv[0]);
    return 1;
}
