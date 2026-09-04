#include "core/online_session.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_endpoint_parser() {
    const auto parsed = jojo::parse_direct_endpoint("127.0.0.1:34567");
    expect(static_cast<bool>(parsed), "canonical endpoint parses");
    if (parsed) {
        expect(parsed.value == jojo::NetworkEndpoint::loopback(34567),
               "parsed endpoint value matches");
        expect(jojo::format_direct_endpoint(parsed.value) == "127.0.0.1:34567",
               "endpoint formats canonically");
    }

    for (const std::string bad : {
             "", "127.0.0.1", "127.0.0.1:", "127.0.0.1:0",
             "127.0.0.1:65536", "256.0.0.1:1234", "127.0.0:1234",
             "127.0.0.1.2:1234", "host:1234", "127.0.0.1 :1234",
             "127.0.0.1: 1234", "::1:1234"}) {
        const auto result = jojo::parse_direct_endpoint(bad);
        const auto message = "reject endpoint: " + bad;
        expect(!result, message.c_str());
        if (!result) {
            expect(result.error == jojo::ErrorCode::invalid_argument,
                   "malformed endpoint uses invalid_argument");
            expect(!result.detail.empty(), "malformed endpoint has detail");
        }
    }
}
} // namespace

int main() {
    test_endpoint_parser();
    return failures == 0 ? 0 : 1;
}
