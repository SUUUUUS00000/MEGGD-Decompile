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

int main()
{
    httplib::Server svr;

    svr.Post("/decompile", [](const httplib::Request& req, httplib::Response& res)
    {
        res.set_header("Access-Control-Allow-Origin", "*");

        try
        {
            const uint8_t* data = reinterpret_cast<const uint8_t*>(req.body.data());
            size_t size = req.body.size();

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
