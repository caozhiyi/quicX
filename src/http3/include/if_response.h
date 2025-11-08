#ifndef HTTP3_HTTP_IF_RESPONSE
#define HTTP3_HTTP_IF_RESPONSE

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include "http3/include/type.h" 

namespace quicx {
namespace http3 {

/**
 * @brief Interface for HTTP3 responses
 * 
 * This interface provides methods to set and get response information.
 */
class IResponse {
public:
    IResponse() {}
    virtual ~IResponse() = default;

    /**
     * @brief Set the status code
     * 
     * @param status_code The status code
     */
    virtual void SetStatusCode(uint32_t status_code) = 0;

    /**
     * @brief Get the status code
     * 
     * @return The status code
     */
    virtual uint32_t GetStatusCode() const = 0;

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
     * @brief Set complete response body (complete mode)
     * 
     * Use this for small to medium-sized bodies that can be buffered in memory.
     * 
     * @param body The complete response body as a string
     * 
     * @note If SetResponseBodyProvider() is called, this body will be ignored.
     */
    virtual void SetBody(const std::string& body) = 0;

    /**
     * @brief Get the complete response body
     * 
     * @return Reference to the response body string
     */
     virtual const std::string& GetBody() const = 0;

    /**
     * @brief Set response body provider for streaming mode
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
     * FILE* file = fopen("large_file.dat", "rb");
     * response->SetResponseBodyProvider([file](uint8_t* buf, size_t size) -> size_t {
     *     size_t read = fread(buf, 1, size, file);
     *     if (read == 0) fclose(file);
     *     return read;  // Return 0 to indicate end of body
     * });
     * @endcode
     */
     virtual void SetResponseBodyProvider(const body_provider& provider) = 0;

    /**
     * @brief Get the response body provider
     * 
     * @return The response body provider
     */
    virtual body_provider GetResponseBodyProvider() const = 0;
    

    /**
     * @brief Append a push response
     * 
     * @param response The push response
     * @note can be called multiple times, if push response is not enabled, it will be ignored
     */
    virtual void AppendPush(std::shared_ptr<IResponse> response) = 0;

    /**
     * @brief Get the push responses
     * 
     * @return The push responses
     */
    virtual std::vector<std::shared_ptr<IResponse>>& GetPushResponses() = 0;

    /**
     * @brief Create a response instance
     * 
     * @return The response instance
     */
    static std::shared_ptr<IResponse> Create();
};

}
}

#endif
