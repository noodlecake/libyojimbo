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

#include "yojimbo_matcher.h"

#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

using namespace rapidjson;

#define SERVER_PORT "8080"
#define SERVER_NAME "localhost"

namespace yojimbo
{
    struct MatcherInternal
    {
        mbedtls_net_context server_fd;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;
        mbedtls_x509_crt cacert;
    };

    Matcher::Matcher()
    {
        m_initialized = false;
        m_status = MATCHER_IDLE;
        m_internal = new MatcherInternal();     // todo: convert to allocator
    }

    Matcher::~Matcher()
    {
        mbedtls_net_free( &m_internal->server_fd );
        mbedtls_x509_crt_free( &m_internal->cacert );
        mbedtls_ssl_free( &m_internal->ssl );
        mbedtls_ssl_config_free( &m_internal->conf );
        mbedtls_ctr_drbg_free( &m_internal->ctr_drbg );
        mbedtls_entropy_free( &m_internal->entropy );

        delete m_internal;
        m_internal = NULL;
    }

    bool Matcher::Initialize()
    {
        int ret;

        const char *pers = "yojimbo_client";

        mbedtls_net_init( &m_internal->server_fd );
        mbedtls_ssl_init( &m_internal->ssl );
        mbedtls_ssl_config_init( &m_internal->conf );
        mbedtls_x509_crt_init( &m_internal->cacert );
        mbedtls_ctr_drbg_init( &m_internal->ctr_drbg );
        mbedtls_entropy_init( &m_internal->entropy );

        if ( ( ret = mbedtls_ctr_drbg_seed( &m_internal->ctr_drbg, mbedtls_entropy_func, &m_internal->entropy, (const unsigned char *) pers, strlen( pers ) ) ) != 0 )
            return false;

        ret = mbedtls_x509_crt_parse( &m_internal->cacert, (const unsigned char *) mbedtls_test_cas_pem, mbedtls_test_cas_pem_len );
        if ( ret < 0 )
            return false;

        m_initialized = true;

        return true;
    }

    void Matcher::RequestMatch( uint32_t protocolId, uint64_t clientId )
    {
        assert( m_initialized );

        int ret;

        if ( ( ret = mbedtls_net_connect( &m_internal->server_fd, SERVER_NAME, SERVER_PORT, MBEDTLS_NET_PROTO_TCP ) ) != 0 )
        {
            m_status = MATCHER_FAILED;
            return;
        }

        if ( ( ret = mbedtls_ssl_config_defaults( &m_internal->conf,
                        MBEDTLS_SSL_IS_CLIENT,
                        MBEDTLS_SSL_TRANSPORT_STREAM,
                        MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 )
        {
            m_status = MATCHER_FAILED;
            return;
        }

        mbedtls_ssl_conf_authmode( &m_internal->conf, MBEDTLS_SSL_VERIFY_OPTIONAL );
        mbedtls_ssl_conf_ca_chain( &m_internal->conf, &m_internal->cacert, NULL );
        mbedtls_ssl_conf_rng( &m_internal->conf, mbedtls_ctr_drbg_random, &m_internal->ctr_drbg );

        if( ( ret = mbedtls_ssl_setup( &m_internal->ssl, &m_internal->conf ) ) != 0 )
        {
            m_status = MATCHER_FAILED;
            return;
        }

        if ( ( ret = mbedtls_ssl_set_hostname( &m_internal->ssl, "yojimbo" ) ) != 0 )
        {
            m_status = MATCHER_FAILED;
            return;
        }

        mbedtls_ssl_set_bio( &m_internal->ssl, &m_internal->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL );

        while ( ( ret = mbedtls_ssl_handshake( &m_internal->ssl ) ) != 0 )
        {
            if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
            {
                m_status = MATCHER_FAILED;
                return;
            }
        }

        uint32_t flags;
        if ( ( flags = mbedtls_ssl_get_verify_result( &m_internal->ssl ) ) != 0 )
        {
            // note: could not verify certificate (eg. it is self-signed)
        }

        char buf[4*1024];

        char request[1024];

        sprintf( request, "GET /match/%d/%" PRIx64 " HTTP/1.0\r\n\r\n", protocolId, clientId );

        while ( ( ret = mbedtls_ssl_write( &m_internal->ssl, (uint8_t*) request, strlen( request ) ) ) <= 0 )
        {
            if ( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
            {
                m_status = MATCHER_FAILED;
                return;
            }
        }

        int len = ret;

        do
        {
            len = sizeof( buf ) - 1;
            memset( buf, 0, sizeof( buf ) );
            ret = mbedtls_ssl_read( &m_internal->ssl, (uint8_t*) buf, len );

            if ( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE )
                continue;

            if ( ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY )
                break;

            if ( ret <= 0 )
                break;

            len = ret;

            const char * json = strstr( (const char*)buf, "\r\n\r\n" ) + 4;

            if ( !json )
                break;

            printf( "\n%s\n", json );

            if ( !ParseMatchResponse( json, m_matchResponse ) )
            {
                m_status = MATCHER_FAILED;
                return;
            }

            m_status = MATCHER_READY;
        }
        while( 1 );

        mbedtls_ssl_close_notify( &m_internal->ssl );

        m_status = MATCHER_FAILED;
    }

    MatcherStatus Matcher::GetStatus()
    {
        return m_status;
    }

    void Matcher::GetMatchResponse( MatchResponse & matchResponse )
    {
        matchResponse = ( m_status == MATCHER_READY ) ? m_matchResponse : MatchResponse();
    }

    static bool exists_and_is_string( Document & doc, const char * key )
    {
        return doc.HasMember( key ) && doc[key].IsString();
    }

    static bool exists_and_is_array( Document & doc, const char * key )
    {
        return doc.HasMember( key ) && doc[key].IsArray();
    }

    bool Matcher::ParseMatchResponse( const char * json, MatchResponse & matchResponse )
    {
        Document doc;
        doc.Parse( json );
        if ( doc.HasParseError() )
            return false;

        if ( !exists_and_is_string( doc, "connectToken" ) )
            return false;

        if ( !exists_and_is_string( doc, "connectNonce" ) )
            return false;

        if ( !exists_and_is_array( doc, "serverAddresses" ) )
            return false;

        if ( !exists_and_is_string( doc, "clientToServerKey" ) )
            return false;

        if ( !exists_and_is_string( doc, "serverToClientKey" ) )
            return false;

        const char * encryptedConnectTokenBase64 = doc["connectToken"].GetString();

        int encryptedLength = base64_decode_data( encryptedConnectTokenBase64, matchResponse.connectToken, ConnectTokenBytes );

        if ( encryptedLength != ConnectTokenBytes )
            return false;        

        matchResponse.connectNonce = atoll( doc["connectToken"].GetString() );

        matchResponse.numServerAddresses = 0;

        const Value & serverAddresses = doc["serverAddresses"];

        if ( !serverAddresses.IsArray() )
            return false;

        for ( SizeType i = 0; i < serverAddresses.Size(); ++i )
        {
            if ( i >= MaxServersPerConnectToken )
                return false;

            if ( !serverAddresses[i].IsString() )
                return false;

            char serverAddress[256];

            base64_decode_string( serverAddresses[i].GetString(), serverAddress, sizeof( serverAddress ) );

            matchResponse.serverAddresses[i] = Address( serverAddress );

            if ( !matchResponse.serverAddresses[i].IsValid() )
                return false;

            matchResponse.numServerAddresses++;
        }

        const char * clientToServerKeyBase64 = doc["clientToServerKey"].GetString();

        const char * serverToClientKeyBase64 = doc["serverToClientKey"].GetString();

        if ( base64_decode_data( clientToServerKeyBase64, matchResponse.clientToServerKey, KeyBytes ) != KeyBytes )
            return false;

        if ( base64_decode_data( serverToClientKeyBase64, matchResponse.serverToClientKey, KeyBytes ) != KeyBytes )
            return false;

        return true;
    }
}
