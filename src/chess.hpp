// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <thread>
#include <atomic>
#include <random>
#include <fstream>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <cstring>

// Board geometry
constexpr int N = -10, E = 1, S = 10, W = -1;
constexpr int A1 = 91, H1 = 98, A8 = 21, H8 = 28;

// Piece values (centipawns)
constexpr int PIECE_VALS[6] = {100, 280, 320, 479, 929, 60000};

// Mate thresholds
constexpr int MATE_LOWER = 60000 - 10 * 929;
constexpr int MATE_UPPER = 60000 + 10 * 929;

// Search tuning
constexpr int EVAL_ROUGHNESS = 15;
constexpr int TT_SIZE_BITS = 20;
constexpr int TT_SIZE = 1 << TT_SIZE_BITS;

// Raw 8x8 PST data (must match Sunfish)
extern const int PST_RAW[6][64];

// Piece type index: P=0,N=1,B=2,R=3,Q=4,K=5
inline int pidx(char c) {
    switch (c) {
        case 'P': return 0; case 'N': return 1; case 'B': return 2;
        case 'R': return 3; case 'Q': return 4; case 'K': return 5;
        default: return -1;
    }
}
inline bool is_piece(char c) { return pidx(c) >= 0; }

struct Move {
    int from, to;
    char prom; // 0 = none, 'n','b','r','q' for promotions
    bool operator==(const Move& o) const {
        return from == o.from && to == o.to && prom == o.prom;
    }
    bool operator!=(const Move& o) const { return !(*this == o); }
};

struct Position {
    std::string board;  // 120-char mailbox
    int score;
    bool wc[2]; // [queenside, kingside]
    bool bc[2]; // opponent's castling rights
    int ep;     // en passant target square, 0 = none
    int kp;     // king passant square, 0 = none

    Position() : board(120, ' '), score(0), ep(0), kp(0) {
        wc[0] = wc[1] = true;
        bc[0] = bc[1] = true;
    }

    void gen_moves(std::vector<Move>& moves) const;
    Position moved(Move m) const;
    Position rotated(bool nullmove = false) const;
    int move_value(Move m) const;
    uint64_t hash() const;
};

// Transposition table
struct TTEntry {
    uint64_t hash;
    int lower, upper;
    int depth;
    Move move;
    bool has_move;
};

class TT {
    std::vector<TTEntry> entries;
public:
    TT() : entries(TT_SIZE) {}
    void probe(uint64_t h, int d, bool& found, int& lower, int& upper, Move& move, bool& has_move) const;
    void store(uint64_t h, int d, int lower, int upper, Move move, bool has_move);
    void clear();
};

// The PST table (mutable for learning)
extern int PST[6][120];

struct Searcher {
    TT tt;
    std::unordered_map<uint64_t, Move> best_moves;
    int nodes;
    void clear();
    int bound(const Position& pos, int gamma, int depth, bool can_null, bool root,
              const std::unordered_set<uint64_t>& history);
    void search(const Position& root_pos, const std::vector<Position>& game_history,
                int max_depth, int time_limit_ms,
                std::function<bool(int, int, int, Move)> callback);
    Move get_best_move(const Position& root_pos, const std::vector<Position>& game_history, int search_depth);
    Move search_parallel(const Position& root_pos, const std::vector<Position>& game_history,
                         int max_depth, int time_limit_ms, int num_threads);
};

// ─── Polyglot Opening Book ──────────────────────────────────────────────────
class PolyglotBook {
    int fd;
    int count;
    std::string path;
public:
    PolyglotBook() : fd(-1), count(0) {}
    ~PolyglotBook() { close(); }
    bool open(const std::string& filepath);
    void close();
    bool is_open() const { return fd >= 0; }
    Move probe(const std::string& fen) const;
};

// ─── Time Management ────────────────────────────────────────────────────────
struct TimeManager {
    int remaining;   // ms for this side
    int increment;   // ms per move
    int movestogo;   // estimated moves until time control

    void set(int wtime, int btime, int winc, int binc, int ply, int mtg = 40);
    int think_time() const;

    TimeManager() : remaining(300000), increment(0), movestogo(40) {}
};

// ─── UCI / engine functions ─────────────────────────────────────────────────
void uci_loop(const std::string& book_path = "");
void selfplay_loop(int search_depth, std::atomic<bool>& stop);

// ─── Init & helpers ─────────────────────────────────────────────────────────
void init_pst();
void init_zobrist();
void copy_pst(int dst[6][120], const int src[6][120]);
int parse_sq(const std::string& c);
std::string render_sq(int sq);
std::string board_to_fen(const std::string& board, bool wc0, bool wc1, bool bc0, bool bc1, int ep);
