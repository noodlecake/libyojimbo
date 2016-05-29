/*
    Yojimbo Client/Server Network Library.

    Copyright © 2016, The Network Protocol Company, Inc.
    
    All rights reserved.
*/

#ifndef YOJIMBO_SOCKET_INTERFACE_H
#define YOJIMBO_SOCKET_INTERFACE_H

#include "yojimbo_config.h"
#include "yojimbo_types.h"
#include "yojimbo_memory.h"
#include "yojimbo_common.h"
#include "yojimbo_crypto.h"
#include "yojimbo_packet.h"
#include "yojimbo_network.h"
#include "yojimbo_allocator.h"
#include "yojimbo_network_interface.h"
#include "yojimbo_packet_processor.h"

namespace yojimbo
{
    enum SocketInterfaceCounter
    {
        SOCKET_INTERFACE_COUNTER_PACKETS_SENT,
        SOCKET_INTERFACE_COUNTER_PACKETS_RECEIVED,
        SOCKET_INTERFACE_COUNTER_PACKETS_READ,
        SOCKET_INTERFACE_COUNTER_PACKETS_WRITTEN,
        SOCKET_INTERFACE_COUNTER_SEND_QUEUE_OVERFLOW,
        SOCKET_INTERFACE_COUNTER_RECEIVE_QUEUE_OVERFLOW,
        SOCKET_INTERFACE_COUNTER_READ_PACKET_FAILURES,
        SOCKET_INTERFACE_COUNTER_WRITE_PACKET_FAILURES,
        SOCKET_INTERFACE_COUNTER_ENCRYPT_PACKET_FAILURES,
        SOCKET_INTERFACE_COUNTER_DECRYPT_PACKET_FAILURES,
        SOCKET_INTERFACE_COUNTER_ENCRYPTED_PACKETS_READ,
        SOCKET_INTERFACE_COUNTER_ENCRYPTED_PACKETS_WRITTEN,
        SOCKET_INTERFACE_COUNTER_UNENCRYPTED_PACKETS_READ,
        SOCKET_INTERFACE_COUNTER_UNENCRYPTED_PACKETS_WRITTEN,
        SOCKET_INTERFACE_COUNTER_ENCRYPTION_MAPPING_FAILURES,
        SOCKET_INTERFACE_COUNTER_NUM_COUNTERS
    };

    class SocketInterface : public NetworkInterface
    {
        void * m_context;

        uint32_t m_protocolId;
        int m_sendQueueSize;
        int m_receiveQueueSize;

        Allocator * m_allocator;
        Socket * m_socket;
        PacketFactory * m_packetFactory;
        PacketProcessor * m_packetProcessor;

        struct PacketEntry
        {
            uint64_t sequence;
            Address address;
            Packet * packet;
        };

        Queue<PacketEntry> m_sendQueue;
        Queue<PacketEntry> m_receiveQueue;

        uint8_t * m_packetTypeIsEncrypted;
        uint8_t * m_packetTypeIsUnencrypted;

        struct EncryptionMapping
        {
            Address address;
            uint8_t sendKey[KeyBytes];
            uint8_t receiveKey[KeyBytes];
        };

        enum { MaxEncryptionMappings = 1024 };
        int m_numEncryptionMappings;
        EncryptionMapping m_encryptionMappings[MaxEncryptionMappings];

        uint64_t m_counters[SOCKET_INTERFACE_COUNTER_NUM_COUNTERS];

    protected:

        void ClearSendQueue();

        void ClearReceiveQueue();

        EncryptionMapping * FindEncryptionMapping( const Address & address )
        {
            for ( int i = 0; i < m_numEncryptionMappings; ++i )
            {
                if ( m_encryptionMappings[i].address == address )
                    return &m_encryptionMappings[i];
            }
            return NULL;
        }

    public:

        SocketInterface( Allocator & allocator,
                         PacketFactory & packetFactory, 
                         uint32_t protocolId,
                         uint16_t socketPort, 
                         SocketType socketType = SOCKET_TYPE_IPV6, 
                         int maxPacketSize = 4 * 1024,
                         int sendQueueSize = 1024,
                         int receiveQueueSize = 1024 );

        ~SocketInterface();

        bool IsError() const;

        int GetError() const;

        Packet * CreatePacket( int type );

        void DestroyPacket( Packet * packet );

        void SendPacket( const Address & address, Packet * packet, uint64_t sequence = 0 );

        Packet * ReceivePacket( Address & from, uint64_t * sequence = NULL );

        void WritePackets( double time );

        void ReadPackets( double time );

        int GetMaxPacketSize() const;

        void SetContext( void * context );

        void EnablePacketEncryption();

        void DisableEncryptionForPacketType( int type );

        bool IsEncryptedPacketType( int type ) const;

        bool AddEncryptionMapping( const Address & address, const uint8_t * sendKey, const uint8_t * receiveKey );

        bool RemoveEncryptionMapping( const Address & address );

        void ResetEncryptionMappings();

        uint64_t GetCounter( int index ) const;
    };
}

#endif // #ifndef YOJIMBO_SOCKET_INTERFACE_H
