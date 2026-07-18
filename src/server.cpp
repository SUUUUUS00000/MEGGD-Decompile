// server.cpp
//
// Starts an HTTP server that listens for incoming POST requests containing
// Luau bytecode and responds with the decompiled source code. Uses the
// lightweight cpp-httplib library.

#include "httplib.h"
#include "decompiler.hpp"
#include "bytecode_types.hpp"
#include "bytecode_reader.hpp"

#include <iostream>
#include <string>
#include <cstdlib>
#include <vector>
#include <cctype>

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

static inline bool is_base64(unsigned char c) {
  return (std::isalnum(c) || (c == '+') || (c == '/'));
}

// Industry-standard safe Base64 decoder with pre-cleaning.
std::string base64_decode(std::string const& encoded_string) {
  std::string clean_string;
  clean_string.reserve(encoded_string.size());
  for (char c : encoded_string) {
    if (is_base64(c) || c == '=') {
      clean_string.push_back(c);
    }
  }

  int in_len = clean_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;

  while (in_len-- && ( clean_string[in_] != '=') && is_base64(clean_string[in_])) {
    char_array_4[i++] = clean_string[in_]; in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++)
        char_array_4[i] = base64_chars.find(char_array_4[i]);

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++)
        ret += char_array_3[i];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++)
      char_array_4[j] = 0;

    for (j = 0; j < 4; j++)
      char_array_4[j] = base64_chars.find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
  }

  return ret;
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
            // but looks like a Base64 string, we decode it first.
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

            luaudec::Module module = luaudec::BytecodeReader::read(data, size);
            std::string decompiled_code = luaudec::decompileModule(module);

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
