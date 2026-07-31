/*-------------------------------------------------------------------------*
 * File:  PACKETDT.C
 *-------------------------------------------------------------------------*/
/**
 * The low level "packets" in the networking are sent and received here.
 * They are classified into two sizes:  Short or Long.  This packet
 * layer interfaces to the Direct Talk communications layer.
 *
 * @addtogroup PACKET
 * @brief Packet Communications
 * @see http://www.amuletsandarmor.com/AALicense.txt
 * @{
 *
 *<!-----------------------------------------------------------------------*/
#include "PACKET.H"
#include "ENDIAN_AA.H"

/* Option to create the file RECEIVE.DAT to record all received bytes */
/* from the packet driver. */
//#define PACKET_CREATE_RECEIVE_FILE

/* Keep track of the number of the current packet.  Although this is mainly */
/* for debugging, we can use it to make sure packets are sent correctly.   */
/* If nothing else, we'll make sure everything is in order. */
static T_word32 G_packetID = 1 ;

static T_word16 IPacketComputeChecksum(T_packetEitherShortOrLong *p_packet) ;

/*-------------------------------------------------------------------------*
 * Routine:  PacketSendShort
 *-------------------------------------------------------------------------*/
/**
 *  PacketSendShort is called to small (about 16 byte) packets out the
 *  currently active communications port.
 *
 *  @param p_shortPacket -- Packet with data entry filled.  The
 *      rest of the fields will be filled out.
 *
 *  @return 0 if packet sent, -1 if not sent
 *
 *<!-----------------------------------------------------------------------*/
T_sword16 PacketSendShort(T_packetShort *p_shortPacket)
{
    T_sword16 code = -1 ;

    DebugRoutine("PacketSendShort") ;
    DebugCheck(p_shortPacket != NULL) ;

    /* Store the header information in the packet. */
    p_shortPacket->header.prefix = PACKET_PREFIX ;

    /* Make this a short packet. */
    p_shortPacket->header.packetLength = SHORT_PACKET_LENGTH ;

    /* Put the id for this packet in the packet. */
    p_shortPacket->header.id = G_packetID++ ;

    // Store the sender in the packet (needed for proper ack packets)
    DirectTalkGetUniqueAddress(&p_shortPacket->header.sender);

    /* Compute the checksum for this packet. */
    p_shortPacket->header.checksum =
        IPacketComputeChecksum((T_packetEitherShortOrLong *)p_shortPacket) ;

    /* id/checksum are computed above as host-native values; swap to wire
       (little-endian) byte order right before the bytes actually leave. */
    p_shortPacket->header.id = EndianLE32(p_shortPacket->header.id) ;
    p_shortPacket->header.checksum = EndianLE16(p_shortPacket->header.checksum) ;

    /* Actually send the data out the port. */
    DirectTalkSendData((T_byte8 *)p_shortPacket, sizeof(T_packetShort)) ;
    /* Note that the packet was sent. */
    code = 0 ;

    DebugEnd() ;

    return code ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PacketSendLong
 *-------------------------------------------------------------------------*/
/**
 *  PacketSendLong sends a long packet of 80 characters out the
 *  currently active communications port.
 *
 *  @param p_longPacket -- Packet with data entry filled.  The
 *      rest of the fields will be filled out.
 *
 *  @return 0 if packet sent, -1 if not sent
 *
 *<!-----------------------------------------------------------------------*/
T_sword16 PacketSendLong(T_packetLong *p_longPacket)
{
    T_sword16 code = -1 ;

    DebugRoutine("PacketSendLong") ;
    DebugCheck(p_longPacket != NULL) ;

    /* Store the header information in the packet. */
    p_longPacket->header.prefix = PACKET_PREFIX ;

    /* Make this a long packet. */
    p_longPacket->header.packetLength = LONG_PACKET_LENGTH ;

    /* Put the id for this packet in the packet. */
    p_longPacket->header.id = G_packetID++ ;

    // Store the sender in the packet (needed for proper ack packets)
    DirectTalkGetUniqueAddress(&p_longPacket->header.sender);

    /* Compute the checksum for this packet. */
    p_longPacket->header.checksum =
        IPacketComputeChecksum((T_packetEitherShortOrLong *)p_longPacket) ;

    /* id/checksum are computed above as host-native values; swap to wire
       (little-endian) byte order right before the bytes actually leave. */
    p_longPacket->header.id = EndianLE32(p_longPacket->header.id) ;
    p_longPacket->header.checksum = EndianLE16(p_longPacket->header.checksum) ;

    /* See if there is room to send the packet. */
    /* Actually send the data out the port. */
    DirectTalkSendData((T_byte8 *)p_longPacket, sizeof(T_packetLong)) ;

    /* Note that the packet was sent. */
    code = 0 ;

    DebugEnd() ;

    return code ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PacketSendAnyLength
 *-------------------------------------------------------------------------*/
/**
 *  PacketSendAnyLength sends a packet of any size up to a long packet
 *  size out the active communications port.
 *
 *  @param p_anyPacket -- packet to send.
 *
 *  @return 0 if packet sent, -1 if not sent
 *
 *<!-----------------------------------------------------------------------*/
T_sword16 PacketSendAnyLength(T_packetEitherShortOrLong *p_anyPacket)
{
    T_sword16 code = -1 ;

    DebugRoutine("PacketSendAnyLength") ;
    DebugCheck(p_anyPacket != NULL) ;
    DebugCheck(p_anyPacket->header.packetLength <= LONG_PACKET_LENGTH) ;

    /* Store the header information in the packet. */
    p_anyPacket->header.prefix = PACKET_PREFIX ;

    /* Make this a long packet. */
//    p_anyPacket->header.packetLength = LONG_PACKET_LENGTH ;

    /* Put the id for this packet in the packet. */
    p_anyPacket->header.id = G_packetID++ ;

    // Store the sender in the packet (needed for proper ack packets)
    DirectTalkGetUniqueAddress(&p_anyPacket->header.sender);

    /* Compute the checksum for this packet. */
    p_anyPacket->header.checksum = IPacketComputeChecksum(p_anyPacket) ;

    /* id/checksum are computed above as host-native values; swap to wire
       (little-endian) byte order right before the bytes actually leave.
       packetLength itself is a single byte, so reading it below is
       unaffected by this swap. */
    p_anyPacket->header.id = EndianLE32(p_anyPacket->header.id) ;
    p_anyPacket->header.checksum = EndianLE16(p_anyPacket->header.checksum) ;

    /* See if there is room to send the packet. */
    /* Actually send the data out the port. */
    DirectTalkSendData(
        (T_byte8 *)p_anyPacket,
        (T_word16)(sizeof(T_packetHeader) + p_anyPacket->header.packetLength)) ;

    /* Note that the packet was sent. */
    code = 0 ;

    DebugEnd() ;

    return code ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PacketSend
 *-------------------------------------------------------------------------*/
/**
 *  PacketSend sends either a short or a long packet out the current
 *  communications port.
 *
 *  @param p_packet -- packet to send.
 *
 *<!-----------------------------------------------------------------------*/
T_sword16 PacketSend(T_packetEitherShortOrLong *p_packet)
{
    T_sword16 status ;

    DebugRoutine("PacketSend") ;

    switch(p_packet->header.packetLength)  {
        case SHORT_PACKET_LENGTH:
            status = PacketSendShort((T_packetShort *)p_packet) ;
            break ;
        case LONG_PACKET_LENGTH:
            status = PacketSendLong((T_packetLong *)p_packet) ;
            break ;
        default:
            DebugCheck(p_packet->header.packetLength <= LONG_PACKET_LENGTH) ;
            status = PacketSendAnyLength(p_packet) ;
            break ;
    }

    DebugEnd() ;

    return status ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PacketSendPreassignedId
 *-------------------------------------------------------------------------*/
/**
 *  For CmdQueue's use only: sends a packet whose header.id was already
 *  assigned once via PacketSetId() and must stay exactly that value for
 *  every retransmission (CmdQUpdateAllReceives()'s ACK matching compares
 *  against it later and needs it stable and in host-native order).
 *
 *  PacketSendShort/Long/AnyLength -- and therefore PacketSend() -- always
 *  assign a *fresh* id via their own G_packetID counter and, worse,
 *  perform their EndianLE32/EndianLE16 wire-order swap in place on the
 *  caller's own struct. For a one-shot send that's harmless (nobody
 *  reads that struct's header again), but CmdQueue calls PacketSend()
 *  repeatedly on the very same persistent queue entry for retransmits --
 *  every retry clobbered the queue's id with a new one, and on a
 *  big-endian build the in-place swap left the *stored* id permanently
 *  byte-swapped after the very first send (EndianLE32 is a no-op on
 *  little-endian, which is exactly why this was invisible until a real
 *  big-endian PPC sent a lossless packet). Both defeat ACK matching --
 *  the queued id no longer matches what the wire/ACK actually carries --
 *  so the packet is never recognized as acked and retransmits forever,
 *  with the receiving side (no duplicate-id detection) displaying every
 *  retry as if it were a distinct new message.
 *
 *  This builds a throwaway local copy for the actual wire-order
 *  transmission and never writes back to *p_packet at all, so the
 *  queue's own copy of header.id (and everything else) stays exactly as
 *  CmdQueue set it, safe to send unlimited times.
 *
 *  @param p_packet -- Packet to send; header.id must already be set via
 *      PacketSetId() and is read, never modified.
 *
 *  @return 0 (matches PacketSend()'s always-0 in practice -- neither
 *      reports real transport failures, see DirectTalkSendData()).
 *
 *<!-----------------------------------------------------------------------*/
T_sword16 PacketSendPreassignedId(T_packetEitherShortOrLong *p_packet)
{
    T_packetLong localCopy ;

    DebugRoutine("PacketSendPreassignedId") ;
    DebugCheck(p_packet != NULL) ;

    memcpy(&localCopy, p_packet,
            sizeof(T_packetHeader) + p_packet->header.packetLength) ;

    localCopy.header.prefix = PACKET_PREFIX ;
    DirectTalkGetUniqueAddress(&localCopy.header.sender) ;
    /* header.id intentionally left as-is (copied above) -- that's the
       whole point of this function. */
    localCopy.header.checksum =
            IPacketComputeChecksum((T_packetEitherShortOrLong *)&localCopy) ;

    localCopy.header.id = EndianLE32(localCopy.header.id) ;
    localCopy.header.checksum = EndianLE16(localCopy.header.checksum) ;

    DirectTalkSendData((T_byte8 *)&localCopy,
            (T_byte8)(sizeof(T_packetHeader) + localCopy.header.packetLength)) ;

    DebugEnd() ;

    return 0 ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PacketSetId
 *-------------------------------------------------------------------------*/
/**
 *  PacketSetId sets the packet Id.  THIS SHOULD ONLY BE CALLED FROM
 *  WITHIN CMDQUEUE.
 *
 *  @param p_packet -- Pointer to packet whose ID must be
 *      p_packet        changed.
 *  @param packetID -- New ID to assign to the packet.
 *
 *<!-----------------------------------------------------------------------*/
T_void PacketSetId (T_packetEitherShortOrLong *p_packet, T_word32 packetID)
{
    p_packet->header.id = packetID;
}


/*-------------------------------------------------------------------------*
 * Routine:  IPacketComputeChecksum
 *-------------------------------------------------------------------------*/
/**
 *  IPacketComputeChecksum calculates a 16 bit checksum for the either
 *  short or long packet passed and returns that value.
 *
 *  @param packet -- Packet to compute checksum
 *
 *  @return Calculated checksum
 *
 *<!-----------------------------------------------------------------------*/
static T_word16 IPacketComputeChecksum(T_packetEitherShortOrLong *packet)
{
    T_word16 checksum ;
    T_word16 i ;

    DebugRoutine("IPacketComputeChecksum") ;
    DebugCheck(packet != NULL) ;

    /* Start out the checksum to equal the id of the block. */
    checksum = packet->header.id ;

    /* Add in the packet type. */
    checksum += packet->header.packetLength ;

    /* Loop the length of the data. */
    for (i=0; i<packet->header.packetLength; i++)  {
        /* If i is odd, then add.  Otherwise, do an exclusive-or to mix */
        /* up the bits. */
        if (i&1)
            checksum += packet->data[i] ;
        else
            checksum ^= packet->data[i] ;
    }

    DebugEnd() ;

    return checksum ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PacketGet
 *-------------------------------------------------------------------------*/
/**
 *  PacketGet is the routine called to retrieve a packet from the
 *  currently active communications port.  If no packet is found, a -1
 *  is returned.  If a packet is found, a 0 is returned.
 *
 *  @param packet -- Packet location to receive data.
 *      Note that you must have room allocated
 *      for a long packet in case either a
 *      long or a short packet is received.
 *      You will want to check the packet type
 *      if you do receive data.
 *
 *  @return Resultant flag.  A -1 means no
 *      packet was received.  A 0 means a
 *      packet was found.
 *
 *<!-----------------------------------------------------------------------*/
static T_packetLong newPacket ;
static E_Boolean newPacketFilled ;

T_sword16 PacketGet(T_packetLong *p_packet)
{
    T_sword16 status ;

    DebugRoutine("PacketGet") ;

    newPacketFilled = FALSE ;

    DirectTalkPollData() ;

    if (newPacketFilled)  {
        status = 0 ;
        *p_packet = newPacket ;
    } else  {
        status = -1 ;
    }

    DebugEnd() ;

    return status ;
}

#ifdef PACKET_CREATE_RECEIVE_FILE
static FILE *G_fpRecv ;

T_void PacketCloseReceiveFile(T_void)
{
    fclose(G_fpRecv) ;
}
#endif

T_void PacketReceiveData(T_void *p_data, T_word16 size)
{
    void PacketPrint(void *aData, unsigned int aSize);
    DebugRoutine("ConnectReceiveData") ;
    DebugCheck(p_data != NULL) ;

#ifdef PACKET_CREATE_RECEIVE_FILE
    if (G_fpRecv == NULL)  {
        G_fpRecv = fopen("receive.dat", "wb") ;
        atexit(PacketCloseReceiveFile) ;
    }

    fprintf(G_fpRecv, "CONNECT: Received %d data\n", size) ;
    for (i=0; i<size; i++)
        fprintf(G_fpRecv, "<%02X>", ((T_byte8 *)p_data)[i]) ;
    fprintf(G_fpRecv, "\n") ;
#endif

    if ((size < sizeof(T_packetHeader)) || (size > sizeof(newPacket))) {
        printf("PACKET: Ignoring packet with bad size (%d bytes)\n", size) ;
        DebugEnd() ;
        return ;
    }

    memcpy(&newPacket, p_data, size) ;

    /* Header id/checksum arrive in wire (little-endian) byte order; swap
       back to host-native right away so everything downstream of
       PacketGet() sees normal host-native values. */
    newPacket.header.id = EndianLE32(newPacket.header.id) ;
    newPacket.header.checksum = EndianLE16(newPacket.header.checksum) ;

    /* packetLength reflects however many bytes of data[] this packet
       claims to carry -- possibly corrupted at this point, so bound it
       against both the fixed data[] capacity and what was actually
       received before IPacketComputeChecksum() below indexes data[]
       with it. */
    if ((newPacket.header.packetLength > LONG_PACKET_LENGTH)
            || ((T_word16)(sizeof(T_packetHeader) + newPacket.header.packetLength) > size)) {
        printf("PACKET: Ignoring packet with invalid length field (%d)\n",
                newPacket.header.packetLength) ;
        DebugEnd() ;
        return ;
    }

    /* The checksum has always been computed and sent (see
       PacketSendShort/Long/AnyLength above), but never actually verified
       here -- a corrupted packet was silently accepted and handed to the
       game as if it were valid, which is worse than having no checksum
       at all. */
    if (IPacketComputeChecksum((T_packetEitherShortOrLong *)&newPacket)
            != newPacket.header.checksum) {
        printf("PACKET: Dropping packet with bad checksum (id=%u)\n",
                newPacket.header.id) ;
        DebugEnd() ;
        return ;
    }

    newPacketFilled = TRUE ;
    PacketPrint(p_data, size);

    DebugEnd() ;
}

/** @} */
/*-------------------------------------------------------------------------*
 * End of File:  PACKETDT.C
 *-------------------------------------------------------------------------*/
