#ifndef EASYWSCLIENT_HPP_20120819_MIOFVASDTNUASZDQPLFD
#define EASYWSCLIENT_HPP_20120819_MIOFVASDTNUASZDQPLFD

//#define RBXWSOCKET_USE_TLS

// This code comes from:
// https://github.com/dhbaird/easywsclient
//
// ----------------------------------------
// 
// Remake for roblox by: igromanvTV
// 
// ver: 0.1 (need fix)
//

#include "../../Crypt/xor.hpp"
#include <string>
#include <vector>
#include <wincrypt.h>

#ifdef RBXWSOCKET_USE_TLS

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform.h>
#include <mbedtls/x509.h>
#include <mbedtls/x509_crt.h>

#endif // RBXWSOCKET_USE_TLS

namespace rbxwsocket {

    struct Callback_Imp { virtual void operator()(const std::string& message) = 0; };
    struct BytesCallback_Imp { virtual void operator()(const std::vector<uint8_t>& message) = 0; };

    class WebSocket {
    public:
        typedef WebSocket* pointer;
        typedef enum readyStateValues { CLOSING, CLOSED, CONNECTING, OPEN } readyStateValues;

        // Factories:
        static pointer create_dummy();
        static pointer from_url(const std::string& url, const std::string& origin = std::string());
        static pointer from_url_no_mask(const std::string& url, const std::string& origin = std::string());

        // Interfaces:
        virtual ~WebSocket() {}
        virtual void poll(int timeout = 0) = 0; // timeout in milliseconds
        virtual void send(const std::string& message) = 0;
        virtual void sendBinary(const std::string& message) = 0;
        virtual void sendBinary(const std::vector<uint8_t>& message) = 0;
        virtual void sendPing() = 0;
        virtual void close() = 0;
        virtual readyStateValues getReadyState() const = 0;
        

        template<class Callable>
        void dispatch(Callable callable)
            // For callbacks that accept a string argument.
        { // N.B. this is compatible with both C++11 lambdas, functors and C function pointers
            struct _Callback : public Callback_Imp {
                Callable& callable;
                _Callback(Callable& callable) : callable(callable) {}
                void operator()(const std::string& message) { callable(message); }
            };
            _Callback callback(callable);
            _dispatch(callback);
        }

        template<class Callable>
        void dispatchBinary(Callable callable)
            // For callbacks that accept a std::vector<uint8_t> argument.
        { // N.B. this is compatible with both C++11 lambdas, functors and C function pointers
            struct _Callback : public BytesCallback_Imp {
                Callable& callable;
                _Callback(Callable& callable) : callable(callable) {}
                void operator()(const std::vector<uint8_t>& message) { callable(message); }
            };
            _Callback callback(callable);
            _dispatchBinary(callback);
        }

    protected:
        virtual void _dispatch(Callback_Imp& callable) = 0;
        virtual void _dispatchBinary(BytesCallback_Imp& callable) = 0;



    public:

    };

} // namespace rbxwsocket

#endif /* EASYWSCLIENT_HPP_20120819_MIOFVASDTNUASZDQPLFD */

namespace TLS_rbxwsocket
{

     class TLSrbxwsocket {

     public:
     static void initTLS();
     static void close();
     static bool initTLS_Main(const std::string& host);

     };

#ifdef RBXWSOCKET_USE_TLS

     inline mbedtls_net_context __server_fd;
     inline mbedtls_ssl_context __ssl;
     inline mbedtls_ssl_config _conf;
     inline mbedtls_entropy_context _entropy;
     inline mbedtls_ctr_drbg_context _ctr_drbg;
     inline mbedtls_x509_crt _cacert;
     inline mbedtls_x509_crt _cert;
     inline mbedtls_pk_context _pkey;

#endif // RBXWSOCKET_USE_TLS
}
