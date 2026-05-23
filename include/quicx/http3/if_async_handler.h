#ifndef HTTP3_INCLUDE_ASYNC_HANDLER
#define HTTP3_INCLUDE_ASYNC_HANDLER

#include <cstddef>
#include <cstdint>
#include <memory>

namespace quicx {

// Forward declarations
class IRequest;
class IResponse;

/**
 * @brief Async handler for server-side streaming request processing
 *
 * This handler is used when the server needs to process request body
 * in a streaming fashion (e.g., large file uploads, real-time data).
 *
 * Handler callbacks are invoked in this order:
 * 1. OnHeaders() - called when request headers are received
 * 2. OnBodyChunk() - called for each body chunk (may be called multiple times)
 * 3. OnError() - called only if a protocol/network error occurs
 *
 * @note For sending response body in streaming mode, use
 *       response->SetResponseBodyProvider() in OnHeaders().
 *
 * @example
 * @code
 * class FileUploadHandler : public IAsyncServerHandler {
 *     FILE* file_ = nullptr;
 *
 *     void OnHeaders(std::shared_ptr<IRequest> request,
 *                   std::shared_ptr<IResponse> response) override {
 *         // Called when headers received (before body)
 *         std::string filename = request->GetPathParam("filename");
 *         file_ = fopen(filename.c_str(), "wb");
 *
 *         response->SetStatusCode(200);
 *         response->AddHeader("Content-Type", "text/plain");
 *         response->SetBody("Upload started");
 *     }
 *
 *     void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
 *         // Called for each chunk of request body
 *         if (file_) {
 *             fwrite(data, 1, length, file_);
 *         }
 *         if (is_last && file_) {
 *             fclose(file_);
 *             file_ = nullptr;
 *             LOG("Upload completed");
 *         }
 *     }
 *
 *     void OnError(uint32_t error_code) override {
 *         // Called only on protocol/network errors
 *         if (file_) {
 *             fclose(file_);
 *             file_ = nullptr;
 *         }
 *         LOG("Error occurred: %u", error_code);
 *     }
 * };
 *
 * // Register handler
 * server->AddHandler(HttpMethod::kPost, "/upload/:filename",
 *                    std::make_shared<FileUploadHandler>());
 * @endcode
 */
class IAsyncServerHandler {
public:
    /**
     * @brief Called when request headers are received
     *
     * This is the first callback invoked for a streaming request.
     * At this point, request headers, path parameters, and query parameters
     * are available, but the request body has not been received yet.
     *
     * Use this callback to:
     * - Inspect request headers, path, method, etc.
     * - Validate the request
     * - Set response headers and status code
     * - Set response body provider for streaming response (if needed)
     * - Initialize state for receiving body chunks
     *
     * @param request Request object containing:
     *                - Headers (GetHeaders(), GetHeader())
     *                - Path and query parameters (GetPathParam(), GetQueryParam())
     *                - HTTP method (GetMethod())
     *                - Note: GetBody() will return empty string at this point
     *
     * @param response Response object to configure:
     *                 - SetStatusCode() to set HTTP status
     *                 - AddHeader() to set response headers
     *                 - SetBody() or SetResponseBodyProvider() to send response
     *
     * @note At this point, request body has NOT been received yet.
     *       Use OnBodyChunk() to process incoming body data.
     *
     * @note Any exceptions thrown from this callback will be caught and
     *       OnError() will be called with an error code.
     */
    virtual void OnHeaders(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) = 0;

    /**
     * @brief Called when a request body chunk is received
     *
     * This callback is invoked for each chunk of request body data as it arrives.
     * For large request bodies, this may be called multiple times.
     *
     * @param data Pointer to chunk data
     *             WARNING: This pointer is only valid during this callback.
     *             If you need to retain the data, you MUST copy it.
     *
     * @param length Length of the chunk in bytes
     *               May be 0 if the request has no body (is_last will be true)
     *
     * @param is_last True if this is the final chunk, false otherwise
     *                When true, no more OnBodyChunk() calls will follow
     *
     * @note The data pointer is only valid during this callback execution.
     *       Copy the data if you need to use it after the callback returns.
     *
     * @note For requests with no body, this will be called once with
     *       length=0 and is_last=true.
     *
     */
    virtual void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) = 0;

    /**
     * @brief Called when a protocol or network error occurs
     *
     * **IMPORTANT**: This callback is ONLY invoked for **protocol/network errors**
     * during request processing, such as connection reset, stream abort, or frame
     * decode errors. It is **NOT** used for application-level error responses.
     *
     * **When this callback IS invoked:**
     * - Client abruptly closed the connection
     * - QUIC stream was reset by peer
     * - HTTP/3 frame decoding failed (malformed request data)
     * - Connection lost during request processing
     *
     * **When this callback is NOT invoked:**
     * - Application decides to return an error response (400, 500, etc.)
     *   → Set status code in OnHeaders() using response->SetStatusCode()
     * - Request validation fails (missing required headers, etc.)
     *   → Set appropriate status code (400, 422, etc.) in response
     * - Business logic error (resource not found, permission denied, etc.)
     *   → Return corresponding HTTP status (404, 403, etc.)
     *
     * **After OnError() is called:**
     * - The request processing has permanently failed at protocol level
     * - No further callbacks will be invoked
     * - Clean up any resources (close files, cancel operations, etc.)
     *
     * @param error_code Error code indicating the type of protocol/network error
     *
     * @example Correct error handling:
     * @code
     * class UploadHandler : public IAsyncServerHandler {
     *     void OnHeaders(std::shared_ptr<IRequest> req,
     *                   std::shared_ptr<IResponse> resp) override {
     *         // Application-level validation
     *         if (!req->GetHeader("content-type").starts_with("image/")) {
     *             resp->SetStatusCode(400);  // Use HTTP status, NOT OnError
     *             resp->SetBody("Invalid content type");
     *             return;
     *         }
     *         // Start processing...
     *     }
     *     void OnError(uint32_t error) override {
     *         // Protocol error - client disconnected
     *         CleanupPartialUpload();
     *     }
     * };
     * @endcode
     */
    virtual void OnError(uint32_t error_code) = 0;
};

/**
 * @brief Async handler for client-side streaming response processing
 *
 * This handler is used when the client needs to process response body
 * in a streaming fashion (e.g., large file downloads, real-time data).
 *
 * Handler callbacks are invoked in this order:
 * 1. OnHeaders() - called when response headers are received
 * 2. OnBodyChunk() - called for each body chunk (may be called multiple times)
 * 3. OnError() - called only if a protocol/network error occurs
 *
 * @note For sending request body in streaming mode, use
 *       request->SetRequestBodyProvider() before calling DoRequest().
 *
 * @example
 * @code
 * class FileDownloadHandler : public IAsyncClientHandler {
 *     FILE* file_ = nullptr;
 *
 *     void OnHeaders(std::shared_ptr<IResponse> response) override {
 *         // Called when response headers received (before body)
 *         int status = response->GetStatusCode();
 *         if (status == 200) {
 *             file_ = fopen("download.dat", "wb");
 *             LOG("Download started, status: %d", status);
 *         } else {
 *             LOG("Download failed, status: %d", status);
 *         }
 *     }
 *
 *     void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
 *         // Called for each chunk of response body
 *         if (file_) {
 *             fwrite(data, 1, length, file_);
 *         }
 *         if (is_last && file_) {
 *             fclose(file_);
 *             file_ = nullptr;
 *             LOG("Download completed");
 *         }
 *     }
 *
 *     void OnError(uint32_t error_code) override {
 *         // Called only on protocol/network errors
 *         if (file_) {
 *             fclose(file_);
 *             file_ = nullptr;
 *         }
 *         LOG("Error occurred: %u", error_code);
 *     }
 * };
 *
 * // Use handler
 * auto request = IRequest::Create();
 * client->DoRequest("https://example.com/file.dat", HttpMethod::kGet,
 *                   request, std::make_shared<FileDownloadHandler>());
 * @endcode
 */
class IAsyncClientHandler {
public:
    /**
     * @brief Called when response headers are received
     *
     * This is the first callback invoked for a streaming response.
     * At this point, response headers and status code are available,
     * but the response body has not been received yet.
     *
     * Use this callback to:
     * - Inspect response status code (GetStatusCode())
     * - Check response headers (GetHeaders(), GetHeader())
     * - Validate the response
     * - Prepare for receiving body chunks (e.g., open output file)
     *
     * @param response Response object containing:
     *                 - Status code (GetStatusCode())
     *                 - Headers (GetHeaders(), GetHeader())
     *                 - Note: GetBody() will return empty string at this point
     *
     * @note At this point, response body has NOT been received yet.
     *       Use OnBodyChunk() to process incoming body data.
     *
     * @note Check response->GetStatusCode() for HTTP errors (404, 500, etc.).
     *       OnError() is only called for protocol/network errors, not HTTP errors.
     *
     * @note Any exceptions thrown from this callback will be caught and
     *       OnError() will be called with an error code.
     */
    virtual void OnHeaders(std::shared_ptr<IResponse> response) = 0;

    /**
     * @brief Called when a response body chunk is received
     *
     * This callback is invoked for each chunk of response body data as it arrives.
     * For large response bodies, this may be called multiple times.
     *
     * @param data Pointer to chunk data
     *             WARNING: This pointer is only valid during this callback.
     *             If you need to retain the data, you MUST copy it.
     *
     * @param length Length of the chunk in bytes
     *               May be 0 if the response has no body (is_last will be true)
     *
     * @param is_last True if this is the final chunk, false otherwise
     *                When true, no more OnBodyChunk() calls will follow
     *
     * @note The data pointer is only valid during this callback execution.
     *       Copy the data if you need to use it after the callback returns.
     *
     * @note For responses with no body, this will be called once with
     *       length=0 and is_last=true.
     *
     */
    virtual void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) = 0;

    /**
     * @brief Called when a protocol or network error occurs
     *
     * **IMPORTANT**: This callback is ONLY invoked for **protocol/network errors**,
     * such as connection timeout, TLS handshake failure, stream reset, or frame
     * decode errors. It is **NOT** called for HTTP status errors (400, 404, 500, etc.).
     *
     * **HTTP status errors (4xx, 5xx) are NOT protocol errors:**
     * - A server returning 404 or 500 is a successful HTTP transaction
     * - These status codes are delivered via OnHeaders() where you check GetStatusCode()
     * - OnError() will NOT be called for HTTP status errors
     *
     * **When this callback IS invoked:**
     * - Connection failed to establish (timeout, network unreachable, etc.)
     * - TLS handshake failed (certificate error, protocol version mismatch, etc.)
     * - QUIC stream was reset by peer
     * - HTTP/3 frame decoding failed (malformed data)
     * - Connection was closed unexpectedly
     *
     * **When this callback is NOT invoked:**
     * - Server returned 4xx client error (400, 404, etc.) → Use OnHeaders() + GetStatusCode()
     * - Server returned 5xx server error (500, 503, etc.) → Use OnHeaders() + GetStatusCode()
     * - Response completed normally with any HTTP status → OnHeaders() + OnBodyChunk() called
     *
     * **After OnError() is called:**
     * - No OnHeaders() or OnBodyChunk() callbacks will be invoked
     * - The request has permanently failed
     * - Clean up any resources (close files, free memory, etc.)
     *
     * @param error_code Error code indicating the type of protocol/network error
     *                   Examples: connection timeout, TLS error, stream reset, etc.
     *
     * @example Correct error handling:
     * @code
     * class MyHandler : public IAsyncClientHandler {
     *     void OnHeaders(std::shared_ptr<IResponse> resp) override {
     *         int status = resp->GetStatusCode();
     *         if (status >= 400) {
     *             // HTTP error - handle here, NOT in OnError()
     *             LOG("HTTP error: %d", status);
     *         }
     *     }
     *     void OnError(uint32_t error) override {
     *         // Protocol/network error - connection failed
     *         LOG("Protocol error: %u", error);
     *     }
     * };
     * @endcode
     */
    virtual void OnError(uint32_t error_code) = 0;
};

}  // namespace quicx

#endif
