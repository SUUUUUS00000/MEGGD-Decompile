// server.cpp
//
// Starts an HTTP server that listens for incoming POST requests containing
// Luau bytecode and responds with the decompiled source code. Uses the
// lightweight cpp-httplib library.

#include "httplib.h"
#include "decompiler.hpp"
#include "bytecode_types.hpp"

#include <iostream>
#include <string>
#include <cstdlib>
#include <vector>

// Helper to decode Base64 payload safely.
std::string base64_decode(const std::string& in)
{
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) 
        T[static_cast<unsigned char>("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i])] = i;

    int val = 0, valb = -8;
    for (unsigned char c : in)
    {
        if (T[c] == -1) 
            continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0)
        {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

int main()
{
    httplib::Server svr;

    svr.Post("/decompile", [](const httplib::Request& req, httplib::Response& res)
    {
        res.set_header("Access-Control-Allow-Origin", "*");

        try
        {
            std::string raw_data = req.body;

            // If the payload does not start with a valid Luau version byte (3..12),
            // but looks like a Base64 string, we decode it first to bypass exploit null-byte truncation.
            if (!raw_data.empty() && (raw_data[0] < 3 || raw_data[0] > 12))
            {
                raw_data = base64_decode(req.body);
            }

            const uint8_t* data = reinterpret_cast<const uint8_t*>(raw_data.data());
            size_t size = raw_data.size();

            if (size == 0)
            {
                res.status = 400;
                res.set_content("Error: Empty bytecode payload", "text/plain");
                return;
            }

            std::string decompiled_code = luaudec::decompile_bytecode(data, size);

            res.status = 200;
            res.set_content(decompiled_code, "text/plain");
        }
        catch (const luaudec::BytecodeReadError& e)
        {
            res.status = 400;
            res.set_content(std::string("Decompilation error: ") + e.what(), "text/plain");
        }
        catch (const std::exception& e)
        {
            res.status = 500;
            res.set_content(std::string("Internal server error: ") + e.what(), "text/plain");
        }
    });

    svr.Options("/decompile", [](const httplib::Request&, httplib::Response& res)
    {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    const char* port_str = std::getenv("PORT");
    int port = port_str ? std::stoi(port_str) : 8080;

    std::cout << "Starting Luau Decompiler Service on port " << port << "..." << std::endl;
    svr.listen("0.0.0.0", port);

    return 0;
}
