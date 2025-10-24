#include "json_writer.hpp"
#include <fstream>

static std::string esc(const std::string& s){
  std::string o; o.reserve(s.size()+8);
  for(char c: s){
    switch(c){
      case '\"': o+="\\\""; break; case '\\': o+="\\\\"; break;
      case '\b': o+="\\b";  break; case '\f': o+="\\f";  break;
      case '\n': o+="\\n";  break; case '\r': o+="\\r";  break;
      case '\t': o+="\\t";  break;
      default: if((unsigned char)c<0x20){ char b[8]; snprintf(b,8,"\\u%04x",c); o+=b; } else o+=c;
    }
  } return o;
}

bool write_graph_json(const Graph& g, const std::string& title, const std::string& out){
  std::ofstream f(out, std::ios::trunc);
  if(!f) return false;
  f << "{\n  \"title\":\"" << esc(title) << "\",\n";
  f << "  \"meta\":{\"node_count\":" << g.nodes.size() << ",\"edge_count\":" << g.edges.size() << "},\n";
  f << "  \"nodes\":[\n";
  bool first=true;
  for(auto& kv: g.nodes){
    if(!first) f << ",\n"; first=false;
    f << "    {\"id\":\"" << esc(kv.first) << "\",\"kind\":\"" << esc(kv.second) << "\",\"group\":\"" << esc(kv.second) << "\"}";
  }
  f << "\n  ],\n  \"links\":[\n";
  for(size_t i=0;i<g.edges.size();++i){
    if(i) f << ",\n";
    f << "    {\"source\":\"" << esc(g.edges[i].first) << "\",\"target\":\"" << esc(g.edges[i].second) << "\"}";
  }
  f << "\n  ]\n}\n";
  return true;
}