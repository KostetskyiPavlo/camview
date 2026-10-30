#include "app/App.hpp"
#include "app/CliParser.hpp"
#include "util/Log.hpp"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    using camview::CliResult;

    const CliResult cli = camview::parseCommandLine(argc, argv);
    switch (cli.action) {
    case CliResult::Action::Help:
        std::fputs(camview::usageText(argv[0]).c_str(), stdout);
        return 0;
    case CliResult::Action::Version:
        std::printf("camview %s\n", CAMVIEW_VERSION);
        return 0;
    case CliResult::Action::Error:
        std::fprintf(stderr, "camview: %s\nTry '%s --help' for more information.\n",
                     cli.error.c_str(), argv[0]);
        return 2;
    case CliResult::Action::Run:
        break;
    }

    try {
        camview::log::info("camview ", CAMVIEW_VERSION);
        camview::App app(cli.config);
        return app.run();
    } catch (const std::exception& e) {
        camview::log::error("Fatal: ", e.what());
        return 1;
    }
}