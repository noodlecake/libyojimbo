/*
    Yojimbo Client/Server Network Library.
    
    Copyright © 2016, The Network Protocol Company, Inc.

    Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

        1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

        2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer 
           in the documentation and/or other materials provided with the distribution.

        3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived 
           from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "yojimbo_client_server.h"
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <ucl.h>

namespace yojimbo
{
    bool ConnectToken::operator == ( const ConnectToken & other )
    {
        if ( protocolId != other.protocolId )
            return false;
        
        if ( clientId != other.clientId )
            return false;
            
        if ( expiryTimestamp != other.expiryTimestamp )
            return false;
            
        if ( numServerAddresses != other.numServerAddresses )
            return false;
            
        for ( int i = 0; i < numServerAddresses; ++i )
        {
            if ( serverAddresses[i] != other.serverAddresses[i] )
                return false;
        }

        if ( memcmp( clientToServerKey, other.clientToServerKey, KeyBytes ) != 0 )
            return false;

        if ( memcmp( serverToClientKey, other.serverToClientKey, KeyBytes ) != 0 )
            return false;

        if ( memcmp( random, other.random, KeyBytes ) != 0 )
            return false;

        return true;
    }

    bool ConnectToken::operator != ( const ConnectToken & other )
    {
        return ! ( (*this) == other );
    }

    void GenerateConnectToken( ConnectToken & token, uint64_t clientId, int numServerAddresses, const Address * serverAddresses, uint32_t protocolId )
    {
        uint64_t timestamp = (uint64_t) time( NULL );
        
        token.protocolId = protocolId;
        token.clientId = clientId;
        token.expiryTimestamp = timestamp + ConnectTokenExpirySeconds;
        
        assert( numServerAddresses > 0 );
        assert( numServerAddresses <= MaxServersPerConnectToken );
        token.numServerAddresses = numServerAddresses;
        for ( int i = 0; i < numServerAddresses; ++i )
            token.serverAddresses[i] = serverAddresses[i];

        GenerateKey( token.clientToServerKey );    

        GenerateKey( token.serverToClientKey );

        GenerateKey( token.random );
    }

    bool EncryptConnectToken( const ConnectToken & token, uint8_t *encryptedMessage, const uint8_t *additional, int additionalLength, const uint8_t * nonce, const uint8_t * key )
    {
        char message[ConnectTokenBytes-AuthBytes];
        memset( message, 0, ConnectTokenBytes - AuthBytes );
        if ( !WriteConnectTokenToJSON( token, message, ConnectTokenBytes - AuthBytes ) )
            return false;

        uint64_t encryptedLength;

        if ( !Encrypt_AEAD( (const uint8_t*)message, ConnectTokenBytes - AuthBytes, encryptedMessage, encryptedLength, additional, additionalLength, nonce, key ) )
            return false;

        assert( encryptedLength == ConnectTokenBytes );

        return true;
    }

    bool DecryptConnectToken( const uint8_t * encryptedMessage, ConnectToken & decryptedToken, const uint8_t * additional, int additionalLength, const uint8_t * nonce, const uint8_t * key )
    {
        const int encryptedMessageLength = ConnectTokenBytes;

        uint64_t decryptedMessageLength;
        uint8_t decryptedMessage[ConnectTokenBytes];

        if ( !Decrypt_AEAD( encryptedMessage, encryptedMessageLength, decryptedMessage, decryptedMessageLength, additional, additionalLength, nonce, key ) )
        {
            // todo: why
            printf( "failed to decrypt\n" );
            return false;
        }

        assert( decryptedMessageLength == ConnectTokenBytes - AuthBytes );

        return ReadConnectTokenFromJSON( (const char*) decryptedMessage, decryptedToken );
    }

    static bool insert_number_as_string( ucl_object_t * object, const char * key, uint64_t number )
    {
        char buffer[256];
        sprintf( buffer, "%" PRId64, number );
        return ucl_object_insert_key( object, ucl_object_fromstring( buffer ), key, 0, false );
    }

    static bool insert_data_as_base64_string( ucl_object_t * object, const char * key, const uint8_t * data, int data_length )
    {
        char * buffer = (char*) alloca( data_length * 2 );
        base64_encode_data( data, data_length, buffer, data_length * 2 );
        return ucl_object_insert_key( object, ucl_object_fromstring( buffer ), key, 0, false );
    }

    bool WriteConnectTokenToJSON( const ConnectToken & connectToken, char * output, int outputSize )
    {
        bool result = false;

        ucl_object_t * root = NULL;
        ucl_object_t * serverAddresses = NULL;
        char * json_output = NULL;
        int json_bytes = 0;

        root = ucl_object_typed_new( UCL_OBJECT );

        if ( !root )
            goto cleanup;

        if ( !insert_number_as_string( root, "protocolId", connectToken.protocolId ) )
            goto cleanup;

        if ( !insert_number_as_string( root, "clientId", connectToken.clientId ) )
            goto cleanup;

        if ( !insert_number_as_string( root, "expiryTimestamp", connectToken.expiryTimestamp ) )
            goto cleanup;

        if ( !insert_number_as_string( root, "numServerAddresses", connectToken.numServerAddresses ) )
            goto cleanup;

        serverAddresses = ucl_object_typed_new( UCL_ARRAY );

        if ( !serverAddresses )
            goto cleanup;

        ucl_object_insert_key( root, serverAddresses, "serverAddresses", 0, false );

        for ( int i = 0; i < connectToken.numServerAddresses; ++i )
        {
            char serverAddress[64];
            assert( connectToken.serverAddresses[i].IsValid() );
            connectToken.serverAddresses[i].ToString( serverAddress, sizeof( serverAddress ) );

            char serverAddressBase64[128];
            base64_encode_string( serverAddress, serverAddressBase64, sizeof( serverAddressBase64 ) );

            if ( !ucl_array_append( serverAddresses, ucl_object_fromstring( serverAddressBase64 ) ) )
                goto cleanup;
        }

        if ( !insert_data_as_base64_string( root, "clientToServerKey", connectToken.clientToServerKey, KeyBytes ) )
            goto cleanup;

        if ( !insert_data_as_base64_string( root, "serverToClientKey", connectToken.serverToClientKey, KeyBytes ) )
            goto cleanup;

        if ( !insert_data_as_base64_string( root, "random", connectToken.random, KeyBytes ) )
            goto cleanup;

        json_output = (char*) ucl_object_emit( root, UCL_EMIT_JSON_COMPACT );

        assert( json_output );

        json_bytes = (int) strlen( json_output ) + 1;

        if ( json_bytes > outputSize )
        {
            goto cleanup;
        }

        result = true;

    cleanup:

        if ( json_output )
        {
            if ( result )
            {
                memcpy( output, json_output, json_bytes );
            }

            free( json_output );
        }

        if ( root )
        {
            ucl_object_unref( root );
        }

        return result;
    }

    static bool read_int_from_string( ucl_object_t * parent, const char * key, int & value )
    {
        const ucl_object_t * object = ucl_object_lookup( parent, key );

        if ( !object || ucl_object_type( object ) != UCL_STRING )
            return false;

        const char * string = ucl_object_tostring( object );
        if ( !string )
            return false;

        value = atoi( string );

        return true;
    }

    static bool read_uint32_from_string( ucl_object_t * parent, const char * key, uint32_t & value )
    {
        const ucl_object_t * object = ucl_object_lookup( parent, key );

        if ( !object || ucl_object_type( object ) != UCL_STRING )
            return false;

        const char * string = ucl_object_tostring( object );
        if ( !string )
            return false;

        value = (uint32_t) strtoull( string, NULL, 10 );

        return true;
    }

    static bool read_uint64_from_string( ucl_object_t * parent, const char * key, uint64_t & value )
    {
        const ucl_object_t * object = ucl_object_lookup( parent, key );

        if ( !object || ucl_object_type( object ) != UCL_STRING )
            return false;

        const char * string = ucl_object_tostring( object );
        if ( !string )
            return false;

        value = (uint64_t) strtoull( string, NULL, 10 );

        return true;
    }

    static bool read_address_from_base64_string( const ucl_object_t * object, Address & address )
    {
        if ( !object || ucl_object_type( object ) != UCL_STRING )
            return false;

        const char * string = ucl_object_tostring( object );
        if ( !string )
            return false;

        const int string_length = strlen( string );

        const int MaxStringLength = 128;

        if ( string_length > 128 )
            return false;

        char buffer[MaxStringLength*2];

        base64_decode_string( string, buffer, sizeof( buffer ) );

        address = Address( buffer );

        return address.IsValid();
    }

    static bool read_data_from_base64_string( const ucl_object_t * parent, const char * key, uint8_t * data, int data_bytes )
    {
        const ucl_object_t * object = ucl_object_lookup( parent, key );

        if ( !object || ucl_object_type( object ) != UCL_STRING )
            return false;

        const char * string = ucl_object_tostring( object );
        if ( !string )
            return false;

        int string_length = strlen( string );

        uint8_t * buffer = (uint8_t*) alloca( string_length );

        int read_data_bytes = base64_decode_data( string, buffer, string_length );

        if ( read_data_bytes != data_bytes )
            return false;

        memcpy( data, buffer, data_bytes );

        return true;
    }

    bool ReadConnectTokenFromJSON( const char * json, ConnectToken & connectToken )
    {
        assert( json );

        bool result = false;

        ucl_parser * parser = NULL;
        ucl_object_t * root = NULL;
        const ucl_object_t * serverAddresses = NULL;

        parser = ucl_parser_new( UCL_PARSER_ZEROCOPY );

        if ( !parser )
            return false;

        if ( !ucl_parser_add_string( parser, json, strlen( json ) ) )
            goto cleanup;

        if ( ucl_parser_get_error( parser ) )
            goto cleanup;

        root = ucl_parser_get_object( parser );

        if ( !root )
            goto cleanup;

        if ( !read_uint32_from_string( root, "protocolId", connectToken.protocolId ) )
            goto cleanup;

        if ( !read_uint64_from_string( root, "clientId", connectToken.clientId ) )
            goto cleanup;

        if ( !read_uint64_from_string( root, "expiryTimestamp", connectToken.expiryTimestamp ) )
            goto cleanup;

        if ( !read_int_from_string( root, "numServerAddresses", connectToken.numServerAddresses ) )
            goto cleanup;

        serverAddresses = ucl_object_lookup( root, "serverAddresses" );

        if ( !serverAddresses || ucl_object_type( serverAddresses ) != UCL_ARRAY )
            goto cleanup;

        if ( connectToken.numServerAddresses <= 0 || connectToken.numServerAddresses > MaxServersPerConnectToken )
            goto cleanup;

        for ( int i = 0; i < connectToken.numServerAddresses; ++i )
        {
            const ucl_object_t * serverAddress = ucl_array_find_index( serverAddresses, i );

            if ( !read_address_from_base64_string( serverAddress, connectToken.serverAddresses[i] ) )
                goto cleanup;
        }

        if ( !read_data_from_base64_string( root, "clientToServerKey", connectToken.clientToServerKey, KeyBytes ) )
            goto cleanup;

        if ( !read_data_from_base64_string( root, "serverToClientKey", connectToken.serverToClientKey, KeyBytes ) )
            goto cleanup;

        if ( !read_data_from_base64_string( root, "random", connectToken.random, KeyBytes ) )
            goto cleanup;

        result = true;

    cleanup:

        if ( root )
        {
            ucl_object_unref( root );
        }

        if ( parser )
        {
            ucl_parser_free( parser );
        }

        return result;
    }

    bool GenerateChallengeToken( const ConnectToken & connectToken, const uint8_t * connectTokenMac, ChallengeToken & challengeToken )
    {
        if ( connectToken.clientId == 0 )
            return false;

        challengeToken.clientId = connectToken.clientId;

        memcpy( challengeToken.connectTokenMac, connectTokenMac, MacBytes );

        memcpy( challengeToken.clientToServerKey, connectToken.clientToServerKey, KeyBytes );

        memcpy( challengeToken.serverToClientKey, connectToken.serverToClientKey, KeyBytes );

        GenerateKey( challengeToken.random );

        return true;
    }

    bool EncryptChallengeToken( ChallengeToken & token, uint8_t *encryptedMessage, const uint8_t *additional, int additionalLength, const uint8_t * nonce, const uint8_t * key )
    {
        uint8_t message[ChallengeTokenBytes - AuthBytes];
        memset( message, 0, ChallengeTokenBytes - AuthBytes );
        WriteStream stream( message, ChallengeTokenBytes - AuthBytes );
        if ( !token.Serialize( stream ) )
            return false;

        stream.Flush();
        
        if ( stream.GetError() )
            return false;

        uint64_t encryptedLength;

        if ( !Encrypt_AEAD( message, ChallengeTokenBytes - AuthBytes, encryptedMessage, encryptedLength, additional, additionalLength, nonce, key ) )
            return false;

        assert( encryptedLength == ChallengeTokenBytes );

        return true;
    }

    bool DecryptChallengeToken( const uint8_t * encryptedMessage, ChallengeToken & decryptedToken, const uint8_t * additional, int additionalLength, const uint8_t * nonce, const uint8_t * key )
    {
        const int encryptedMessageLength = ChallengeTokenBytes;

        uint64_t decryptedMessageLength;
        uint8_t decryptedMessage[ChallengeTokenBytes];

        if ( !Decrypt_AEAD( encryptedMessage, encryptedMessageLength, decryptedMessage, decryptedMessageLength, additional, additionalLength, nonce, key ) )
            return false;

        assert( decryptedMessageLength == ChallengeTokenBytes - AuthBytes );

        ReadStream stream( decryptedMessage, ChallengeTokenBytes - AuthBytes );
        if ( !decryptedToken.Serialize( stream ) )
            return false;

        if ( stream.GetError() )
            return false;

        return true;
    }

    // =============================================================

    Server::Server( NetworkInterface & networkInterface )
    {
        memset( m_privateKey, 0, KeyBytes );

        m_networkInterface = &networkInterface;

        m_time = 0.0;

        m_flags = 0;

        m_maxClients = -1;

        m_numConnectedClients = 0;

        m_challengeTokenNonce = 0;

        m_globalSequence = 0;

        memset( m_clientSequence, 0, sizeof( m_clientSequence ) );

        for ( int i = 0; i < MaxClients; ++i )
            ResetClientState( i );

        memset( m_counters, 0, sizeof( m_counters ) );
    }

    Server::~Server()
    {
        assert( m_networkInterface );

        m_networkInterface = NULL;
    }

    void Server::SetPrivateKey( const uint8_t * privateKey )
    {
        memcpy( m_privateKey, privateKey, KeyBytes );
    }

    void Server::SetServerAddress( const Address & address )
    {
        m_serverAddress = address;
    }

    void Server::Start( int maxClients )
    {
        assert( maxClients > 0 );
        assert( maxClients <= MaxClients );

        Stop();

        m_maxClients = maxClients;

        OnStart( maxClients );
    }

    void Server::Stop()
    {
        if ( !IsRunning() )
            return;

        OnStop();

        DisconnectAllClients();

        m_maxClients = -1;
    }

    void Server::DisconnectClient( int clientIndex, bool sendDisconnectPacket )
    {
        assert( IsRunning() );
        assert( clientIndex >= 0 );
        assert( clientIndex < m_maxClients );
        assert( m_numConnectedClients > 0 );
        assert( m_clientConnected[clientIndex] );

        OnClientDisconnect( clientIndex );

        if ( sendDisconnectPacket )
        {
            for ( int i = 0; i < NumDisconnectPackets; ++i )
            {
                ConnectionDisconnectPacket * packet = (ConnectionDisconnectPacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_DISCONNECT );

                if ( packet )
                {
                    SendPacketToConnectedClient( clientIndex, packet, true );
                }
            }
        }

        m_networkInterface->RemoveEncryptionMapping( m_clientData[clientIndex].address );

        ResetClientState( clientIndex );

        m_counters[SERVER_COUNTER_CLIENT_DISCONNECTS]++;

        m_numConnectedClients--;
    }

    void Server::DisconnectAllClients( bool sendDisconnectPacket )
    {
        assert( IsRunning() );

        for ( int i = 0; i < m_maxClients; ++i )
        {
            if ( m_clientConnected[i] )
                DisconnectClient( i, sendDisconnectPacket );
        }
    }

    void Server::SendPackets()
    {
        if ( !IsRunning() )
            return;

        const double time = GetTime();

        for ( int i = 0; i < m_maxClients; ++i )
        {
            if ( !m_clientConnected[i] )
                continue;

            if ( m_clientData[i].lastPacketSendTime + ConnectionHeartBeatRate > time )
                return;

            ConnectionHeartBeatPacket * packet = CreateHeartBeatPacket( i );

            if ( packet )
            {
                SendPacketToConnectedClient( i, packet );
            }
        }
    }

    void Server::ReceivePackets()
    {
        while ( true )
        {
            Address address;
            uint64_t sequence;
            Packet * packet = m_networkInterface->ReceivePacket( address, &sequence );
            if ( !packet )
                break;

            if ( IsRunning() )
                ProcessPacket( packet, address, sequence );

            m_networkInterface->DestroyPacket( packet );
        }
    }

    void Server::CheckForTimeOut()
    {
        if ( !IsRunning() )
            return;

        const double time = GetTime();

        for ( int i = 0; i < m_maxClients; ++i )
        {
            if ( !m_clientConnected[i] )
                continue;

            if ( m_clientData[i].lastPacketReceiveTime + ConnectionTimeOut < time )
            {
                OnClientTimedOut( i );

                m_counters[SERVER_COUNTER_CLIENT_TIMEOUT_DISCONNECTS]++;

                DisconnectClient( i, false );
            }
        }
    }

    void Server::AdvanceTime( double time )
    {
        assert( time >= m_time );

        m_time = time;
    }

    void Server::SetFlags( uint64_t flags )
    {
        m_flags = flags;
    }

    bool Server::IsRunning() const
    {
        return m_maxClients > 0;
    }

    int Server::GetMaxClients() const
    {
        return m_maxClients;
    }

    bool Server::IsClientConnected( int clientIndex ) const
    {
        assert( clientIndex >= 0 );
        assert( clientIndex < m_maxClients );
        return m_clientConnected[clientIndex];
    }

    const Address & Server::GetServerAddress() const
    {
        return m_serverAddress;
    }

    int Server::FindClientIndex( const Address & address ) const
    {
        if ( !address.IsValid() )
            return -1;

        for ( int i = 0; i < m_maxClients; ++i )
        {   
            if ( m_clientConnected[i] && m_clientAddress[i] == address )
                return i;
        }

        return -1;
    }

    uint64_t Server::GetClientId( int clientIndex ) const
    {
        assert( clientIndex >= 0 );
        assert( clientIndex < m_maxClients );
        return m_clientId[clientIndex];
    }

    const Address & Server::GetClientAddress( int clientIndex ) const
    {
        assert( clientIndex >= 0 );
        assert( clientIndex < m_maxClients );
        return m_clientAddress[clientIndex];
    }

    int Server::GetNumConnectedClients() const
    {
        return m_numConnectedClients;
    }

    uint64_t Server::GetCounter( int index ) const 
    {
        assert( index >= 0 );
        assert( index < SERVER_COUNTER_NUM_COUNTERS );
        return m_counters[index];
    }

    double Server::GetTime() const
    {
        return m_time;
    }

    uint64_t Server::GetFlags() const
    {
        return m_flags;
    }

    void Server::ResetClientState( int clientIndex )
    {
        assert( clientIndex >= 0 );
        assert( clientIndex < MaxClients );
        m_clientConnected[clientIndex] = false;
        m_clientId[clientIndex] = 0;
        m_clientAddress[clientIndex] = Address();
        m_clientData[clientIndex] = ServerClientData();
        m_clientSequence[clientIndex] = 0;
    }

    int Server::FindFreeClientIndex() const
    {
        for ( int i = 0; i < m_maxClients; ++i )
        {
            if ( !m_clientConnected[i] )
                return i;
        }
        return -1;
    }

    int Server::FindExistingClientIndex( const Address & address ) const
    {
        for ( int i = 0; i < m_maxClients; ++i )
        {
            if ( m_clientConnected[i] && m_clientAddress[i] == address )
                return i;
        }
        return -1;
    }

    int Server::FindExistingClientIndex( const Address & address, uint64_t clientId ) const
    {
        for ( int i = 0; i < m_maxClients; ++i )
        {
            if ( m_clientId[i] == clientId && m_clientConnected[i] && m_clientAddress[i] == address )
                return i;
        }
        return -1;
    }

    bool Server::FindConnectTokenEntry( const uint8_t * mac )
    {
        for ( int i = 0; i < MaxConnectTokenEntries; ++i )
        {
            if ( memcmp( mac, m_connectTokenEntries[i].mac, MacBytes ) == 0 )
                return true;
        }

        return false;
    }

    bool Server::FindOrAddConnectTokenEntry( const Address & address, const uint8_t * mac )
    {
        // find the matching entry for the token mac, and the oldest token. constant time worst case O(1) at all times. This is intentional!

        const double time = GetTime();

        assert( address.IsValid() );

        assert( mac );

        int matchingTokenIndex = -1;
        int oldestTokenIndex = -1;
        double oldestTokenTime = 0.0;
        for ( int i = 0; i < MaxConnectTokenEntries; ++i )
        {
            if ( memcmp( mac, m_connectTokenEntries[i].mac, MacBytes ) == 0 )
            {
                matchingTokenIndex = i;
            }

            if ( oldestTokenIndex == -1 || m_connectTokenEntries[i].time < oldestTokenTime )
            {
                oldestTokenTime = m_connectTokenEntries[i].time;
                oldestTokenIndex = i;
            }
        }

        // if no entry is found with the mac, replace the oldest entry with this (mac,address,time) and return true

        assert( oldestTokenIndex != -1 );

        if ( matchingTokenIndex == -1 )
        {
            m_connectTokenEntries[oldestTokenIndex].time = time;
            m_connectTokenEntries[oldestTokenIndex].address = address;
            memcpy( m_connectTokenEntries[oldestTokenIndex].mac, mac, MacBytes );
            return true;
        }

        // if an entry is found with the same mac *and* it has the same address, return true

        assert( matchingTokenIndex >= 0 );
        assert( matchingTokenIndex < MaxConnectTokenEntries );

        if ( m_connectTokenEntries[matchingTokenIndex].address == address )
            return true;

        // otherwise an entry exists with the same mac but a different address, somebody is trying to reuse the connect token as a replay attack!

        return false;
    }

    void Server::ConnectClient( int clientIndex, const Address & clientAddress, uint64_t clientId )
    {
        assert( IsRunning() );
        assert( clientIndex >= 0 );
        assert( clientIndex < m_maxClients );
        assert( m_numConnectedClients >= 0 );
        assert( m_numConnectedClients < m_maxClients );
        assert( !m_clientConnected[clientIndex] );

        const double time = GetTime();

        m_counters[SERVER_COUNTER_CLIENT_CONNECTS]++;

        m_numConnectedClients++;

        m_clientConnected[clientIndex] = true;
        m_clientId[clientIndex] = clientId;
        m_clientAddress[clientIndex] = clientAddress;

        m_clientData[clientIndex].address = clientAddress;
        m_clientData[clientIndex].clientId = clientId;
        m_clientData[clientIndex].connectTime = time;
        m_clientData[clientIndex].lastPacketSendTime = time;
        m_clientData[clientIndex].lastPacketReceiveTime = time;

        OnClientConnect( clientIndex );

        ConnectionHeartBeatPacket * connectionHeartBeatPacket = CreateHeartBeatPacket( clientIndex );

        if ( connectionHeartBeatPacket )
        {
            SendPacketToConnectedClient( clientIndex, connectionHeartBeatPacket );
        }
    }

    int Server::FindClientId( uint64_t clientId ) const
    {
        for ( int i = 0; i < m_maxClients; ++i )
        {
            if ( !m_clientConnected[i] )
                continue;
            if ( m_clientId[i] == clientId )
                return i;
        }
        return -1;
    }

    int Server::FindAddressAndClientId( const Address & address, uint64_t clientId ) const
    {
        for ( int i = 0; i < m_maxClients; ++i )
        {
            if ( !m_clientConnected[i] )
                continue;
            if ( m_clientAddress[i] == address && m_clientId[i] == clientId )
                return i;
        }
        return -1;
    }

    void Server::SendPacket( const Address & address, Packet * packet, bool immediate )
    {
        assert( IsRunning() );

        m_networkInterface->SendPacket( address, packet, ++m_globalSequence, immediate );

        OnPacketSent( packet->GetType(), address, immediate );
    }

    void Server::SendPacketToConnectedClient( int clientIndex, Packet * packet, bool immediate )
    {
        assert( IsRunning() );
        assert( packet );
        assert( clientIndex >= 0 );
        assert( clientIndex < m_maxClients );
        assert( m_clientConnected[clientIndex] );

        const double time = GetTime();
        
        m_clientData[clientIndex].lastPacketSendTime = time;
        
        m_networkInterface->SendPacket( m_clientAddress[clientIndex], packet, ++m_clientSequence[clientIndex], immediate );
        
        OnPacketSent( packet->GetType(), m_clientAddress[clientIndex], immediate );
    }

    void Server::ProcessConnectionRequest( const ConnectionRequestPacket & packet, const Address & address )
    {
        assert( IsRunning() );

        if ( m_flags & SERVER_FLAG_IGNORE_CONNECTION_REQUESTS )
            return;

        m_counters[SERVER_COUNTER_CONNECTION_REQUEST_PACKETS_RECEIVED]++;

        ConnectToken connectToken;
        if ( !DecryptConnectToken( packet.connectTokenData, connectToken, NULL, 0, packet.connectTokenNonce, m_privateKey ) )
        {
//            printf( "failed to decrypt connect token\n" );
            m_counters[SERVER_COUNTER_CONNECT_TOKEN_FAILED_TO_DECRYPT]++;
            return;
        }

        bool serverAddressInConnectTokenWhiteList = false;

        for ( int i = 0; i < connectToken.numServerAddresses; ++i )
        {
            if ( m_serverAddress == connectToken.serverAddresses[i] )
            {
                serverAddressInConnectTokenWhiteList = true;
                break;
            }
        }

        if ( !serverAddressInConnectTokenWhiteList )
        {
//            printf( "server address not in connect token whitelist\n" );
            m_counters[SERVER_COUNTER_CONNECT_TOKEN_SERVER_ADDRESS_NOT_IN_WHITELIST]++;
            return;
        }

        if ( connectToken.clientId == 0 )
        {
//            printf( "client id is zero\n" );
            m_counters[SERVER_COUNTER_CONNECT_TOKEN_CLIENT_ID_IS_ZERO]++;
            return;
        }

        if ( FindAddressAndClientId( address, connectToken.clientId ) >= 0 )
        {
//            printf( "client id already connected: %d\n", (int) connectToken.clientId );
            m_counters[SERVER_COUNTER_CONNECT_TOKEN_CLIENT_ID_ALREADY_CONNECTED]++;
            return;
        }

        uint64_t timestamp = (uint64_t) ::time( NULL );

        if ( connectToken.expiryTimestamp <= timestamp )
        {
//            printf( "connect token expired\n" );
            m_counters[SERVER_COUNTER_CONNECT_TOKEN_EXPIRED]++;
            return;
        }

        if ( !FindConnectTokenEntry( packet.connectTokenData ) )
        {
            if ( !m_networkInterface->AddEncryptionMapping( address, connectToken.serverToClientKey, connectToken.clientToServerKey ) )
            {
//                printf( "failed to add encryption mapping\n" );
                m_counters[SERVER_COUNTER_ENCRYPTION_MAPPING_CANNOT_ADD]++;
                return;
            }
        }

        assert( m_numConnectedClients >= 0 );
        assert( m_numConnectedClients <= m_maxClients );

        if ( m_numConnectedClients == m_maxClients )
        {
//            printf( "server is full\n" );
            m_counters[SERVER_COUNTER_CONNECTION_DENIED_SERVER_IS_FULL]++;
            ConnectionDeniedPacket * connectionDeniedPacket = (ConnectionDeniedPacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_DENIED );
            if ( connectionDeniedPacket )
            {
                SendPacket( address, connectionDeniedPacket );
            }
            return;
        }

        if ( !FindOrAddConnectTokenEntry( address, packet.connectTokenData ) )
        {
//            printf( "find or add connect token entry failed\n" );
            m_counters[SERVER_COUNTER_CONNECT_TOKEN_ALREADY_USED]++;
            return;
        }

        ChallengeToken challengeToken;
        if ( !GenerateChallengeToken( connectToken, packet.connectTokenData, challengeToken ) )
        {
            m_counters[SERVER_COUNTER_CHALLENGE_TOKEN_FAILED_TO_GENERATE]++;
            return;
        }

        ConnectionChallengePacket * connectionChallengePacket = (ConnectionChallengePacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_CHALLENGE );
        if ( !connectionChallengePacket )
            return;

        memcpy( connectionChallengePacket->challengeTokenNonce, (uint8_t*) &m_challengeTokenNonce, NonceBytes );

        if ( !EncryptChallengeToken( challengeToken, connectionChallengePacket->challengeTokenData, NULL, 0, connectionChallengePacket->challengeTokenNonce, m_privateKey ) )
        {
            m_counters[SERVER_COUNTER_CHALLENGE_TOKEN_FAILED_TO_ENCRYPT]++;
            return;
        }

        m_counters[SERVER_COUNTER_CHALLENGE_PACKETS_SENT]++;

        SendPacket( address, connectionChallengePacket );
    }

    void Server::ProcessConnectionResponse( const ConnectionResponsePacket & packet, const Address & address )
    {
        assert( IsRunning() );

        if ( m_flags & SERVER_FLAG_IGNORE_CHALLENGE_RESPONSES )
            return;

        const double time = GetTime();

        m_counters[SERVER_COUNTER_CHALLENGE_RESPONSE_PACKETS_RECEIVED]++;

        ChallengeToken challengeToken;
        if ( !DecryptChallengeToken( packet.challengeTokenData, challengeToken, NULL, 0, packet.challengeTokenNonce, m_privateKey ) )
        {
            m_counters[SERVER_COUNTER_CHALLENGE_TOKEN_FAILED_TO_DECRYPT]++;
            return;
        }

        const int existingClientIndex = FindExistingClientIndex( address, challengeToken.clientId );
        if ( existingClientIndex != -1 )
        {
            assert( existingClientIndex >= 0 );
            assert( existingClientIndex < m_maxClients );

            if ( m_clientData[existingClientIndex].lastPacketSendTime + ConnectionConfirmSendRate < time )
            {
                ConnectionHeartBeatPacket * connectionHeartBeatPacket = CreateHeartBeatPacket( existingClientIndex );

                if ( connectionHeartBeatPacket )
                {
                    SendPacketToConnectedClient( existingClientIndex, connectionHeartBeatPacket );
                }
            }

            return;
        }

        if ( m_numConnectedClients == m_maxClients )
        {
            m_counters[SERVER_COUNTER_CONNECTION_DENIED_SERVER_IS_FULL]++;
            ConnectionDeniedPacket * connectionDeniedPacket = (ConnectionDeniedPacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_DENIED );
            if ( connectionDeniedPacket )
            {
                SendPacket( address, connectionDeniedPacket );
            }
            return;
        }

        const int clientIndex = FindFreeClientIndex();

        assert( clientIndex != -1 );
        if ( clientIndex == -1 )
            return;

        ConnectClient( clientIndex, address, challengeToken.clientId );
    }

    void Server::ProcessConnectionHeartBeat( const ConnectionHeartBeatPacket & /*packet*/, const Address & address )
    {
        assert( IsRunning() );

        const int clientIndex = FindExistingClientIndex( address );
        if ( clientIndex == -1 )
            return;

        assert( clientIndex >= 0 );
        assert( clientIndex < m_maxClients );

        const double time = GetTime();
        
        m_clientData[clientIndex].lastPacketReceiveTime = time;
    }

    void Server::ProcessConnectionDisconnect( const ConnectionDisconnectPacket & /*packet*/, const Address & address )
    {
        assert( IsRunning() );

        const int clientIndex = FindExistingClientIndex( address );
        if ( clientIndex == -1 )
            return;

        assert( clientIndex >= 0 );
        assert( clientIndex < m_maxClients );

        m_counters[SERVER_COUNTER_CLIENT_CLEAN_DISCONNECTS]++;

        DisconnectClient( clientIndex, false );
    }

#if YOJIMBO_INSECURE_CONNECT
    void Server::ProcessInsecureConnect( const InsecureConnectPacket & packet, const Address & address )
    {
        assert( IsRunning() );

        if ( ( GetFlags() & SERVER_FLAG_ALLOW_INSECURE_CONNECT ) == 0 )
            return;

        if ( m_numConnectedClients == m_maxClients )
        {
            m_counters[SERVER_COUNTER_CONNECTION_DENIED_SERVER_IS_FULL]++;
            ConnectionDeniedPacket * connectionDeniedPacket = (ConnectionDeniedPacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_DENIED );
            if ( connectionDeniedPacket )
            {
                SendPacket( address, connectionDeniedPacket );
            }
            return;
        }

        const uint64_t clientSalt = packet.clientSalt;

        int clientIndex = FindExistingClientIndex( address );

        if ( clientIndex != -1 )
        {
            if ( m_clientData[clientIndex].clientSalt == clientSalt )
            {
                ConnectionHeartBeatPacket * connectionHeartBeatPacket = CreateHeartBeatPacket( clientIndex );

                if ( connectionHeartBeatPacket )
                {
                    SendPacketToConnectedClient( clientIndex, connectionHeartBeatPacket );
                }
            }

            return;            
        }

        clientIndex = FindFreeClientIndex();

        assert( clientIndex != -1 );
        if ( clientIndex == -1 )
            return;

        ConnectClient( clientIndex, address, 0 );

        m_clientData[clientIndex].clientSalt = clientSalt;
    }
#endif // #if YOJIMBO_INSECURE_CONNECT

    void Server::ProcessPacket( Packet * packet, const Address & address, uint64_t sequence )
    {
#if DEBUG
        if ( m_networkInterface->IsEncryptedPacketType( packet->GetType() ) && ( m_networkInterface->GetFlags() & NETWORK_INTERFACE_FLAG_INSECURE_MODE ) == 0 )
        {
            assert( sequence > 0 );
        }
        else
        {
            assert( sequence == 0 );
        }
#endif // #if DEBUG

        OnPacketReceived( packet->GetType(), address, sequence );
        
        switch ( packet->GetType() )
        {
            case CLIENT_SERVER_PACKET_CONNECTION_REQUEST:
                ProcessConnectionRequest( *(ConnectionRequestPacket*)packet, address );
                return;

            case CLIENT_SERVER_PACKET_CONNECTION_RESPONSE:
                ProcessConnectionResponse( *(ConnectionResponsePacket*)packet, address );
                return;

            case CLIENT_SERVER_PACKET_CONNECTION_HEARTBEAT:
                ProcessConnectionHeartBeat( *(ConnectionHeartBeatPacket*)packet, address );
                return;

            case CLIENT_SERVER_PACKET_CONNECTION_DISCONNECT:
                ProcessConnectionDisconnect( *(ConnectionDisconnectPacket*)packet, address );
                return;

#if YOJIMBO_INSECURE_CONNECT
            case CLIENT_SERVER_PACKET_INSECURE_CONNECT:
                ProcessInsecureConnect( *(InsecureConnectPacket*)packet, address );
                return;
#endif // #if YOJIMBO_INSECURE_CONNECT

            default:
                break;
        }

        const int clientIndex = FindClientIndex( address );

        if ( clientIndex == -1 )
            return;

        if ( !ProcessGamePacket( clientIndex, packet, sequence ) )
            return;

        m_clientData[clientIndex].lastPacketReceiveTime = GetTime();
    }

    ConnectionHeartBeatPacket * Server::CreateHeartBeatPacket( int clientIndex )
    {
        ConnectionHeartBeatPacket * packet = (ConnectionHeartBeatPacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_HEARTBEAT );

        if ( packet )
        {
            packet->clientIndex = clientIndex;
        }

        return packet;
    }

    // =============================================================

    Client::Client( NetworkInterface & networkInterface )
    {
        m_time = 0.0;
        m_networkInterface = &networkInterface;
        m_clientState = CLIENT_STATE_DISCONNECTED;
        ResetConnectionData();
    }

    Client::~Client()
    {
        m_networkInterface = NULL;
    }

#if YOJIMBO_INSECURE_CONNECT
    void Client::InsecureConnect( const Address & address )
    {
        Disconnect();

        m_serverAddress = address;

        OnConnect( address );

        SetClientState( CLIENT_STATE_SENDING_INSECURE_CONNECT );

        const double time = GetTime();        

        m_lastPacketSendTime = time - 1.0f;
        m_lastPacketReceiveTime = time;

        RandomBytes( (uint8_t*) &m_clientSalt, sizeof( m_clientSalt ) );

        m_networkInterface->ResetEncryptionMappings();
    }
#endif // #if YOJIMBO_INSECURE_CONNECT

    void Client::Connect( const Address & address, 
                          const uint8_t * connectTokenData, 
                          const uint8_t * connectTokenNonce,
                          const uint8_t * clientToServerKey,
                          const uint8_t * serverToClientKey )
    {
        Disconnect();

        m_serverAddress = address;

        OnConnect( address );

        SetClientState( CLIENT_STATE_SENDING_CONNECTION_REQUEST );

        const double time = GetTime();        

        m_lastPacketSendTime = time - 1.0f;
        m_lastPacketReceiveTime = time;
        memcpy( m_connectTokenData, connectTokenData, ConnectTokenBytes );
        memcpy( m_connectTokenNonce, connectTokenNonce, NonceBytes );

        m_networkInterface->ResetEncryptionMappings();

        m_networkInterface->AddEncryptionMapping( m_serverAddress, clientToServerKey, serverToClientKey );
    }

    void Client::Disconnect( int clientState, bool sendDisconnectPacket )
    {
        assert( clientState <= CLIENT_STATE_DISCONNECTED );

        if ( m_clientState != clientState )
        {
            OnDisconnect();
        }

        if ( sendDisconnectPacket && m_clientState > CLIENT_STATE_DISCONNECTED )
        {
            for ( int i = 0; i < NumDisconnectPackets; ++i )
            {
                ConnectionDisconnectPacket * packet = (ConnectionDisconnectPacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_DISCONNECT );            

                if ( packet )
                {
                    SendPacketToServer( packet, true );
                }
            }
        }

        ResetConnectionData( clientState );
    }

    bool Client::IsConnecting() const
    {
        return m_clientState > CLIENT_STATE_DISCONNECTED && m_clientState < CLIENT_STATE_CONNECTED;
    }

    bool Client::IsConnected() const
    {
        return m_clientState == CLIENT_STATE_CONNECTED;
    }

    bool Client::IsDisconnected() const
    {
        return m_clientState <= CLIENT_STATE_DISCONNECTED;
    }

    bool Client::ConnectionFailed() const
    {
        return m_clientState < CLIENT_STATE_DISCONNECTED;
    }

    ClientState Client::GetClientState() const
    { 
        return m_clientState;
    }

    void Client::SendPackets()
    {
        const double time = GetTime();

        switch ( m_clientState )
        {
#if YOJIMBO_INSECURE_CONNECT

            case CLIENT_STATE_SENDING_INSECURE_CONNECT:
            {
                if ( m_lastPacketSendTime + InsecureConnectSendRate > time )
                    return;

                InsecureConnectPacket * packet = (InsecureConnectPacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_INSECURE_CONNECT );
                if ( packet )
                {
                    packet->clientSalt = m_clientSalt;
                    SendPacketToServer( packet );
                }
            }
            break;

#endif // #if YOJIMBO_INSECURE_CONNECT

            case CLIENT_STATE_SENDING_CONNECTION_REQUEST:
            {
                if ( m_lastPacketSendTime + ConnectionRequestSendRate > time )
                    return;

                ConnectionRequestPacket * packet = (ConnectionRequestPacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_REQUEST );
                if ( packet )
                {
                    memcpy( packet->connectTokenData, m_connectTokenData, ConnectTokenBytes );
                    memcpy( packet->connectTokenNonce, m_connectTokenNonce, NonceBytes );

                    SendPacketToServer( packet );
                }
            }
            break;

            case CLIENT_STATE_SENDING_CHALLENGE_RESPONSE:
            {
                if ( m_lastPacketSendTime + ConnectionResponseSendRate > time )
                    return;

                ConnectionResponsePacket * packet = (ConnectionResponsePacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_RESPONSE );
                if ( packet )
                {
                    memcpy( packet->challengeTokenData, m_challengeTokenData, ChallengeTokenBytes );
                    memcpy( packet->challengeTokenNonce, m_challengeTokenNonce, NonceBytes );
                    
                    SendPacketToServer( packet );
                }
            }
            break;

            case CLIENT_STATE_CONNECTED:
            {
                if ( m_lastPacketSendTime + ConnectionHeartBeatRate > time )
                    return;

                ConnectionHeartBeatPacket * packet = (ConnectionHeartBeatPacket*) m_networkInterface->CreatePacket( CLIENT_SERVER_PACKET_CONNECTION_HEARTBEAT );

                if ( packet )
                {
                    SendPacketToServer( packet );
                }
            }
            break;

            default:
                break;
        }
    }

    void Client::ReceivePackets()
    {
        while ( true )
        {
            Address address;
            uint64_t sequence;
            Packet * packet = m_networkInterface->ReceivePacket( address, &sequence );
            if ( !packet )
                break;

            ProcessPacket( packet, address, sequence );

            m_networkInterface->DestroyPacket( packet );
        }
    }

    void Client::CheckForTimeOut()
    {
        const double time = GetTime();

        switch ( m_clientState )
        {
#if YOJIMBO_INSECURE_CONNECT

            case CLIENT_STATE_SENDING_INSECURE_CONNECT:
            {
                if ( m_lastPacketReceiveTime + InsecureConnectTimeOut < time )
                {
                    Disconnect( CLIENT_STATE_INSECURE_CONNECT_TIMED_OUT, false );
                    return;
                }
            }
            break;

#endif // #if YOJIMBO_INSECURE_CONNECT

            case CLIENT_STATE_SENDING_CONNECTION_REQUEST:
            {
                if ( m_lastPacketReceiveTime + ConnectionRequestTimeOut < time )
                {
                    Disconnect( CLIENT_STATE_CONNECTION_REQUEST_TIMED_OUT, false );
                    return;
                }
            }
            break;

            case CLIENT_STATE_SENDING_CHALLENGE_RESPONSE:
            {
                if ( m_lastPacketReceiveTime + ChallengeResponseTimeOut < time )
                {
                    Disconnect( CLIENT_STATE_CHALLENGE_RESPONSE_TIMED_OUT, false );
                    return;
                }
            }
            break;

            case CLIENT_STATE_CONNECTED:
            {
                if ( m_lastPacketReceiveTime + ConnectionTimeOut < time )
                {
                    Disconnect( CLIENT_STATE_CONNECTION_TIMED_OUT, false );
                    return;
                }
            }
            break;

            default:
                break;
        }
    }

    void Client::AdvanceTime( double time )
    {
        assert( time >= m_time );

        m_time = time;
    }

    double Client::GetTime() const
    {
        return m_time;
    }

    int Client::GetClientIndex() const
    {
        return m_clientIndex;
    }

    void Client::SetClientState( int clientState )
    {
        const int previous = m_clientState;
        m_clientState = (ClientState) clientState;
        if ( clientState != previous )
            OnClientStateChange( previous, clientState );
    }

    void Client::ResetConnectionData( int clientState )
    {
        assert( m_networkInterface );
        m_clientIndex = -1;
        m_serverAddress = Address();
        SetClientState( clientState );
        m_lastPacketSendTime = -1000.0;
        m_lastPacketReceiveTime = -1000.0;
        memset( m_connectTokenData, 0, ConnectTokenBytes );
        memset( m_connectTokenNonce, 0, NonceBytes );
        memset( m_challengeTokenData, 0, ChallengeTokenBytes );
        memset( m_challengeTokenNonce, 0, NonceBytes );
        m_networkInterface->ResetEncryptionMappings();
        m_sequence = 0;
#if YOJIMBO_INSECURE_CONNECT
        m_clientSalt = 0;
#endif // #if YOJIMBO_INSECURE_CONNECT
    }

    void Client::SendPacketToServer( Packet * packet, bool immediate )
    {
        assert( packet );
        assert( m_clientState > CLIENT_STATE_DISCONNECTED );
        assert( m_serverAddress.IsValid() );

        m_networkInterface->SendPacket( m_serverAddress, packet, ++m_sequence, immediate );

        OnPacketSent( packet->GetType(), m_serverAddress, immediate );

        m_lastPacketSendTime = GetTime();
    }

    void Client::ProcessConnectionDenied( const ConnectionDeniedPacket & /*packet*/, const Address & address )
    {
        if ( m_clientState != CLIENT_STATE_SENDING_CONNECTION_REQUEST )
            return;

        if ( address != m_serverAddress )
            return;

        SetClientState( CLIENT_STATE_CONNECTION_DENIED );
    }

    void Client::ProcessConnectionChallenge( const ConnectionChallengePacket & packet, const Address & address )
    {
        if ( m_clientState != CLIENT_STATE_SENDING_CONNECTION_REQUEST )
            return;

        if ( address != m_serverAddress )
            return;

        memcpy( m_challengeTokenData, packet.challengeTokenData, ChallengeTokenBytes );
        memcpy( m_challengeTokenNonce, packet.challengeTokenNonce, NonceBytes );

        SetClientState( CLIENT_STATE_SENDING_CHALLENGE_RESPONSE );

        const double time = GetTime();

        m_lastPacketReceiveTime = time;
    }

    void Client::ProcessConnectionHeartBeat( const ConnectionHeartBeatPacket & packet, const Address & address )
    {
#if YOJIMBO_INSECURE_CONNECT

        if ( m_clientState != CLIENT_STATE_SENDING_CHALLENGE_RESPONSE && 
             m_clientState != CLIENT_STATE_SENDING_INSECURE_CONNECT && 
             m_clientState != CLIENT_STATE_CONNECTED )
        {
            return;
        }

#else // #if YOJIMBO_INSECURE_CONNECT

        if ( m_clientState < CLIENT_STATE_SENDING_CHALLENGE_RESPONSE )
            return;

#endif // #if YOJIMBO_INSECURE_CONNECT

        if ( address != m_serverAddress )
        {
            return;
        }

        if ( m_clientState == CLIENT_STATE_SENDING_CHALLENGE_RESPONSE )
        {
            m_clientIndex = packet.clientIndex;

            memset( m_connectTokenData, 0, ConnectTokenBytes );
            memset( m_connectTokenNonce, 0, NonceBytes );
            memset( m_challengeTokenData, 0, ChallengeTokenBytes );
            memset( m_challengeTokenNonce, 0, NonceBytes );

            SetClientState( CLIENT_STATE_CONNECTED );
        }

#if YOJIMBO_INSECURE_CONNECT

        if ( m_clientState == CLIENT_STATE_SENDING_INSECURE_CONNECT )
        {
            m_clientIndex = packet.clientIndex;

            SetClientState( CLIENT_STATE_CONNECTED );
        }

#endif // #if YOJIMBO_INSECURE_CONNECT

        const double time = GetTime();

        m_lastPacketReceiveTime = time;
    }

    void Client::ProcessConnectionDisconnect( const ConnectionDisconnectPacket & /*packet*/, const Address & address )
    {
        if ( m_clientState != CLIENT_STATE_CONNECTED )
            return;

        if ( address != m_serverAddress )
            return;

        Disconnect( CLIENT_STATE_DISCONNECTED, false );
    }

    void Client::ProcessPacket( Packet * packet, const Address & address, uint64_t sequence )
    {
#if DEBUG
        if ( m_networkInterface->IsEncryptedPacketType( packet->GetType() ) && ( m_networkInterface->GetFlags() & NETWORK_INTERFACE_FLAG_INSECURE_MODE ) == 0 )
        {
            assert( sequence > 0 );
        }
        else
        {
            assert( sequence == 0 );
        }
#endif // #if DEBUG

        OnPacketReceived( packet->GetType(), address, sequence );
        
        switch ( packet->GetType() )
        {
            case CLIENT_SERVER_PACKET_CONNECTION_DENIED:
                ProcessConnectionDenied( *(ConnectionDeniedPacket*)packet, address );
                return;

            case CLIENT_SERVER_PACKET_CONNECTION_CHALLENGE:
                ProcessConnectionChallenge( *(ConnectionChallengePacket*)packet, address );
                return;

            case CLIENT_SERVER_PACKET_CONNECTION_HEARTBEAT:
                ProcessConnectionHeartBeat( *(ConnectionHeartBeatPacket*)packet, address );
                return;

            case CLIENT_SERVER_PACKET_CONNECTION_DISCONNECT:
                ProcessConnectionDisconnect( *(ConnectionDisconnectPacket*)packet, address );
                return;

            default:
                break;
        }

        if ( !IsConnected() )
            return;

        if ( address != m_serverAddress )
            return;

        if ( !ProcessGamePacket( packet, sequence ) )
            return;

        m_lastPacketReceiveTime = GetTime();
    }
}
