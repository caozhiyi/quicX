#ifndef HTTP3_INCLUDE_IF_CLIENT
#define HTTP3_INCLUDE_IF_CLIENT

#include <memory>
#include <string>

#include "http3/include/if_async_handler.h"
#include "http3/include/if_request.h"
#include "http3/include/type.h"
#include "quic/include/if_quic_client.h"

namespace quicx {

/**
 * @brief HTTP3 client configuration
 *
 * Wraps the QUIC client configuration and adds HTTP/3-specific settings.
 */
struct Http3ClientConfig {
    /** QUIC client configuration (including session cache, retry, etc.) */
    QuicClientConfig quic_config_;

    /** Connection timeout in milliseconds */
    uint32_t connection_timeout_ms_ = 0;

    /** Metrics endpoint configuration (optional for client) */
    MetricsConfig metrics_;

    /** Local connection limits */
    uint64_t max_concurrent_streams_ = 200;  // max concurrent streams allowed
    bool enable_push_ = false;               // whether to enable push streams
};

/**
 * @brief HTTP3 client interface
 *
 * This interface provides methods to send requests to the server.
 * It supports both complete mode and streaming mode.
 */
class IClient {
public:
    IClient() {}
    virtual ~IClient() {};

    /**
     * @brief Initialize the client
     *
     * @param config The client configuration
     * @return True if the client is initialized successfully, false otherwise
     */
    virtual bool Init(const Http3ClientConfig& config) = 0;

    /**
     * @brief Send an HTTP request in complete mode (entire body buffered)
     *
     * In complete mode, the handler is called AFTER the entire response body
     * has been received and buffered. Use response->GetBody() to access the
     * complete body content.
     *
     * @param url Target URL (e.g., "https://example.com/api/data")
     * @param method HTTP method (GET, POST, PUT, DELETE, etc.)
     * @param request Request object containing headers and optionally body
     * @param handler Response handler callback (called with complete response)
     * @return true if request was sent successfully, false otherwise
     *
     * @note For sending large request bodies, use request->SetRequestBodyProvider()
     * @note This is the simplest mode for small-to-medium sized responses
     *
     * @example Simple request/response:
     * @code
     * auto req = IRequest::Create();
     * req->SetBody("request data");
     * client->DoRequest(url, HttpMethod::kPost, req,
     *     [](std::shared_ptr<IResponse> resp, uint32_t error) {
     *         if (error == 0) {
     *             std::string body = resp->GetBody();  // Complete response body
     *             process(body);
     *         }
     *     });
     * @endcode
     *
     * @example Large request body with streaming:
     * @code
     * auto req = IRequest::Create();
     * FILE* upload = fopen("upload.dat", "rb");
     * req->SetRequestBodyProvider([upload](uint8_t* buf, size_t size) {
     *     size_t read = fread(buf, 1, size, upload);
     *     if (read == 0) fclose(upload);
     *     return read;
     * });
     * client->DoRequest(url, HttpMethod::kPost, req,
     *     [](std::shared_ptr<IResponse> resp, uint32_t error) {
     *         if (error == 0) {
     *             std::cout << "Upload complete: " << resp->GetStatusCode() << std::endl;
     *         }
     *     });
     * @endcode
     */
    virtual bool DoRequest(const std::string& url, HttpMethod method, std::shared_ptr<IRequest> request,
        const http_response_handler& handler) = 0;

    /**
     * @brief Send an HTTP request with async handler for streaming response
     *
     * In streaming mode, the handler receives callbacks as data arrives:
     * - OnHeaders(): called when response headers are received
     * - OnBodyChunk(): called for each chunk of response body
     * - OnError(): called only if a protocol/network error occurs
     *
     * This is useful for:
     * - Large file downloads
     * - Real-time data streaming
     * - Avoiding memory buffering of large responses
     *
     * @param url Target URL (e.g., "https://example.com/api/data")
     * @param method HTTP method (GET, POST, PUT, DELETE, etc.)
     * @param request Request object containing headers and optionally body
     * @param handler Async handler instance to process response
     * @return true if request was sent successfully, false otherwise
     *
     * @note For sending large request bodies, use request->SetRequestBodyProvider()
     *
     * @example File download with streaming:
     * @code
     * class FileDownloadHandler : public IAsyncClientHandler {
     * public:
     *     void OnHeaders(std::shared_ptr<IResponse> resp) override {
     *         if (resp->GetStatusCode() == 200) {
     *             file_ = fopen("download.dat", "wb");
     *         }
     *     }
     *     void OnBodyChunk(const uint8_t* data, size_t len, bool is_last) override {
     *         if (file_) fwrite(data, 1, len, file_);
     *         if (is_last && file_) {
     *             fclose(file_);
     *             std::cout << "Download complete" << std::endl;
     *         }
     *     }
     *     void OnError(uint32_t error) override {
     *         if (file_) fclose(file_);
     *     }
     * private:
     *     FILE* file_ = nullptr;
     * };
     *
     * auto req = IRequest::Create();
     * client->DoRequest(url, HttpMethod::kGet, req,
     *                  std::make_shared<FileDownloadHandler>());
     * @endcode
     */
    virtual bool DoRequest(const std::string& url, HttpMethod method, std::shared_ptr<IRequest> request,
        std::shared_ptr<IAsyncClientHandler> handler) = 0;

    /**
     * @brief Set push promise handler
     *
     * @param push_promise_handler Push promise handler
     */
    virtual void SetPushPromiseHandler(const http_push_promise_handler& push_promise_handler) = 0;

    /**
     * @brief Set push handler
     *
     * @param push_handler Push handler
     */
    virtual void SetPushHandler(const http_response_handler& push_handler) = 0;

    /**
     * @brief Set the error handler
     *
     * @param error_handler The error handler
     */
    virtual void SetErrorHandler(const error_handler& error_handler) = 0;

    /**
     * @brief Gracefully close all connections
     *
     * Sends CONNECTION_CLOSE frames to all active connections and allows
     * outstanding data to be flushed before closing.
     */
    virtual void Close() = 0;

    /**
     * @brief Initiate connection migration on active connections (simple API)
     *
     * This is a convenience wrapper for interop tests that delegates to
     * InitiateMigrationTo() with the current local IP and a system-chosen port.
     *
     * The migration will create a new socket with:
     * - Same local IP address
     * - New ephemeral port (system-chosen)
     * - Full socket switch and path validation
     *
     * Used primarily for QUIC Interop Runner's connectionmigration test case.
     *
     * @return true if migration was initiated on at least one connection
     *
     * @note For production use, prefer InitiateMigrationTo() which provides
     *       detailed error codes and explicit address control.
     */
    virtual bool InitiateMigration() = 0;

    /**
     * @brief Initiate connection migration to a specific local address (production API)
     *
     * This creates a new socket bound to the specified address and starts
     * path validation on all active connections.
     *
     * @param local_ip New local IP address to migrate to
     * @param local_port New local port (0 = system chooses ephemeral port)
     * @return MigrationResult indicating success or failure
     */
    virtual MigrationResult InitiateMigrationTo(const std::string& local_ip, uint16_t local_port = 0) = 0;

    /**
     * @brief Set callback for migration events
     *
     * The callback is invoked when migration completes (success or failure)
     * on any active connection.
     *
     * @param cb Callback to invoke on migration events
     */
    virtual void SetMigrationCallback(migration_callback cb) = 0;

    /**
     * @brief Create a client instance
     *
     * @param settings HTTP3 settings
     * @return Client instance
     */
    static std::unique_ptr<IClient> Create(const Http3Settings& settings = kDefaultHttp3Settings);
};

}  // namespace quicx

#endif
