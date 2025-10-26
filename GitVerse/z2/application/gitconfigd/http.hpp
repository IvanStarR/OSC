#pragma once
#include <string>
#include <unordered_map>

namespace http_ns {

struct Request {
  std::string method;
  std::string path;
  std::string query;
  std::unordered_map<std::string,std::string> headers;
  std::string body;
};

struct Response {
  int status = 200;
  std::string status_text = "OK";
  std::unordered_map<std::string,std::string> headers;
  std::string body;
};

static inline std::string reason_phrase(int code) {
  switch(code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 500: return "Internal Server Error";
    default: return "OK";
  }
}

}
