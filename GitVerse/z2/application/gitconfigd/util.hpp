#pragma once
#include <string>
#include <vector>
#include <cctype>

namespace util_ns {

static inline int hex(char c) {
  if (c>='0'&&c<='9') return c-'0';
  if (c>='a'&&c<='f') return 10+(c-'a');
  if (c>='A'&&c<='F') return 10+(c-'A');
  return -1;
}

static inline std::string url_decode(const std::string& s) {
  std::string o; o.reserve(s.size());
  for (size_t i=0;i<s.size();++i) {
    char c=s[i];
    if (c=='+' ) { o.push_back(' '); }
    else if (c=='%' && i+2<s.size()) {
      int h1=hex(s[i+1]), h2=hex(s[i+2]);
      if (h1>=0&&h2>=0) { o.push_back(static_cast<char>(h1*16+h2)); i+=2; }
      else o.push_back(c);
    } else o.push_back(c);
  }
  return o;
}

static inline std::vector<std::pair<std::string,std::string>> parse_query(const std::string& q) {
  std::vector<std::pair<std::string,std::string>> out;
  size_t pos=0;
  while (pos<q.size()) {
    size_t amp = q.find('&', pos);
    if (amp==std::string::npos) amp=q.size();
    std::string pair = q.substr(pos, amp-pos);
    size_t eq = pair.find('=');
    if (eq==std::string::npos) out.push_back({url_decode(pair), ""});
    else out.push_back({url_decode(pair.substr(0,eq)), url_decode(pair.substr(eq+1))});
    pos = amp+1;
  }
  return out;
}

static inline std::string json_escape(const std::string& s) {
  std::string o; o.reserve(s.size()+8);
  for (char c: s) {
    switch(c) {
      case '\"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\b': o += "\\b"; break;
      case '\f': o += "\\f"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) { o += "\\u00"; const char* hexd="0123456789abcdef"; o.push_back(hexd[(c>>4)&0xF]); o.push_back(hexd[c&0xF]); }
        else o.push_back(c);
    }
  }
  return o;
}

}
