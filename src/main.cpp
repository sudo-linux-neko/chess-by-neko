// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#include "chess.hpp"
#include <csignal>
#include <cstring>
#include <iostream>
#include <cctype>

std::atomic<bool> stop_flag(false);

static void signal_handler(int) {
    stop_flag = true;
}

void print_usage(const char* prog) {
    std::cout << "NekoChess - A Chess Engine (Sunfish port)\n"
              << "Usage:\n"
              << "  " << prog << "                   Run in UCI mode (for chess GUIs)\n"
              << "  " << prog << " --selfplay [depth]  Self-play learning mode (default depth=8)\n"
              << "  " << prog << " --book <file>       Use Polyglot opening book\n"
              << "  " << prog << " --help              Show this help\n"
              << "\nControls:\n"
              << "  Ctrl+C to stop self-play gracefully\n"
              << "\nIn self-play mode, the engine plays against itself and evolves\n"
              << "its evaluation function. Progress is saved periodically.\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    // Parse args
    std::string book_path;
    bool selfplay = false;
    int selfplay_depth = 8;
    bool depth_was_set = false;

    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--book") {
            if (i + 1 < argc) book_path = argv[++i];
        } else if (arg == "--selfplay") {
            selfplay = true;
        } else if (selfplay && !depth_was_set && std::isdigit(arg[0])) {
            try {
                selfplay_depth = std::stoi(arg);
                depth_was_set = true;
            } catch (...) {
                std::cerr << "Warning: invalid depth '" << arg << "', using default 8.\n";
            }
        }
    }

    if (selfplay) {
        selfplay_depth = std::max(1, std::min(selfplay_depth, 30));

        if (!book_path.empty())
            std::cout << "Note: --book is not used in self-play mode.\n";

        selfplay_loop(selfplay_depth, stop_flag);

        if (stop_flag)
            std::cerr << "\nReceived interrupt. Shutting down gracefully..." << std::endl;

        return 0;
    }

    // Default: UCI mode
    uci_loop(book_path);
    return 0;
}
