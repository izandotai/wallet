// izan_watch — watch-only snapshot from the command line.
//
//   izan_watch 0xADDRESS [data-dir]
//
// Reads chains.json and tokens.json from data-dir (default "data"),
// then prints every native and token position for the address. The
// Ship-0 kernel: paste an address, see the assets.
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "core/units/decimal.hpp"
#include "domain/assets/portfolio.hpp"

namespace {

std::string slurp(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("cannot read " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: izan_watch 0xADDRESS [data-dir]\n";
        return 2;
    }
    const std::string dataDir = argc > 2 ? argv[2] : "data";

    try {
        auto chains = izan::chains::ChainRegistry::from_json(
            slurp(dataDir + "/chains.json"));
        auto tokens = izan::assets::TokenRegistry::from_json(
            slurp(dataDir + "/tokens.json"));
        izan::assets::PortfolioReader reader(
            std::move(chains), std::move(tokens));

        int failures = 0;
        for (const auto& h : reader.snapshot(argv[1])) {
            std::cout << h.chain << "\t" << h.symbol << "\t";
            if (h.ok) {
                std::cout << izan::units::format_units(h.amount, h.decimals);
            } else {
                std::cout << "unreadable: " << h.error;
                ++failures;
            }
            std::cout << "\n";
        }
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "izan_watch: " << e.what() << "\n";
        return 2;
    }
}
