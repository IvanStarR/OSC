#pragma once
#include <string>
struct Config {
    std::string repoids;
    std::string archs = "x86_64,noarch";
    std::string outdir = "web/static";
    std::string cachedir = "cache";
    int threads = 0;
};
