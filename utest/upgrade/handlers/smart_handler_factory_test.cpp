#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "upgrade/include/type.h"
#include "upgrade/handlers/http_smart_handler.h"
#include "upgrade/handlers/https_smart_handler.h"
#include "upgrade/handlers/smart_handler_factory.h"

namespace quicx {
namespace upgrade {
namespace {

class MockTcpAction:
    public ITcpAction {
public:
    MockTcpAction() : init_called_(false), stop_called_(false), join_called_(false) {}

    virtual bool Init() override {
        init_called_ = true;
        return true;
    }
    
    virtual bool AddListener(const std::string& addr, uint16_t port, std::shared_ptr<ISocketHandler> handler) override {
        return true;
    }
    
    virtual void Stop() override {
        stop_called_ = true;
    }
    
    virtual void Join() override {
        join_called_ = true;
    }
    
    virtual uint64_t AddTimer(std::function<void()> callback, uint32_t timeout_ms) override {  
        return 0;
    }
    
    virtual bool RemoveTimer(uint64_t timer_id) override {
        return true;
    }
    
    bool IsInitCalled() const { return init_called_; }
    bool IsStopCalled() const { return stop_called_; }
    bool IsJoinCalled() const { return join_called_; }
    
private:
    std::atomic<bool> init_called_;
    std::atomic<bool> stop_called_;
    std::atomic<bool> join_called_;
};

class SmartHandlerFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test fixtures
        tcp_action_ = std::make_shared<MockTcpAction>();
    }
    
    void TearDown() override {
        // Clean up test fixtures
    }

    std::shared_ptr<MockTcpAction> tcp_action_;
};

// Test factory creation
TEST_F(SmartHandlerFactoryTest, FactoryCreation) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    EXPECT_NE(factory, nullptr);
}

// Test HTTP handler creation
TEST_F(SmartHandlerFactoryTest, CreateHttpHandler) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Create settings for HTTP
    UpgradeSettings settings;
    settings.http_port = 8080;
    settings.https_port = 0; // Disable HTTPS
    
    auto handler = factory->CreateHandler(settings, tcp_action_);
    EXPECT_NE(handler, nullptr);
    
    // Should be HTTP handler
    EXPECT_EQ(handler->GetType(), "HTTP");
    
    // Dynamic cast should work
    auto http_handler = std::dynamic_pointer_cast<HttpSmartHandler>(handler);
    EXPECT_NE(http_handler, nullptr);
}

// Test HTTPS handler creation
TEST_F(SmartHandlerFactoryTest, CreateHttpsHandler) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Create settings for HTTPS
    UpgradeSettings settings;
    settings.http_port = 0; // Disable HTTP
    settings.https_port = 8443;
    settings.cert_file = "test.crt";
    settings.key_file = "test.key";
    
    auto handler = factory->CreateHandler(settings, tcp_action_);
    EXPECT_NE(handler, nullptr);
    
    // Should be HTTPS handler
    EXPECT_EQ(handler->GetType(), "HTTPS");
    
    // Dynamic cast should work
    auto https_handler = std::dynamic_pointer_cast<HttpsSmartHandler>(handler);
    EXPECT_NE(https_handler, nullptr);
}

// Test handler creation with both HTTP and HTTPS enabled
TEST_F(SmartHandlerFactoryTest, CreateHandlerWithBothEnabled) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Create settings with both HTTP and HTTPS
    UpgradeSettings settings;
    settings.http_port = 8080;
    settings.https_port = 8443;
    settings.cert_file = "test.crt";
    settings.key_file = "test.key";
    
    auto handler = factory->CreateHandler(settings, tcp_action_);
    EXPECT_NE(handler, nullptr);
    
    // Should be HTTPS handler (preferred when both are enabled)
    EXPECT_EQ(handler->GetType(), "HTTPS");
    
    // Dynamic cast should work
    auto https_handler = std::dynamic_pointer_cast<HttpsSmartHandler>(handler);
    EXPECT_NE(https_handler, nullptr);
}

// Test handler creation with neither HTTP nor HTTPS enabled
TEST_F(SmartHandlerFactoryTest, CreateHandlerWithNeitherEnabled) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Create settings with neither HTTP nor HTTPS
    UpgradeSettings settings;
    settings.http_port = 0;
    settings.https_port = 0;
    
    auto handler = factory->CreateHandler(settings, tcp_action_);
    EXPECT_NE(handler, nullptr);
    
    // Should default to HTTP handler
    EXPECT_EQ(handler->GetType(), "HTTP");
    
    // Dynamic cast should work
    auto http_handler = std::dynamic_pointer_cast<HttpSmartHandler>(handler);
    EXPECT_NE(http_handler, nullptr);
}

// Test handler creation with invalid HTTPS settings
TEST_F(SmartHandlerFactoryTest, CreateHandlerWithInvalidHttpsSettings) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Create settings with HTTPS port but no certificate
    UpgradeSettings settings;
    settings.http_port = 0;
    settings.https_port = 8443;
    // No cert_file or key_file
    
    auto handler = factory->CreateHandler(settings, tcp_action_);
    EXPECT_NE(handler, nullptr);
    
    // Should fall back to HTTP handler
    EXPECT_EQ(handler->GetType(), "HTTP");
    
    // Dynamic cast should work
    auto http_handler = std::dynamic_pointer_cast<HttpSmartHandler>(handler);
    EXPECT_NE(http_handler, nullptr);
}

// Test handler creation with partial HTTPS settings
TEST_F(SmartHandlerFactoryTest, CreateHandlerWithPartialHttpsSettings) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Create settings with only cert file
    UpgradeSettings settings1;
    settings1.http_port = 0;
    settings1.https_port = 8443;
    settings1.cert_file = "test.crt";
    // No key_file
    
    auto handler1 = factory->CreateHandler(settings1, tcp_action_);
    EXPECT_NE(handler1, nullptr);
    EXPECT_EQ(handler1->GetType(), "HTTP");
    
    // Create settings with only key file
    UpgradeSettings settings2;
    settings2.http_port = 0;
    settings2.https_port = 8443;
    // No cert_file
    settings2.key_file = "test.key";
    
    auto handler2 = factory->CreateHandler(settings2, tcp_action_);
    EXPECT_NE(handler2, nullptr);
    EXPECT_EQ(handler2->GetType(), "HTTP");
}

// Test handler creation with different port configurations
TEST_F(SmartHandlerFactoryTest, CreateHandlerWithDifferentPorts) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Test different HTTP ports
    UpgradeSettings settings1;
    settings1.http_port = 80;
    settings1.https_port = 0;
    
    auto handler1 = factory->CreateHandler(settings1, tcp_action_);
    EXPECT_NE(handler1, nullptr);
    EXPECT_EQ(handler1->GetType(), "HTTP");
    
    // Test different HTTPS ports
    UpgradeSettings settings2;
    settings2.http_port = 0;
    settings2.https_port = 443;
    settings2.cert_file = "test.crt";
    settings2.key_file = "test.key";
    
    auto handler2 = factory->CreateHandler(settings2, tcp_action_);
    EXPECT_NE(handler2, nullptr);
    EXPECT_EQ(handler2->GetType(), "HTTPS");
}

// Test handler creation with empty certificate files
TEST_F(SmartHandlerFactoryTest, CreateHandlerWithEmptyCertFiles) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Create settings with empty certificate files
    UpgradeSettings settings;
    settings.http_port = 0;
    settings.https_port = 8443;
    settings.cert_file = "";
    settings.key_file = "";
    
    auto handler = factory->CreateHandler(settings, tcp_action_);
    EXPECT_NE(handler, nullptr);
    
    // Should fall back to HTTP handler
    EXPECT_EQ(handler->GetType(), "HTTP");
}

// Test multiple handler creation
TEST_F(SmartHandlerFactoryTest, MultipleHandlerCreation) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Create HTTP handler
    UpgradeSettings http_settings;
    http_settings.http_port = 8080;
    http_settings.https_port = 0;
    
    auto http_handler = factory->CreateHandler(http_settings, tcp_action_);
    EXPECT_NE(http_handler, nullptr);
    EXPECT_EQ(http_handler->GetType(), "HTTP");
    
    // Create HTTPS handler
    UpgradeSettings https_settings;
    https_settings.http_port = 0;
    https_settings.https_port = 8443;
    https_settings.cert_file = "test.crt";
    https_settings.key_file = "test.key";
    
    auto https_handler = factory->CreateHandler(https_settings, tcp_action_);
    EXPECT_NE(https_handler, nullptr);
    EXPECT_EQ(https_handler->GetType(), "HTTPS");
    
    // Handlers should be different instances
    EXPECT_NE(http_handler, https_handler);
}

// Test handler creation with default settings
TEST_F(SmartHandlerFactoryTest, CreateHandlerWithDefaultSettings) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Create settings with default values
    UpgradeSettings settings;
    // All fields are default-initialized
    
    auto handler = factory->CreateHandler(settings, tcp_action_);
    EXPECT_NE(handler, nullptr);
    
    // Should default to HTTP handler
    EXPECT_EQ(handler->GetType(), "HTTP");
}

// Test handler creation with large port numbers
TEST_F(SmartHandlerFactoryTest, CreateHandlerWithLargePorts) {
    auto factory = std::make_unique<SmartHandlerFactory>();
    
    // Test with large port numbers
    UpgradeSettings settings;
    settings.http_port = 65535;
    settings.https_port = 0;
    
    auto handler = factory->CreateHandler(settings, tcp_action_);
    EXPECT_NE(handler, nullptr);
    EXPECT_EQ(handler->GetType(), "HTTP");
}

}
} // namespace upgrade
} // namespace quicx 