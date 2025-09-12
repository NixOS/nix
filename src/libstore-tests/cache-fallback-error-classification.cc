#include <iostream>
#include <string>
#include <cassert>
#include <algorithm>
#include <vector>

// Mock Error class for testing
class Error {
public:
    std::string message;
    Error(const std::string& msg) : message(msg) {}
    const char* what() const { return message.c_str(); }
};

// Function under test - this would be in your actual implementation
bool isRecoverableStoreError(const Error& e) {
    std::string msg = e.what();
    
    // Convert to lowercase for case-insensitive matching
    std::string lower_msg = msg;
    std::transform(lower_msg.begin(), lower_msg.end(), lower_msg.begin(), ::tolower);
    
    // Network timeout errors - be more specific to avoid false positives
    if (lower_msg.find("connection timeout") != std::string::npos ||
        lower_msg.find("timed out") != std::string::npos ||
        lower_msg.find("timeout occurred") != std::string::npos ||
        lower_msg.find("timeout was reached") != std::string::npos ||
        lower_msg.find("operation timeout") != std::string::npos) {
        return true;
    }
    
    // DNS resolution failures
    if (lower_msg.find("could not resolve") != std::string::npos ||
        lower_msg.find("couldn't resolve host") != std::string::npos ||
        lower_msg.find("temporary failure in name resolution") != std::string::npos ||
        lower_msg.find("name resolution failed") != std::string::npos) {
        return true;
    }
    
    // Connection issues
    if (lower_msg.find("connection refused") != std::string::npos ||
        lower_msg.find("network unreachable") != std::string::npos ||
        lower_msg.find("connection reset") != std::string::npos ||
        lower_msg.find("couldn't connect") != std::string::npos) {
        return true;
    }
    
    // HTTP service errors
    if (lower_msg.find("service unavailable") != std::string::npos ||
        lower_msg.find("503") != std::string::npos ||
        lower_msg.find("502") != std::string::npos ||
        lower_msg.find("504") != std::string::npos) {
        return true;
    }
    
    // Curl-specific errors
    if (lower_msg.find("curl: (6)") != std::string::npos ||  // Couldn't resolve host
        lower_msg.find("curl: (7)") != std::string::npos ||  // Couldn't connect
        lower_msg.find("curl: (28)") != std::string::npos || // Timeout reached
        lower_msg.find("curl: (56)") != std::string::npos) { // Connection reset
        return true;
    }
    
    return false;
}

// Test framework
struct TestCase {
    std::string name;
    std::string error_msg;
    bool expected_recoverable;
};

void runTest(const TestCase& test) {
    Error error(test.error_msg);
    bool result = isRecoverableStoreError(error);
    
    if (result == test.expected_recoverable) {
        std::cout << "✓ PASS: " << test.name << std::endl;
    } else {
        std::cout << "✗ FAIL: " << test.name << std::endl;
        std::cout << "  Message: '" << test.error_msg << "'" << std::endl;
        std::cout << "  Expected: " << (test.expected_recoverable ? "recoverable" : "non-recoverable") << std::endl;
        std::cout << "  Got: " << (result ? "recoverable" : "non-recoverable") << std::endl;
        assert(false);
    }
}

int main() {
    std::cout << "=== Error Classification Unit Tests ===" << std::endl;
    
    // Test cases for recoverable errors
    std::vector<TestCase> tests = {
        // Network timeout errors
        {"Network timeout", "Connection timeout occurred", true},
        {"Generic timeout", "Operation timed out", true},
        {"Connection timeout", "connection timeout while downloading", true},
        {"Case insensitive timeout", "CONNECTION TIMEOUT", true},
        
        // DNS resolution failures
        {"DNS resolution failure", "could not resolve hostname", true},
        {"Curl DNS failure", "Couldn't resolve host name", true},
        {"Temporary DNS failure", "temporary failure in name resolution", true},
        {"Name resolution failed", "name resolution failed for host", true},
        
        // Connection issues
        {"Connection refused", "connection refused by server", true},
        {"Network unreachable", "network unreachable", true},
        {"Connection reset", "connection reset by peer", true},
        {"Couldn't connect", "couldn't connect to server", true},
        
        // HTTP service errors
        {"Service unavailable", "503 service unavailable", true},
        {"Bad gateway", "502 bad gateway", true},
        {"Gateway timeout", "504 gateway timeout", true},
        {"Service unavailable text", "service unavailable", true},
        
        // Curl-specific errors
        {"Curl error 6", "curl: (6) Couldn't resolve host", true},
        {"Curl error 7", "curl: (7) Couldn't connect to server", true},
        {"Curl error 28", "curl: (28) Timeout was reached", true},
        {"Curl error 56", "curl: (56) Connection reset by peer", true},
        
        // Non-recoverable errors
        {"Authentication failure", "401 unauthorized", false},
        {"Permission denied", "403 forbidden", false},
        {"Not found", "404 not found", false},
        {"Certificate error", "SSL certificate verification failed", false},
        {"Invalid URL", "malformed URL", false},
        {"Protocol error", "unsupported protocol", false},
        {"File not found", "no such file or directory", false},
        
        // Edge cases
        {"Empty message", "", false},
        {"Unrelated error", "random error message", false},
        {"Mixed case", "Connection TIMEOUT occurred", true},
        {"Partial match", "not a timeout issue", false}
    };
    
    int passed = 0;
    for (const auto& test : tests) {
        try {
            runTest(test);
            passed++;
        } catch (...) {
            // Test failed, already printed error message
        }
    }
    
    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << tests.size() << " tests" << std::endl;
    
    if (passed == tests.size()) {
        std::cout << "All tests passed! Error classification is working correctly." << std::endl;
        return 0;
    } else {
        std::cout << "Some tests failed. Review the output above." << std::endl;
        return 1;
    }
}
