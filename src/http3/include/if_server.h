#ifndef HTTP3_INCLUDE_IF_SERVER
#define HTTP3_INCLUDE_IF_SERVER

#include <string>
#include <memory>
#include <cstdint>
#include "http3/include/type.h"
#include "http3/include/if_async_handler.h"

namespace quicx {
namespace http3 {

/**
 * @brief HTTP3 server configuration
 * 
 * This configuration is used to initialize the server.
 */
struct Http3ServerConfig {
    std::string cert_file_;          // certificate file path 
    std::string key_file_;           // key file path
    const char* cert_pem_ = nullptr; // certificate pem
    const char* key_pem_ = nullptr;  // key pem

    Http3Config config_;             // http3 config
};

/**
 * @brief Interface for HTTP3 server
 * 
 * This interface provides methods to start and stop the server.
 */
class IServer {
public:
    IServer() {}
    virtual ~IServer() {};

    /**
     * @brief Initialize the server with a certificate and a key
     * 
     * @param config The server configuration
     * @return True if the server is initialized successfully, false otherwise
     */
    virtual bool Init(const Http3ServerConfig& config) = 0;

    /**
     * @brief Start the server on the given address and port
     * 
     * @param addr The address to start the server
     * @param port The port to start the server
     * @return True if the server is started successfully, false otherwise
     */
    // server will block until the server is stopped
    virtual bool Start(const std::string& addr, uint16_t port) = 0;

    /**
     * @brief Stop the server
     */
    virtual void Stop() = 0;

    /**
     * @brief Join the server
     * 
     * This method will block until the server is stopped.
     */
    virtual void Join() = 0;

    /**
     * @brief Register a handler for complete mode (entire body buffered)
     * 
     * In complete mode, the handler is called AFTER the entire request body
     * has been received and buffered. Use request->GetBody() to access the
     * complete body content.
     * 
     * @param method HTTP method (GET, POST, PUT, DELETE, etc.)
     * @param path URL path pattern (supports ":param" and "*" wildcards)
     * @param handler Handler function to process requests
     * 
     * @example
     * @code
     * server->AddHandler(HttpMethod::kPost, "/api/data", 
     *     [](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
     *         std::string body = req->GetBody();  // Complete body available
     *         process(body);
     *         resp->SetBody("OK");
     *     });
     * @endcode
     */
    virtual void AddHandler(HttpMethod method, const std::string& path, 
                           const http_handler& handler) = 0;
    
    /**
     * @brief Register an async handler for streaming mode
     * 
     * In streaming mode, the handler receives callbacks as data arrives:
     * - OnHeaders(): called when request headers are received
     * - OnBodyChunk(): called for each chunk of request body
     * - OnError(): called only if a protocol/network error occurs
     * 
     * This is useful for:
     * - Large file uploads
     * - Real-time data processing
     * - Avoiding memory buffering of large bodies
     * 
     * @param method HTTP method (GET, POST, PUT, DELETE, etc.)
     * @param path URL path pattern (supports ":param" and "*" wildcards)
     * @param handler Async handler instance to process requests
     * 
     * @example
     * @code
     * class FileUploadHandler : public IAsyncServerHandler {
     * public:
     *     void OnHeaders(auto req, auto resp) override {
     *         file_ = fopen("upload.dat", "wb");
     *         resp->SetStatusCode(200);
     *     }
     *     void OnBodyChunk(const uint8_t* data, size_t len, bool is_last) override {
     *         if (file_) fwrite(data, 1, len, file_);
     *         if (is_last && file_) fclose(file_);
     *     }
     *     void OnError(uint32_t error) override {
     *         if (file_) fclose(file_);
     *     }
     * private:
     *     FILE* file_ = nullptr;
     * };
     * 
     * server->AddHandler(HttpMethod::kPost, "/upload", 
     *                   std::make_shared<FileUploadHandler>());
     * @endcode
     */
    virtual void AddHandler(HttpMethod method, const std::string& path, 
                           std::shared_ptr<IAsyncServerHandler> handler) = 0;

    /**
     * @brief Register a middleware for a specific method and position
     * 
     * @param method The HTTP method
     * @param mp The middleware position
     * @param handler The middleware handler
     */ 
    virtual void AddMiddleware(HttpMethod method, MiddlewarePosition mp, const http_handler& handler) = 0;

    /**
     * @brief Set the error handler
     * 
     * @param error_handler The error handler
     */
    virtual void SetErrorHandler(const error_handler& error_handler) = 0;

    /**
     * @brief Create a server instance
     * 
     * @param settings The HTTP3 settings
     * @return The server instance (shared_ptr required for enable_shared_from_this)
     */
    static std::shared_ptr<IServer> Create(const Http3Settings& settings = kDefaultHttp3Settings);
};

}
}

#endif

