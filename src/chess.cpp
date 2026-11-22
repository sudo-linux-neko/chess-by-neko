// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#include "chess.hpp"
#include <cctype>
#include <cstring>
#include <cassert>
#include <random>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <unistd.h>
#include <fcntl.h>

// ─── PST Raw Data (from Sunfish) ────────────────────────────────────────────
const int PST_RAW[6][64] = {
    // P
    {   0,   0,   0,   0,   0,   0,   0,   0,
       78,  83,  86,  73, 102,  82,  85,  90,
        7,  29,  21,  44,  40,  31,  44,   7,
      -17,  16,  -2,  15,  14,   0,  15, -13,
      -26,   3,  10,   9,   6,   1,   0, -23,
      -22,   9,   5, -11, -10,  -2,   3, -19,
      -31,   8,  -7, -37, -36, -14,   3, -31,
        0,   0,   0,   0,   0,   0,   0,   0 },
    // N
    { -66, -53, -75, -75, -10, -55, -58, -70,
       -3,  -6, 100, -36,   4,  62,  -4, -14,
       10,  67,   1,  74,  73,  27,  62,  -2,
       24,  24,  45,  37,  33,  41,  25,  17,
       -1,   5,  31,  21,  22,  35,   2,   0,
      -18,  10,  13,  22,  18,  15,  11, -14,
      -23, -15,   2,   0,   2,   0, -23, -20,
      -74, -23, -26, -24, -19, -35, -22, -69 },
    // B
    { -59, -78, -82, -76, -23,-107, -37, -50,
      -11,  20,  35, -42, -39,  31,   2, -22,
       -9,  39, -32,  41,  52, -10,  28, -14,
       25,  17,  20,  34,  26,  25,  15,  10,
       13,  10,  17,  23,  17,  16,   0,   7,
       14,  25,  24,  15,   8,  25,  20,  15,
       19,  20,  11,   6,   7,   6,  20,  16,
       -7,   2, -15, -12, -14, -15, -10, -10 },
    // R
    {  35,  29,  33,   4,  37,  33,  56,  50,
       55,  29,  56,  67,  55,  62,  34,  60,
       19,  35,  28,  33,  45,  27,  25,  15,
        0,   5,  16,  13,  18,  -4,  -9,  -6,
      -28, -35, -16, -21, -13, -29, -46, -30,
      -42, -28, -42, -25, -25, -35, -26, -46,
      -53, -38, -31, -26, -29, -43, -44, -53,
      -30, -24, -18,   5,  -2, -18, -31, -32 },
    // Q
    {   6,   1,  -8,-104,  69,  24,  88,  26,
       14,  32,  60, -10,  20,  76,  57,  24,
       -2,  43,  32,  60,  72,  63,  43,   2,
        1, -16,  22,  17,  25,  20, -13,  -6,
      -14, -15,  -2,  -5,  -1, -10, -20, -22,
      -30,  -6, -13, -11, -16, -11, -16, -27,
      -36, -18,   0, -19, -15, -15, -21, -38,
      -39, -30, -31, -13, -31, -36, -34, -42 },
    // K
    {   4,  54,  47, -99, -99,  60,  83, -62,
      -32,  10,  55,  56,  56,  55,  10,   3,
      -62,  12, -57,  44, -67,  28,  37, -31,
      -55,  50,  11,  -4, -19,  13,   0, -49,
      -55, -43, -52, -28, -51, -47,  -8, -50,
      -47, -42, -43, -79, -64, -32, -29, -32,
       -4,   3, -14, -50, -57, -18,  13,   4,
       17,  30,  -3, -14,   6,  -1,  40,  18 }
};

// ─── Mutable PST Table ──────────────────────────────────────────────────────
int PST[6][120];

void init_pst() {
    for (int p = 0; p < 6; p++) {
        for (int sq = 0; sq < 120; sq++) PST[p][sq] = 0;
        for (int row = 0; row < 8; row++) {
            int board_start = (row + 2) * 10;
            for (int col = 0; col < 8; col++) {
                int sq = board_start + 1 + col;
                PST[p][sq] = PST_RAW[p][row * 8 + col] + PIECE_VALS[p];
            }
        }
    }
}

void copy_pst(int dst[6][120], const int src[6][120]) {
    std::memcpy(dst, src, sizeof(int) * 6 * 120);
}

// ─── Zobrist Hashing ────────────────────────────────────────────────────────
// [piece_idx][square] + castling[4] + ep[120] + kp[120]
uint64_t ZOBRIST_PIECES[6][120];
uint64_t ZOBRIST_CASTLING[4];
uint64_t ZOBRIST_EP[120];
uint64_t ZOBRIST_KP[120];
bool zobrist_initialized = false;

void init_zobrist() {
    if (zobrist_initialized) return;
    std::mt19937_64 rng(12345678);
    for (int p = 0; p < 6; p++)
        for (int sq = 0; sq < 120; sq++)
            ZOBRIST_PIECES[p][sq] = rng();
    for (int i = 0; i < 4; i++)
        ZOBRIST_CASTLING[i] = rng();
    for (int i = 0; i < 120; i++) {
        ZOBRIST_EP[i] = rng();
        ZOBRIST_KP[i] = rng();
    }
    zobrist_initialized = true;
}

uint64_t Position::hash() const {
    uint64_t h = 0;
    for (int i = 0; i < 120; i++) {
        char c = board[i];
        int pi = -1;
        if (isupper(c)) pi = pidx(c);
        else if (islower(c)) pi = pidx(toupper(c)) + 6; // use different key for black pieces
        else continue;
        if (pi >= 0) {
            // We need 12 piece types in the Zobrist, but we only have 6 PST tables.
            // Use a simple trick: xor with a secondary key for black pieces.
            h ^= ZOBRIST_PIECES[pi % 6][i];
            if (pi >= 6) h ^= 0xFFULL << 56; // differentiate black from white
        }
    }
    if (wc[0]) h ^= ZOBRIST_CASTLING[0];
    if (wc[1]) h ^= ZOBRIST_CASTLING[1];
    if (bc[0]) h ^= ZOBRIST_CASTLING[2];
    if (bc[1]) h ^= ZOBRIST_CASTLING[3];
    if (ep)  h ^= ZOBRIST_EP[ep];
    if (kp)  h ^= ZOBRIST_KP[kp];
    return h;
}

// ─── Polyglot Zobrist Keys ───────────────────────────────────────────────────
// Standard Polyglot Zobrist constants (12×64 + 4 + 8 + 1 = 781 keys)
// Generated by the same RNG used in Polyglot by Fabien Letouzey
static uint64_t PG_PIECE[12][64];
static uint64_t PG_CASTLE[4];
static uint64_t PG_EP[8];
static uint64_t PG_SIDE;
static bool pg_init_done = false;

static uint64_t pg_rand64(uint64_t& seed) {
    seed = seed * 0x9A795F9B7A1C53ULL + 1;
    return seed;
}

void init_polyglot_zobrist() {
    if (pg_init_done) return;
    uint64_t seed = 0;
    for (int i = 0; i < 12; i++)
        for (int sq = 0; sq < 64; sq++)
            PG_PIECE[i][sq] = pg_rand64(seed);
    for (int i = 0; i < 4; i++)
        PG_CASTLE[i] = pg_rand64(seed);
    for (int i = 0; i < 8; i++)
        PG_EP[i] = pg_rand64(seed);
    PG_SIDE = pg_rand64(seed);
    pg_init_done = true;
}

// ─── Standard Board (for Polyglot FEN) ──────────────────────────────────────
// Standard chess position representation (a1=0, h1=7, a8=56, h8=63)
// lowercase = black, uppercase = white
struct StdBoard {
    char board[64];
    int castle[4];  // K, Q, k, q
    int ep;         // -1 or target square 0-63
    bool white;     // true=white to move

    void clear() {
        static const char* init =
            "rnbqkbnr"
            "pppppppp"
            "........"
            "........"
            "........"
            "........"
            "PPPPPPPP"
            "RNBQKBNR";
        std::memcpy(board, init, 64);
        castle[0] = castle[1] = castle[2] = castle[3] = 1;
        ep = -1;
        white = true;
    }

    std::string fen() const {
        std::string f;
        for (int r = 7; r >= 0; r--) {
            int empty = 0;
            for (int f2 = 0; f2 < 8; f2++) {
                char c = board[r * 8 + f2];
                if (c == '.') { empty++; continue; }
                if (empty) { f += std::to_string(empty); empty = 0; }
                f += c;
            }
            if (empty) f += std::to_string(empty);
            if (r > 0) f += '/';
        }
        f += white ? " w " : " b ";
        std::string cast;
        if (castle[0]) cast += 'K';
        if (castle[1]) cast += 'Q';
        if (castle[2]) cast += 'k';
        if (castle[3]) cast += 'q';
        f += cast.empty() ? "-" : cast;
        f += ' ';
        if (ep >= 0 && ep < 64)
            f += std::string(1, 'a' + (ep % 8)) + std::string(1, '1' + (ep / 8));
        else
            f += '-';
        f += " 0 1";
        return f;
    }

    int sq_idx(const std::string& sq) const {
        return (sq[1] - '1') * 8 + (sq[0] - 'a');
    }

    void apply_uci_move(const std::string& uci) {
        int from = sq_idx(uci.substr(0, 2));
        int to = sq_idx(uci.substr(2, 2));
        char prom = (uci.size() >= 5) ? (char)toupper(uci[4]) : 0;
        char piece = board[from];
        int from_rank = from / 8;
        int to_rank = to / 8;

        // Save ep square before clearing
        int prev_ep = ep;
        ep = -1;

        // Clear castling rights
        // castle[0]=K(wK), [1]=Q(wQ), [2]=k(bK), [3]=q(bQ)
        if (piece == 'K') { castle[0] = castle[1] = 0; }
        if (piece == 'k') { castle[2] = castle[3] = 0; }
        if (from == 0 || to == 0) castle[1] = 0;   // a1 rook → lose Q-side
        if (from == 7 || to == 7) castle[0] = 0;   // h1 rook → lose K-side
        if (from == 56 || to == 56) castle[3] = 0; // a8 rook → lose q-side
        if (from == 63 || to == 63) castle[2] = 0; // h8 rook → lose k-side

        // Execute move
        board[to] = piece;
        board[from] = '.';

        // En passant capture
        if (piece == 'P' && to == prev_ep && prev_ep >= 0) {
            board[to - 8] = '.';
        }
        if (piece == 'p' && to == prev_ep && prev_ep >= 0) {
            board[to + 8] = '.';
        }

        // Double pawn push → set ep
        if (piece == 'P' && from_rank == 1 && to_rank == 3)
            ep = from + 8;
        if (piece == 'p' && from_rank == 6 && to_rank == 4)
            ep = from - 8;

        // Castling
        if (piece == 'K') {
            if (to - from == 2) { board[to - 1] = 'R'; board[to + 1] = '.'; }
            if (to - from == -2) { board[to + 1] = 'R'; board[to - 2] = '.'; }
        }
        if (piece == 'k') {
            if (to - from == -2) { board[to + 1] = 'r'; board[to - 2] = '.'; }
            if (to - from == 2) { board[to - 1] = 'r'; board[to + 1] = '.'; }
        }

        // Promotion
        if (prom) {
            board[to] = white ? prom : (char)tolower(prom);
        }

        white = !white;
    }

    void apply_sunfish_move(const std::string& uci) {
        // UCI move from sunfish coordinates — just reverse black's perspective
        // (handled externally; use apply_uci_move for standard UCI)
        apply_uci_move(uci);
    }
};

// Polyglot hash from FEN string
uint64_t polyglot_hash_from_fen(const std::string& fen) {
    uint64_t h = 0;
    // Parse FEN
    std::istringstream ss(fen);
    std::string board_part, side, castling, ep_str;
    ss >> board_part >> side >> castling >> ep_str;

    // Board piece squares: rank 8 (top) to rank 1 (bottom)
    int sq = 56;
    for (char c : board_part) {
        if (c == '/') { sq -= 16; continue; }
        if (isdigit(c)) { sq += (c - '0'); continue; }
        int piece_type;
        bool is_white = isupper(c);
        char p = toupper(c);
        switch (p) {
            case 'P': piece_type = 0; break;
            case 'N': piece_type = 1; break;
            case 'B': piece_type = 2; break;
            case 'R': piece_type = 3; break;
            case 'Q': piece_type = 4; break;
            case 'K': piece_type = 5; break;
            default: continue;
        }
        if (!is_white) piece_type += 6;
        h ^= PG_PIECE[piece_type][sq];
        sq++;
    }

    // Castling
    for (char c : castling) {
        switch (c) {
            case 'K': h ^= PG_CASTLE[0]; break;
            case 'Q': h ^= PG_CASTLE[1]; break;
            case 'k': h ^= PG_CASTLE[2]; break;
            case 'q': h ^= PG_CASTLE[3]; break;
        }
    }

    // En passant
    if (ep_str.size() >= 2 && ep_str != "-") {
        int ep_file = ep_str[0] - 'a';
        if (ep_file >= 0 && ep_file < 8)
            h ^= PG_EP[ep_file];
    }

    // Side to move
    if (side == "b")
        h ^= PG_SIDE;

    return h;
}

// Decode Polyglot move (uint16_t) to UCI string
static std::string polyglot_move_to_uci(uint16_t pmove) {
    int from = (pmove >> 6) & 0x3F;
    int to = pmove & 0x3F;
    int prom = (pmove >> 12) & 0x7;
    char prom_char = 0;
    if (prom == 1) prom_char = 'n';
    else if (prom == 2) prom_char = 'b';
    else if (prom == 3) prom_char = 'r';
    else if (prom == 4) prom_char = 'q';
    std::string uci;
    uci += 'a' + (from % 8);
    uci += '1' + (from / 8);
    uci += 'a' + (to % 8);
    uci += '1' + (to / 8);
    if (prom_char) uci += prom_char;
    return uci;
}

// ─── Initial Board ──────────────────────────────────────────────────────────
static const std::string INITIAL_BOARD =
    "         \n"
    "         \n"
    " rnbqkbnr\n"
    " pppppppp\n"
    " ........\n"
    " ........\n"
    " ........\n"
    " ........\n"
    " PPPPPPPP\n"
    " RNBQKBNR\n"
    "         \n"
    "         \n";

static Position initial_position() {
    Position pos;
    pos.board = INITIAL_BOARD;
    pos.score = 0;
    pos.wc[0] = pos.wc[1] = true;
    pos.bc[0] = pos.bc[1] = true;
    pos.ep = 0;
    pos.kp = 0;
    return pos;
}

// ─── UCI Helpers ────────────────────────────────────────────────────────────
int parse_sq(const std::string& c) {
    int fil = c[0] - 'a';
    int rank = c[1] - '1';
    return A1 + fil - 10 * rank;
}

std::string render_sq(int sq) {
    int board_row = sq / 10;
    int file = (sq % 10) - 1;
    int rank = 9 - board_row;
    std::string s;
    s += (char)('a' + file);
    s += (char)('1' + rank);
    return s;
}

// ─── FEN (simplified) ───────────────────────────────────────────────────────
std::string board_to_fen(const std::string& board, bool wc0, bool wc1, bool bc0, bool bc1, int ep) {
    std::string fen;
    for (int row = 2; row <= 9; row++) {
        int empty = 0;
        for (int col = 1; col <= 8; col++) {
            char c = board[row * 10 + col];
            if (c == '.') { empty++; continue; }
            if (empty > 0) { fen += std::to_string(empty); empty = 0; }
            fen += c;
        }
        if (empty > 0) fen += std::to_string(empty);
        if (row < 9) fen += '/';
    }
    std::string castling;
    if (wc0) castling += 'K'; // white kingside (east)
    if (wc1) castling += 'Q'; // white queenside (west)
    if (bc0) castling += 'k';
    if (bc1) castling += 'q';
    if (castling.empty()) castling = "-";
    std::string ep_str = (ep ? render_sq(ep) : "-");
    return fen + " w " + castling + " " + ep_str + " 0 1";
}

// ─── Position: Move Generation ──────────────────────────────────────────────
void Position::gen_moves(std::vector<Move>& moves) const {
    moves.clear();
    for (int i = 0; i < 120; i++) {
        char p = board[i];
        if (!isupper((unsigned char)p)) continue;

        // Direction rays per piece
        switch (p) {
        case 'P': {
            // N, N+N, N+W, N+E
            int dd[] = {N, N+N, N+W, N+E};
            for (int di = 0; di < 4; di++) {
                int d = dd[di];
                int j = i + d;
                char q = board[j];
                if (isspace((unsigned char)q) || isupper((unsigned char)q)) continue;
                if (d == N || d == N+N) { if (q != '.') continue; }
                if (d == N+N) { if (i < A1 + N || board[i+N] != '.') continue; }
                if ((d == N+W || d == N+E) && q == '.') {
                    if (j != ep && (kp == 0 || (j != kp && j != kp-1 && j != kp+1)))
                        continue;
                }
                if (A8 <= j && j <= H8) {
                    moves.push_back({i, j, 'n'});
                    moves.push_back({i, j, 'b'});
                    moves.push_back({i, j, 'r'});
                    moves.push_back({i, j, 'q'});
                } else {
                    moves.push_back({i, j, 0});
                }
            }
            break;
        }
        case 'N': {
            int dd[] = {N+N+E, E+N+E, E+S+E, S+S+E, S+S+W, W+S+W, W+N+W, N+N+W};
            for (int di = 0; di < 8; di++) {
                int j = i + dd[di];
                char q = board[j];
                if (!isspace((unsigned char)q) && !isupper((unsigned char)q))
                    moves.push_back({i, j, 0});
            }
            break;
        }
        case 'B': {
            int dd[] = {N+E, S+E, S+W, N+W};
            for (int di = 0; di < 4; di++) {
                for (int j = i + dd[di]; ; j += dd[di]) {
                    char q = board[j];
                    if (isspace((unsigned char)q) || isupper((unsigned char)q)) break;
                    moves.push_back({i, j, 0});
                    if (islower((unsigned char)q)) break;
                }
            }
            break;
        }
        case 'R': {
            int dd[] = {N, E, S, W};
            for (int di = 0; di < 4; di++) {
                int d = dd[di];
                for (int j = i + d; ; j += d) {
                    char q = board[j];
                    if (isspace((unsigned char)q) || isupper((unsigned char)q)) break;
                    moves.push_back({i, j, 0});
                    if (islower((unsigned char)q)) break;
                    // Castling: rook sliding next to king
                    if (i == A1 && board[j+E] == 'K' && wc[0]) {
                        moves.push_back({j+E, j+W, 0});
                    }
                    if (i == H1 && board[j+W] == 'K' && wc[1]) {
                        moves.push_back({j+W, j+E, 0});
                    }
                }
            }
            break;
        }
        case 'Q': {
            int dd[] = {N, E, S, W, N+E, S+E, S+W, N+W};
            for (int di = 0; di < 8; di++) {
                for (int j = i + dd[di]; ; j += dd[di]) {
                    char q = board[j];
                    if (isspace((unsigned char)q) || isupper((unsigned char)q)) break;
                    moves.push_back({i, j, 0});
                    if (islower((unsigned char)q)) break;
                }
            }
            break;
        }
        case 'K': {
            int dd[] = {N, E, S, W, N+E, S+E, S+W, N+W};
            for (int di = 0; di < 8; di++) {
                int j = i + dd[di];
                char q = board[j];
                if (!isspace((unsigned char)q) && !isupper((unsigned char)q))
                    moves.push_back({i, j, 0});
            }
            break;
        }
        }
    }
}

// ─── Position: Rotate ───────────────────────────────────────────────────────
Position Position::rotated(bool nullmove) const {
    Position pos;
    pos.board = board;
    std::reverse(pos.board.begin(), pos.board.end());
    for (char& c : pos.board) {
        if (isupper((unsigned char)c)) c = (char)tolower((unsigned char)c);
        else if (islower((unsigned char)c)) c = (char)toupper((unsigned char)c);
    }
    pos.score = -score;
    pos.wc[0] = bc[0]; pos.wc[1] = bc[1];
    pos.bc[0] = wc[0]; pos.bc[1] = wc[1];
    pos.ep = (ep && !nullmove) ? (119 - ep) : 0;
    pos.kp = (kp && !nullmove) ? (119 - kp) : 0;
    return pos;
}

// ─── Position: Move Value ───────────────────────────────────────────────────
int Position::move_value(Move m) const {
    int i = m.from, j = m.to;
    char p = board[i];
    char q = board[j];
    int pi = pidx(p);
    int score = PST[pi][j] - PST[pi][i];

    // Capture
    if (islower((unsigned char)q)) {
        int qi = pidx((char)toupper((unsigned char)q));
        score += PST[qi][119 - j];
    }

    // King capture detection (castling through check)
    if (kp && std::abs(j - kp) < 2) {
        score += PST[pidx('K')][119 - j];
    }

    // Castling
    if (p == 'K' && std::abs(i - j) == 2) {
        int rook_dst = (i + j) / 2;
        score += PST[pidx('R')][rook_dst];
        score -= PST[pidx('R')][(j < i) ? A1 : H1];
    }

    // Pawn promotion and en passant
    if (p == 'P') {
        if (A8 <= j && j <= H8) {
            score += PST[pidx((char)toupper((unsigned char)m.prom))][j] - PST[pidx('P')][j];
        }
        if (j == ep) {
            score += PST[pidx('P')][119 - (j + S)];
        }
    }

    return score;
}

// ─── Position: Make Move ────────────────────────────────────────────────────
Position Position::moved(Move m) const {
    int i = m.from, j = m.to;
    char p = board[i];

    Position pos = *this;
    pos.ep = 0;
    pos.kp = 0;
    pos.score += move_value(m);

    // Move piece
    pos.board[j] = p;
    pos.board[i] = '.';

    // Castling rights
    if (i == A1) pos.wc[0] = false;
    if (i == H1) pos.wc[1] = false;
    if (j == A8) pos.bc[0] = false;
    if (j == H8) pos.bc[1] = false;

    // Castling
    if (p == 'K') {
        pos.wc[0] = pos.wc[1] = false;
        if (std::abs(j - i) == 2) {
            pos.kp = (i + j) / 2;
            pos.board[(j < i) ? A1 : H1] = '.';
            pos.board[pos.kp] = 'R';
        }
    }

    // Pawn promotion, double move, en passant
    if (p == 'P') {
        if (A8 <= j && j <= H8) {
            pos.board[j] = (char)toupper((unsigned char)m.prom);
        }
        if (j - i == 2 * N) {
            pos.ep = i + N;
        }
        if (j == ep) {
            pos.board[j + S] = '.';
        }
    }

    return pos.rotated();
}

// ─── Transposition Table ────────────────────────────────────────────────────
void TT::probe(uint64_t h, int d, bool& found, int& lower, int& upper, Move& move, bool& has_move) const {
    unsigned idx = h & (TT_SIZE - 1);
    const auto& e = entries[idx];
    if (e.hash == h) {
        found = true;
        lower = e.lower;
        upper = e.upper;
        if (e.has_move) {
            move = e.move;
            has_move = true;
        } else {
            has_move = false;
        }
    } else {
        found = false;
        has_move = false;
    }
}

void TT::store(uint64_t h, int d, int lower_, int upper_, Move move_, bool has_move_) {
    unsigned idx = h & (TT_SIZE - 1);
    auto& e = entries[idx];
    // Always replace (depth-preferred would be better, but simple is fine)
    e.hash = h;
    e.lower = lower_;
    e.upper = upper_;
    e.depth = d;
    if (has_move_) {
        e.move = move_;
        e.has_move = true;
    } else {
        e.has_move = false;
    }
}

void TT::clear() {
    for (auto& e : entries) {
        e.hash = 0;
        e.lower = -MATE_UPPER;
        e.upper = MATE_UPPER;
        e.depth = 0;
        e.has_move = false;
    }
}

// ─── Searcher Methods ────────────────────────────────────────────────────────
void Searcher::clear() {
        tt.clear();
        best_moves.clear();
        nodes = 0;
    }

    int Searcher::bound(const Position& pos, int gamma, int depth, bool can_null, bool root,
              const std::unordered_set<uint64_t>& history) {
        nodes++;
        depth = std::max(depth, 0);

        if (pos.score <= -MATE_LOWER) return -MATE_UPPER;

        uint64_t h = pos.hash();
        bool found; int lower = -MATE_UPPER; int upper = MATE_UPPER; Move tmove{0,0,0}; bool has_ttmove;
        tt.probe(h, depth, found, lower, upper, tmove, has_ttmove);
        if (found) {
            if (lower >= gamma) return lower;
            if (upper < gamma) return upper;
        }

        if (can_null && depth > 0 && history.count(h)) return 0;

        int best = -MATE_UPPER;
        Move best_move{0,0,0};

        // Null-move pruning
        if (depth > 2 && can_null && std::abs(pos.score) < 500) {
            Position null_pos = pos.rotated(true);
            int null_score = -bound(null_pos, 1 - gamma, depth - 3, false, false, history);
            if (null_score > best) best = null_score;
            if (best >= gamma) {
                tt.store(h, depth, best, upper, Move{0,0,0}, false);
                return best;
            }
        }

        // Stand-pat for QS
        if (depth == 0) {
            if (pos.score > best) best = pos.score;
            if (best >= gamma) {
                tt.store(h, depth, best, upper, Move{0,0,0}, false);
                return best;
            }
        }

        // Generate and score moves (local buffers for thread/recursion safety)
        std::vector<Move> moves;
        pos.gen_moves(moves);
        std::vector<std::pair<int, Move>> scored;
        for (auto& m : moves) {
            int val = pos.move_value(m);
            if (depth == 0 && val < 40) continue;
            scored.emplace_back(val, m);
        }
        std::sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        // Hash move first
        int start_idx = 0;
        if (has_ttmove && (depth > 0 || pos.move_value(tmove) >= 40)) {
            start_idx = 1;
            Position child = pos.moved(tmove);
            int score = -bound(child, 1 - gamma, depth - 1, true, false, history);
            if (score > best) { best = score; best_move = tmove; }
            if (best >= gamma) {
                tt.store(h, depth, best, upper, best_move, true);
                best_moves[h] = best_move;
                return best;
            }
        }

        // Search remaining moves
        for (int idx = start_idx; idx < (int)scored.size(); idx++) {
            auto& sm = scored[idx];
            Move m = sm.second;
            int val = sm.first;

            if (has_ttmove && m == tmove) continue;

            // Futility pruning
            if (depth <= 1 && pos.score + val < gamma && val < MATE_LOWER) {
                int score = pos.score + val;
                if (score > best) best = score;
                break;
            }

            Position child = pos.moved(m);
            int score = -bound(child, 1 - gamma, depth - 1, true, false, history);
            if (score > best) { best = score; best_move = m; }
            if (best >= gamma) break;
        }

        // Stalemate
        if (depth > 2 && best == -MATE_UPPER) {
            Position flipped = pos.rotated(true);
            bool in_check = (bound(flipped, MATE_UPPER, 0, false, false, history) == MATE_UPPER);
            best = in_check ? -MATE_LOWER : 0;
        }

        // Update TT
        if (best >= gamma) {
            tt.store(h, depth, best, upper, best_move, (best_move.from || best_move.to));
            if (best_move.from || best_move.to) best_moves[h] = best_move;
        } else {
            tt.store(h, depth, lower, best, Move{0,0,0}, false);
        }

        return best;
    }

    void Searcher::search(const Position& root_pos, const std::vector<Position>& game_history,
                int max_depth, int time_limit_ms,
                std::function<bool(int, int, int, Move)> callback) {
        clear();
        std::unordered_set<uint64_t> history;
        for (auto& p : game_history) history.insert(p.hash());
        history.insert(root_pos.hash());

        int gamma = 0;
        auto start = std::chrono::steady_clock::now();

        for (int d = 1; d <= max_depth; d++) {
            int lower = -MATE_UPPER;
            int upper = MATE_UPPER;
            int last_score = 0;

            while (lower < upper - EVAL_ROUGHNESS) {
                int score = bound(root_pos, gamma, d, false, true, history);
                if (score >= gamma) { lower = score; }
                if (score < gamma) { upper = score; }
                gamma = (lower + upper + 1) / 2;
                last_score = score;
            }

            Move best{0,0,0};
            auto it = best_moves.find(root_pos.hash());
            if (it != best_moves.end()) best = it->second;

            if (!callback(d, last_score, gamma, best))
                return;

            auto now = std::chrono::steady_clock::now();
            int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (time_limit_ms > 0 && elapsed >= time_limit_ms)
                return;
        }
    }

    Move Searcher::get_best_move(const Position& root_pos, const std::vector<Position>& game_history, int search_depth) {
        Move best_move{0,0,0};
        search(root_pos, game_history, search_depth, 0,
               [&](int d, int score, int gamma, Move move) {
                   if (move.from || move.to) best_move = move;
                   return true;
               });
        return best_move;
    }

    // ─── Lazy SMP: parallel search helper ──────────────────────────────────────
    struct SMPWorker {
        const Position* root_pos;
        const std::vector<Position>* game_history;
        int max_depth;
        int time_limit_ms;
        int thread_id;
        Move best;
        bool done;
    };

    static void smp_worker(SMPWorker* w) {
        w->done = false;

        std::vector<Position> history_copy = *w->game_history;

        Searcher local;
        // Each thread has its own empty TT — avoids expensive ~40MB copy
        // and races on TT entries

        // Use a different aspiration window per thread
        int gamma = w->thread_id * 10;

        std::unordered_set<uint64_t> hist;
        for (auto& p : history_copy) hist.insert(p.hash());
        hist.insert(w->root_pos->hash());

        auto start = std::chrono::steady_clock::now();
        Move best_move{0,0,0};

        for (int d = 1; d <= w->max_depth; d++) {
            int lower = -MATE_UPPER;
            int upper = MATE_UPPER;

            while (lower < upper - EVAL_ROUGHNESS) {
                int score = local.bound(*w->root_pos, gamma, d, false, true, hist);
                if (score >= gamma) lower = score;
                if (score < gamma) upper = score;
                gamma = (lower + upper + 1) / 2;
            }

            auto it = local.best_moves.find(w->root_pos->hash());
            if (it != local.best_moves.end() && (it->second.from || it->second.to))
                best_move = it->second;

            auto now = std::chrono::steady_clock::now();
            int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (w->time_limit_ms > 0 && elapsed >= w->time_limit_ms)
                break;
        }

        w->best = best_move;
        w->done = true;
    }

    Move Searcher::search_parallel(const Position& root_pos, const std::vector<Position>& game_history,
                                   int max_depth, int time_limit_ms, int num_threads) {
        if (num_threads <= 1)
            return get_best_move(root_pos, game_history, max_depth);

        std::vector<SMPWorker> workers(num_threads);
        std::vector<std::thread> threads;

        for (int t = 0; t < num_threads; t++) {
            workers[t] = {&root_pos, &game_history, max_depth, time_limit_ms, t, Move{0,0,0}, false};
            threads.emplace_back(smp_worker, &workers[t]);
        }

        for (auto& th : threads)
            th.join();

        // Pick the best result from any thread (first move from any completed thread)
        for (auto& w : workers) {
            if (w.done && (w.best.from || w.best.to))
                return w.best;
        }
        return Move{0,0,0};
    }

// Forward declarations
static Move fen_to_move(const std::string& uci);

// ─── Polyglot Opening Book ──────────────────────────────────────────────────
bool PolyglotBook::open(const std::string& filepath) {
    close();
    fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd < 0) return false;
    // Get file size
    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz < 16 || sz % 16 != 0) { close(); return false; }
    count = (int)(sz / 16);
    path = filepath;
    return true;
}

void PolyglotBook::close() {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
    count = 0;
    path.clear();
}

Move PolyglotBook::probe(const std::string& fen) const {
    if (fd < 0 || count == 0) return Move{0,0,0};
    uint64_t key = polyglot_hash_from_fen(fen);

    // Binary search in the book file
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint64_t mid_key;
        char buf[8];
        ssize_t r = pread(fd, buf, 8, mid * 16);
        if (r != 8) break;
        mid_key = (uint64_t)buf[0] | ((uint64_t)buf[1] << 8) | ((uint64_t)buf[2] << 16) |
                  ((uint64_t)buf[3] << 24) | ((uint64_t)buf[4] << 32) | ((uint64_t)buf[5] << 40) |
                  ((uint64_t)buf[6] << 48) | ((uint64_t)buf[7] << 56);

        if (key < mid_key) hi = mid - 1;
        else if (key > mid_key) lo = mid + 1;
        else {
            // Found a matching key — collect all matching entries for weighted random pick
            int start = mid, end = mid;
            while (start > 0) {
                char k[8];
                if (pread(fd, k, 8, (start - 1) * 16) != 8) break;
                uint64_t kk = (uint64_t)k[0] | ((uint64_t)k[1] << 8) | ((uint64_t)k[2] << 16) |
                              ((uint64_t)k[3] << 24) | ((uint64_t)k[4] << 32) | ((uint64_t)k[5] << 40) |
                              ((uint64_t)k[6] << 48) | ((uint64_t)k[7] << 56);
                if (kk == key) start--;
                else break;
            }
            while (end < count - 1) {
                char k[8];
                if (pread(fd, k, 8, (end + 1) * 16) != 8) break;
                uint64_t kk = (uint64_t)k[0] | ((uint64_t)k[1] << 8) | ((uint64_t)k[2] << 16) |
                              ((uint64_t)k[3] << 24) | ((uint64_t)k[4] << 32) | ((uint64_t)k[5] << 40) |
                              ((uint64_t)k[6] << 48) | ((uint64_t)k[7] << 56);
                if (kk == key) end++;
                else break;
            }

            // Weighted random selection
            int total_weight = 0;
            int weights[512];
            uint16_t moves[512];
            int n = end - start + 1;
            if (n > 512) n = 512;
            for (int i = 0; i < n; i++) {
                char buf2[10];
                if (pread(fd, buf2, 10, (start + i) * 16) != 10) break;
                uint16_t mv = (uint16_t)buf2[8] | ((uint16_t)buf2[9] << 8);
                // We ignore the move part here — read weights too
                char wbuf[4];
                if (pread(fd, wbuf, 4, (start + i) * 16 + 8) != 4) break;
                uint16_t w = (uint16_t)wbuf[2] | ((uint16_t)wbuf[3] << 8);
                weights[i] = w;
                moves[i] = mv;
                total_weight += w;
            }

            if (total_weight <= 0) return Move{0,0,0};

            // Pick random entry
            static thread_local std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
            int pick = std::uniform_int_distribution<int>(0, total_weight - 1)(rng);
            for (int i = 0; i < n; i++) {
                pick -= weights[i];
                if (pick < 0) {
                    std::string uci = polyglot_move_to_uci(moves[i]);
                    return fen_to_move(uci);
                }
            }
            return Move{0,0,0};
        }
    }
    return Move{0,0,0};
}

// ─── Time Manager ───────────────────────────────────────────────────────────
void TimeManager::set(int wtime, int btime, int winc, int binc, int ply, int mtg) {
    if (ply % 2 == 0) { // white to move
        remaining = wtime;
        increment = winc;
    } else {
        remaining = btime;
        increment = binc;
    }
    movestogo = mtg;
    // Safety
    if (remaining < 1) remaining = 1;
}

int TimeManager::think_time() const {
    int base = remaining / movestogo + increment;
    // Use at most 1/4 of remaining time, minimum 10ms
    int capped = std::min(base, remaining / 4);
    return std::max(capped, 10);
}

// ─── FEN → Move helper ──────────────────────────────────────────────────────
static Move fen_to_move(const std::string& uci) {
    if (uci.size() < 4) return Move{0,0,0};
    int from = parse_sq(uci.substr(0, 2));
    int to = parse_sq(uci.substr(2, 2));
    char prom = (uci.size() >= 5) ? (char)tolower(uci[4]) : 0;
    return Move{from, to, prom};
}

// ─── Sunfish Position → Standard FEN ───────────────────────────────────────
void uci_loop(const std::string& book_path) {
    init_pst();
    init_zobrist();
    init_polyglot_zobrist();

    std::vector<Position> history;
    history.push_back(initial_position());
    Searcher searcher;
    PolyglotBook book;
    if (!book_path.empty())
        book.open(book_path);
    TimeManager tm;
    StdBoard std_pos;
    std_pos.clear();

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "uci") {
            std::cout << "id name NekoChess 1.0" << std::endl;
            std::cout << "id author Neko (ported from Sunfish)" << std::endl;
            std::cout << "option name BookFile type string default <empty>" << std::endl;
            std::cout << "uciok" << std::endl;
        } else if (cmd == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (cmd == "setoption") {
            std::string name, value;
            iss >> name; // "name"
            iss >> name;
            if (name == "BookFile") {
                iss >> value; // "value"
                iss >> value;
                if (!value.empty() && value != "<empty>")
                    book.open(value);
            }
        } else if (cmd == "ucinewgame") {
            history.clear();
            history.push_back(initial_position());
            std_pos.clear();
            searcher.clear();
        } else if (cmd == "position") {
            std::string sub;
            iss >> sub;
            if (sub == "startpos") {
                history.clear();
                history.push_back(initial_position());
                std_pos.clear();
                iss >> sub; // "moves"
                if (sub == "moves") {
                    std::string move_str;
                    while (iss >> move_str) {
                        if (move_str.length() < 4) continue;
                        int i = parse_sq(move_str.substr(0, 2));
                        int j = parse_sq(move_str.substr(2, 2));
                        char prom = 0;
                        if (move_str.length() >= 5) prom = (char)tolower((unsigned char)move_str[4]);
                        int ply = (int)history.size() - 1;
                        if (ply % 2 == 1) {
                            i = 119 - i;
                            j = 119 - j;
                        }
                        Position next = history.back().moved({i, j, prom});
                        history.push_back(next);
                        std_pos.apply_uci_move(move_str);
                    }
                }
            } else if (sub == "fen") {
                std::string fen_str;
                std::getline(iss, fen_str);
                // Skip FEN parsing — just reset for now
                history.clear();
                history.push_back(initial_position());
                std_pos.clear();
            }
        } else if (cmd == "go") {
            std::string sub;
            int wtime = 0, btime = 0, winc = 0, binc = 0;
            int movestogo = 40;
            int depth = 100;

            while (iss >> sub) {
                if (sub == "wtime") iss >> wtime;
                else if (sub == "btime") iss >> btime;
                else if (sub == "winc") iss >> winc;
                else if (sub == "binc") iss >> binc;
                else if (sub == "movestogo") iss >> movestogo;
                else if (sub == "depth") iss >> depth;
            }

            // Try Polyglot book first
            if (book.is_open()) {
                std::string fen = std_pos.fen();
                Move book_move = book.probe(fen);
                if (book_move.from || book_move.to) {
                    int ii = book_move.from, jj = book_move.to;
                    int ply = (int)history.size() - 1;
                    if (ply % 2 == 1) {
                        ii = 119 - ii;
                        jj = 119 - jj;
                    }
                    std::string move_str = render_sq(ii) + render_sq(jj);
                    if (book_move.prom) move_str += book_move.prom;
                    std::cout << "bestmove " << move_str << std::endl;
                    continue;
                }
            }

            // Time management
            tm.set(wtime, btime, winc, binc, (int)history.size() - 1, movestogo);
            int think_time = (wtime > 0 || btime > 0) ? tm.think_time() : 5000;

            auto start = std::chrono::steady_clock::now();
            Move best_move{0,0,0};
            bool stopped = false;

            searcher.search(history.back(), history, depth, think_time,
                [&](int d, int score, int gamma, Move move) {
                    auto now = std::chrono::steady_clock::now();
                    int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

                    if (move.from || move.to) {
                        int i = move.from, j = move.to;
                        if (((int)history.size() - 1) % 2 == 1) {
                            i = 119 - i;
                            j = 119 - j;
                        }
                        std::string pv = render_sq(i) + render_sq(j);
                        if (move.prom) pv += move.prom;

                        std::cout << "info depth " << d << " score cp " << score
                                  << " time " << elapsed << " pv " << pv << std::endl;
                        best_move = move;
                    }

                    if (elapsed > think_time * 0.8 && (best_move.from || best_move.to)) {
                        stopped = true;
                        return false;
                    }
                    return true;
                });

            if (best_move.from || best_move.to) {
                int i = best_move.from, j = best_move.to;
                if (((int)history.size() - 1) % 2 == 1) {
                    i = 119 - i;
                    j = 119 - j;
                }
                std::string move_str = render_sq(i) + render_sq(j);
                if (best_move.prom) move_str += best_move.prom;
                std::cout << "bestmove " << move_str << std::endl;
            } else {
                std::cout << "bestmove (none)" << std::endl;
            }
        } else if (cmd == "quit") {
            break;
        }
    }
}

// ─── Self-Play Learning ─────────────────────────────────────────────────────
static int play_one_game(int pst_white[6][120], int pst_black[6][120],
                          int search_depth, std::mt19937& rng) {
    // Returns: 1 = white wins, -1 = black wins, 0 = draw/stalemate

    std::vector<Position> history;
    history.push_back(initial_position());
    Searcher searcher;
    Searcher check_searcher;

    int max_moves = 100; // enough for depth 3-8 games

    for (int move_count = 0; move_count < max_moves; move_count++) {
        // Set PST for current side
        if ((int)history.size() % 2 == 1) {
            // White to move
            std::memcpy(PST, pst_white, sizeof(int) * 6 * 120);
        } else {
            // Black to move (we use pst_black for when it's black's turn, which
            // from the engine's perspective is always the "current player")
            std::memcpy(PST, pst_black, sizeof(int) * 6 * 120);
        }

        Move best = searcher.get_best_move(history.back(), history, search_depth);

        if (best.from == 0 && best.to == 0) {
            // No legal moves
            Position flipped = history.back().rotated(true);
            // Check if in check
            std::unordered_set<uint64_t> empty_hist;
            int check_val = check_searcher.bound(flipped, MATE_UPPER, 0, false, false, empty_hist);
            if (check_val == MATE_UPPER) {
                // Checkmate
                return (history.size() % 2 == 1) ? -1 : 1;
            } else {
                // Stalemate
                return 0;
            }
        }

        Position next = history.back().moved(best);
        history.push_back(next);

        // Check for king capture (win condition)
        // After rotation, if the opponent has no king, we win
        std::string& bd = next.board;
        bool has_white_king = false;
        bool has_black_king = false;
        for (char c : bd) {
            if (c == 'K') has_white_king = true;
            if (c == 'k') has_black_king = true;
        }
        // After rotate, current player's view: uppercase = friendly
        if (!has_white_king) {
            // We lost our king
            return (history.size() % 2 == 1) ? -1 : 1;
        }
        if (!has_black_king) {
            return (history.size() % 2 == 1) ? 1 : -1;
        }
    }

    return 0; // draw by move limit
}

void mutate_pst(int pst[6][120], std::mt19937& rng) {
    std::normal_distribution<float> dist(0, 1.5f);
    std::uniform_int_distribution<int> which_piece(0, 5);
    std::uniform_int_distribution<int> which_sq(0, 119);

    for (int i = 0; i < 30; i++) {
        int p = which_piece(rng);
        int sq = which_sq(rng);
        // Don't mutate padding squares
        if (sq < 20 || sq >= 100) continue;
        if (sq % 10 == 0 || sq % 10 == 9) continue;
        int delta = (int)(dist(rng) * 5.0f);
        if (delta == 0) delta = (dist(rng) > 0) ? 1 : -1;
        pst[p][sq] += delta;
    }
}

static bool load_pst_bin(const char* path, int pst[6][120]) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.read((char*)pst, sizeof(int) * 6 * 120);
    return f.gcount() == sizeof(int) * 6 * 120;
}

void selfplay_loop(int search_depth, std::atomic<bool>& stop) {
    init_pst();
    init_zobrist();
    init_polyglot_zobrist();

    int champion_pst[6][120];
    int challenger_pst[6][120];

    // Try to resume from saved champion; otherwise start from defaults
    if (load_pst_bin("champion_pst.bin", champion_pst)) {
        std::cout << "Loaded previous champion PST from champion_pst.bin" << std::endl;
    } else {
        copy_pst(champion_pst, PST);
    }

    std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

    int generation = 0;
    int champion_wins = 0;
    int total_matches = 0;

    std::cout << "Self-play learning started (depth=" << search_depth << ")" << std::endl;
    std::cout << "Press Ctrl+C to stop and save progress." << std::endl;

    while (!stop) {
        generation++;

        // Create challenger by mutating champion
        copy_pst(challenger_pst, champion_pst);
        mutate_pst(challenger_pst, rng);

        // Play match: 2 games, swap colors
        // Game 1: champion white, challenger black
        int result1 = play_one_game(champion_pst, challenger_pst, search_depth, rng);
        if (stop) break;

        // Game 2: challenger white, champion black
        int result2 = play_one_game(challenger_pst, champion_pst, search_depth, rng);
        if (stop) break;

        total_matches++;

        // Score: win=2, draw=1, loss=0
        int champ_score = 0, chal_score = 0;
        if (result1 > 0) champ_score += 2;
        else if (result1 == 0) { champ_score += 1; chal_score += 1; }
        else chal_score += 2;

        if (result2 > 0) chal_score += 2;
        else if (result2 == 0) { champ_score += 1; chal_score += 1; }
        else champ_score += 2;

        bool challenger_won = (chal_score > champ_score);

        if (challenger_won) {
            copy_pst(champion_pst, challenger_pst);
            champion_wins = 0;
            std::cout << "Gen " << generation << ": CHALLENGER WINS ("
                      << (result1 > 0 ? "W" : (result1 == 0 ? "D" : "L"))
                      << (result2 > 0 ? "W" : (result2 == 0 ? "D" : "L"))
                      << ") score=" << chal_score << "-" << champ_score
                      << " | NEW CHAMPION" << std::endl;
        } else {
            champion_wins++;
            std::cout << "Gen " << generation << ": champion retains ("
                      << (result1 > 0 ? "W" : (result1 == 0 ? "D" : "L"))
                      << (result2 > 0 ? "W" : (result2 == 0 ? "D" : "L"))
                      << ") score=" << chal_score << "-" << champ_score << std::endl;
        }

        // Save periodically
        if (generation % 50 == 0) {
            std::ofstream f("champion_pst.bin", std::ios::binary);
            f.write((const char*)champion_pst, sizeof(champion_pst));
            std::cout << "Saved champion PST to champion_pst.bin" << std::endl;
        }

        // Print summary every 20 generations
        if (generation % 20 == 0) {
            std::cout << "--- After " << generation << " generations, "
                      << total_matches << " matches played ---" << std::endl;
        }
    }

    // Save on exit
    std::ofstream f("champion_pst.bin", std::ios::binary);
    f.write((const char*)champion_pst, sizeof(champion_pst));
    std::cout << "Saved final champion PST to champion_pst.bin" << std::endl;

    // Copy to global PST for potential UCI use
    std::memcpy(PST, champion_pst, sizeof(PST));
    std::cout << "Self-play stopped after " << generation << " generations." << std::endl;
}
