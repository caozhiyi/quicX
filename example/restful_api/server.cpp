#include <iostream>
#include <string>
#include <map>
#include <mutex>
#include <sstream>
#include "http3/include/if_server.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

// Simple in-memory database for users
struct User {
    int id;
    std::string name;
    std::string email;
    int age;
};

class UserDatabase {
private:
    std::map<int, User> users_;
    std::mutex mutex_;
    int next_id_ = 1;

public:
    UserDatabase() {
        // Add some initial data
        AddUser("Alice", "alice@example.com", 25);
        AddUser("Bob", "bob@example.com", 30);
        AddUser("Charlie", "charlie@example.com", 35);
    }

    int AddUser(const std::string& name, const std::string& email, int age) {
        std::lock_guard<std::mutex> lock(mutex_);
        int id = next_id_++;
        users_[id] = User{id, name, email, age};
        return id;
    }

    bool GetUser(int id, User& user) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(id);
        if (it != users_.end()) {
            user = it->second;
            return true;
        }
        return false;
    }

    std::vector<User> GetAllUsers() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<User> result;
        for (const auto& pair : users_) {
            result.push_back(pair.second);
        }
        return result;
    }

    bool UpdateUser(int id, const std::string& name, const std::string& email, int age) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(id);
        if (it != users_.end()) {
            it->second.name = name;
            it->second.email = email;
            it->second.age = age;
            return true;
        }
        return false;
    }

    bool DeleteUser(int id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return users_.erase(id) > 0;
    }

    size_t GetUserCount() {
        std::lock_guard<std::mutex> lock(mutex_);
        return users_.size();
    }
};

// Helper functions for JSON-like string formatting
std::string UserToJson(const User& user) {
    std::ostringstream oss;
    oss << "{\"id\":" << user.id 
        << ",\"name\":\"" << user.name << "\""
        << ",\"email\":\"" << user.email << "\""
        << ",\"age\":" << user.age << "}";
    return oss.str();
}

std::string UsersToJson(const std::vector<User>& users) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < users.size(); ++i) {
        oss << UserToJson(users[i]);
        if (i < users.size() - 1) {
            oss << ",";
        }
    }
    oss << "]";
    return oss.str();
}

// Simple JSON parser for this example (in production, use a proper JSON library)
bool ParseUserJson(const std::string& json, std::string& name, std::string& email, int& age) {
    // Very simple parser for demo purposes
    size_t name_pos = json.find("\"name\":\"");
    size_t email_pos = json.find("\"email\":\"");
    size_t age_pos = json.find("\"age\":");
    
    if (name_pos == std::string::npos || email_pos == std::string::npos || age_pos == std::string::npos) {
        return false;
    }

    // Extract name
    name_pos += 8; // length of "name":"
    size_t name_end = json.find("\"", name_pos);
    if (name_end == std::string::npos) return false;
    name = json.substr(name_pos, name_end - name_pos);

    // Extract email
    email_pos += 9; // length of "email":"
    size_t email_end = json.find("\"", email_pos);
    if (email_end == std::string::npos) return false;
    email = json.substr(email_pos, email_end - email_pos);

    // Extract age
    age_pos += 6; // length of "age":
    size_t age_end = json.find_first_of(",}", age_pos);
    if (age_end == std::string::npos) return false;
    std::string age_str = json.substr(age_pos, age_end - age_pos);
    try {
        age = std::stoi(age_str);
    } catch (...) {
        return false;
    }

    return true;
}

int ExtractIdFromPath(const std::string& path) {
    // Extract ID from path like "/users/123"
    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos || last_slash == path.length() - 1) {
        return -1;
    }
    std::string id_str = path.substr(last_slash + 1);
    try {
        return std::stoi(id_str);
    } catch (...) {
        return -1;
    }
}

int main() {

    static const char cert_pem[] =
      "-----BEGIN CERTIFICATE-----\n"
      "MIICWDCCAcGgAwIBAgIJAPuwTC6rEJsMMA0GCSqGSIb3DQEBBQUAMEUxCzAJBgNV\n"
      "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
      "aWRnaXRzIFB0eSBMdGQwHhcNMTQwNDIzMjA1MDQwWhcNMTcwNDIyMjA1MDQwWjBF\n"
      "MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"
      "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKB\n"
      "gQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92kWdGMdAQhLci\n"
      "HnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiFKKAnHmUcrgfV\n"
      "W28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQABo1AwTjAdBgNV\n"
      "HQ4EFgQUi3XVrMsIvg4fZbf6Vr5sp3Xaha8wHwYDVR0jBBgwFoAUi3XVrMsIvg4f\n"
      "Zbf6Vr5sp3Xaha8wDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUFAAOBgQA76Hht\n"
      "ldY9avcTGSwbwoiuIqv0jTL1fHFnzy3RHMLDh+Lpvolc5DSrSJHCP5WuK0eeJXhr\n"
      "T5oQpHL9z/cCDLAKCKRa4uV0fhEdOWBqyR9p8y5jJtye72t6CuFUV5iqcpF4BH4f\n"
      "j2VNHwsSrJwkD4QUGlUtH7vwnQmyCFxZMmWAJg==\n"
      "-----END CERTIFICATE-----\n";

    static const char key_pem[] =
      "-----BEGIN RSA PRIVATE KEY-----\n"
      "MIICXgIBAAKBgQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92\n"
      "kWdGMdAQhLciHnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiF\n"
      "KKAnHmUcrgfVW28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQAB\n"
      "AoGBAIBy09Fd4DOq/Ijp8HeKuCMKTHqTW1xGHshLQ6jwVV2vWZIn9aIgmDsvkjCe\n"
      "i6ssZvnbjVcwzSoByhjN8ZCf/i15HECWDFFh6gt0P5z0MnChwzZmvatV/FXCT0j+\n"
      "WmGNB/gkehKjGXLLcjTb6dRYVJSCZhVuOLLcbWIV10gggJQBAkEA8S8sGe4ezyyZ\n"
      "m4e9r95g6s43kPqtj5rewTsUxt+2n4eVodD+ZUlCULWVNAFLkYRTBCASlSrm9Xhj\n"
      "QpmWAHJUkQJBAOVzQdFUaewLtdOJoPCtpYoY1zd22eae8TQEmpGOR11L6kbxLQsk\n"
      "aMly/DOnOaa82tqAGTdqDEZgSNmCeKKknmECQAvpnY8GUOVAubGR6c+W90iBuQLj\n"
      "LtFp/9ihd2w/PoDwrHZaoUYVcT4VSfJQog/k7kjE4MYXYWL8eEKg3WTWQNECQQDk\n"
      "104Wi91Umd1PzF0ijd2jXOERJU1wEKe6XLkYYNHWQAe5l4J4MWj9OdxFXAxIuuR/\n"
      "tfDwbqkta4xcux67//khAkEAvvRXLHTaa6VFzTaiiO8SaFsHV3lQyXOtMrBpB5jd\n"
      "moZWgjHvB2W9Ckn7sDqsPB+U2tyX0joDdQEyuiMECDY8oQ==\n"
      "-----END RSA PRIVATE KEY-----\n";

    // Create database instance
    auto db = std::make_shared<UserDatabase>();

    auto server = quicx::http3::IServer::Create();

    // Logging middleware - runs before all handlers
    server->AddMiddleware(
        quicx::http3::HttpMethod::kAny,
        quicx::http3::MiddlewarePosition::kBefore,
        [](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            std::cout << "[" << req->GetMethodString() << "] " << req->GetPath() << std::endl;
        }
    );

    // GET /users - Get all users
    server->AddHandler(
        quicx::http3::HttpMethod::kGet,
        "/users",
        [db](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            auto users = db->GetAllUsers();
            std::string json = UsersToJson(users);
            
            resp->AddHeader("Content-Type", "application/json");
            resp->SetBody(json);
            resp->SetStatusCode(200);
            
            std::cout << "  -> Returned " << users.size() << " users" << std::endl;
        }
    );

    // GET /users/:id - Get single user
    server->AddHandler(
        quicx::http3::HttpMethod::kGet,
        "/users/:id",
        [db](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            int id = ExtractIdFromPath(req->GetPath());
            if (id < 0) {
                resp->SetStatusCode(400);
                resp->SetBody("{\"error\":\"Invalid user ID\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> Error: Invalid ID" << std::endl;
                return;
            }

            User user;
            if (db->GetUser(id, user)) {
                std::string json = UserToJson(user);
                resp->AddHeader("Content-Type", "application/json");
                resp->SetBody(json);
                resp->SetStatusCode(200);
                std::cout << "  -> Returned user: " << user.name << std::endl;
            } else {
                resp->SetStatusCode(404);
                resp->SetBody("{\"error\":\"User not found\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> User not found (ID: " << id << ")" << std::endl;
            }
        }
    );

    // POST /users - Create new user
    server->AddHandler(
        quicx::http3::HttpMethod::kPost,
        "/users",
        [db](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            std::string name, email;
            int age;
            
            if (!ParseUserJson(req->GetBody(), name, email, age)) {
                resp->SetStatusCode(400);
                resp->SetBody("{\"error\":\"Invalid JSON format\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> Error: Invalid JSON" << std::endl;
                return;
            }

            int id = db->AddUser(name, email, age);
            User user{id, name, email, age};
            std::string json = UserToJson(user);
            
            resp->AddHeader("Content-Type", "application/json");
            resp->AddHeader("Location", "/users/" + std::to_string(id));
            resp->SetBody(json);
            resp->SetStatusCode(201); // Created
            
            std::cout << "  -> Created user: " << name << " (ID: " << id << ")" << std::endl;
        }
    );

    // PUT /users/:id - Update user
    server->AddHandler(
        quicx::http3::HttpMethod::kPut,
        "/users/:id",
        [db](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            int id = ExtractIdFromPath(req->GetPath());
            if (id < 0) {
                resp->SetStatusCode(400);
                resp->SetBody("{\"error\":\"Invalid user ID\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> Error: Invalid ID" << std::endl;
                return;
            }

            std::string name, email;
            int age;
            
            if (!ParseUserJson(req->GetBody(), name, email, age)) {
                resp->SetStatusCode(400);
                resp->SetBody("{\"error\":\"Invalid JSON format\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> Error: Invalid JSON" << std::endl;
                return;
            }

            if (db->UpdateUser(id, name, email, age)) {
                User user{id, name, email, age};
                std::string json = UserToJson(user);
                resp->AddHeader("Content-Type", "application/json");
                resp->SetBody(json);
                resp->SetStatusCode(200);
                std::cout << "  -> Updated user: " << name << " (ID: " << id << ")" << std::endl;
            } else {
                resp->SetStatusCode(404);
                resp->SetBody("{\"error\":\"User not found\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> User not found (ID: " << id << ")" << std::endl;
            }
        }
    );

    // DELETE /users/:id - Delete user
    server->AddHandler(
        quicx::http3::HttpMethod::kDelete,
        "/users/:id",
        [db](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            int id = ExtractIdFromPath(req->GetPath());
            if (id < 0) {
                resp->SetStatusCode(400);
                resp->SetBody("{\"error\":\"Invalid user ID\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> Error: Invalid ID" << std::endl;
                return;
            }

            if (db->DeleteUser(id)) {
                resp->SetStatusCode(204); // No Content
                resp->SetBody("");
                std::cout << "  -> Deleted user (ID: " << id << ")" << std::endl;
            } else {
                resp->SetStatusCode(404);
                resp->SetBody("{\"error\":\"User not found\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> User not found (ID: " << id << ")" << std::endl;
            }
        }
    );

    // GET /stats - Get statistics
    server->AddHandler(
        quicx::http3::HttpMethod::kGet,
        "/stats",
        [db](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            std::ostringstream oss;
            oss << "{\"total_users\":" << db->GetUserCount() << "}";
            
            resp->AddHeader("Content-Type", "application/json");
            resp->SetBody(oss.str());
            resp->SetStatusCode(200);
            
            std::cout << "  -> Statistics requested" << std::endl;
        }
    );

    // Response time middleware - runs after all handlers
    server->AddMiddleware(
        quicx::http3::HttpMethod::kAny,
        quicx::http3::MiddlewarePosition::kAfter,
        [](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            resp->AddHeader("X-Powered-By", "QuicX-HTTP3");
            resp->AddHeader("Access-Control-Allow-Origin", "*"); // CORS
        }
    );

    // Configure and start server
    quicx::http3::Http3ServerConfig config;
    config.cert_pem_ = cert_pem;
    config.key_pem_ = key_pem;
    config.config_.thread_num_ = 2;
    config.config_.log_level_ = quicx::http3::LogLevel::kError;
    
    server->Init(config);
    
    std::cout << "==================================" << std::endl;
    std::cout << "RESTful API Server Starting..." << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Listen on: https://0.0.0.0:8883" << std::endl;
    std::cout << std::endl;
    std::cout << "Available endpoints:" << std::endl;
    std::cout << "  GET    /users       - Get all users" << std::endl;
    std::cout << "  GET    /users/:id   - Get single user" << std::endl;
    std::cout << "  POST   /users       - Create new user" << std::endl;
    std::cout << "  PUT    /users/:id   - Update user" << std::endl;
    std::cout << "  DELETE /users/:id   - Delete user" << std::endl;
    std::cout << "  GET    /stats       - Get statistics" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;

    if (!server->Start("0.0.0.0", 8883)) {
        std::cout << "Failed to start server" << std::endl;
        return 1;
    }
    
    server->Join();
    return 0;
}

