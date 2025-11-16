#ifndef HTTP3_HTTP_IF_REQUEST
#define HTTP3_HTTP_IF_REQUEST

#include <string>
#include <memory>
#include <unordered_map>
#include "http3/include/type.h"
#include "common/include/if_buffer_read.h"

namespace quicx {

/**
 * @brief Interface for HTTP3 requests
 * 
 * This interface provides methods to set and get request information.
 */
class IRequest {
public:
    IRequest() {}
    virtual ~IRequest() = default;

    /**
     * @brief Set the HTTP method
     * 
     * @param method The HTTP method
     */
    virtual void SetMethod(HttpMethod method) = 0;

    /**
     * @brief Get the HTTP method
     * 
     * @return The HTTP method
     */
    virtual HttpMethod GetMethod() const = 0;

    /**
     * @brief Get the HTTP method string
     * 
     * @return The HTTP method string
     */
    virtual std::string GetMethodString() const = 0;


    /**
     * @brief Set the request path
     * 
     * @param path The request path
     */
    virtual void SetPath(const std::string& path) = 0;

    /**
     * @brief Get the request path
     * 
     * @return The request path
     */
    virtual const std::string& GetPath() const = 0;


    /**
     * @brief Set the request scheme
     * 
     * @param scheme The request scheme
     */
    virtual void SetScheme(const std::string& scheme) = 0;
    /**
     * @brief Get the request scheme
     * 
     * @return The request scheme
     */
    virtual const std::string& GetScheme() const = 0;


    /**
     * @brief Set the request authority
     * 
     * @param authority The request authority
     */
    virtual void SetAuthority(const std::string& authority) = 0;
    /**
     * @brief Get the request authority
     * 
     * @return The request authority
     */
    virtual const std::string& GetAuthority() const = 0;


    /**
     * @brief Add a header
     * 
     * @param name The header name
     * @param value The header value
     */
    virtual void AddHeader(const std::string& name, const std::string& value) = 0;

    /**
     * @brief Get a header
     * 
     * @param name The header name
     * @param value The header value
     * @return True if the header is found, false otherwise
     */
    virtual bool GetHeader(const std::string& name, std::string& value) const = 0;

    /**
     * @brief Set the headers
     * 
     * @param headers The headers
     */
    virtual void SetHeaders(const std::unordered_map<std::string, std::string>& headers) = 0;
    /**
     * @brief Get the headers
     * 
     * @return The headers
     */
    virtual std::unordered_map<std::string, std::string>& GetHeaders() = 0;
    /**
     * @brief Get the request body as a string
     * 
     * @return The request body as a string
     */
    virtual std::string GetBodyAsString() const = 0;


    /**
     * @brief Set complete request body (complete mode)
     */
    virtual void AppendBody(const std::string& body) = 0;
    virtual void AppendBody(const uint8_t* data, uint32_t length) = 0;

    /**
     * @brief Get the complete request body
     * 
     * @return The request body
     * @note if SetRequestBodyProvider() is called, this body will be empty.
     */
     virtual std::shared_ptr<IBufferRead> GetBody() const = 0;

    /**
     * @brief Set request body provider for streaming mode
     * 
     * Use this for large bodies that should be sent incrementally without
     * buffering the entire content in memory.
     * 
     * @param provider Callback function that provides body chunks
     * 
     * @note If this is set, SetBody() content will be ignored.
     * 
     * @example
     * @code
     * FILE* file = fopen("large_upload.dat", "rb");
     * request->SetRequestBodyProvider([file](uint8_t* buf, size_t size) -> size_t {
     *     size_t read = fread(buf, 1, size, file);
     *     if (read == 0) fclose(file);
     *     return read;  // Return 0 to indicate end of body
     * });
     * @endcode
     */
     virtual void SetRequestBodyProvider(const body_provider& provider) = 0;

    /**
     * @brief Get the request body provider
     * 
     * @return The request body provider
     */
    virtual body_provider GetRequestBodyProvider() const = 0;

    /**
     * @brief Get the query parameters
     * 
     * @return The query parameters
     * @note example:path="/api/users?page=1&limit=10" -> path="/api/users", query={{"page", "1"}, {"limit", "10"}}
     */
    virtual const std::unordered_map<std::string, std::string>& GetQueryParams() const = 0;

    /**
     * @brief Get the path parameters
     * 
     * @return The path parameters
     * @note example: route="/users/:id", request="/users/123" -> {{"id", "123"}}
     */
    virtual const std::unordered_map<std::string, std::string>& GetPathParams() const = 0;

    /**
     * @brief Create a request instance
     * 
     * @return The request instance
     */
    static std::shared_ptr<IRequest> Create();
};

}

#endif

