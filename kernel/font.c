/* 5x7 dot-matrix font, ported from demos/acidstorm/src/font.c so the kernel
 * and the ACIDSTORM demo share one glyph set. The only change from the
 * original is dropping <ctype.h>'s toupper() for a manual equivalent --
 * there's no libc linked into the kernel (-nostdlib), so even a single
 * libc call would fail at link time. Each glyph is 7 rows of 5 chars:
 * 'X' = lit, '.' = off. */

#include "font.h"

static const char *const G_A[7] = {".XXX.","X...X","X...X","XXXXX","X...X","X...X","X...X"};
static const char *const G_B[7] = {"XXXX.","X...X","X...X","XXXX.","X...X","X...X","XXXX."};
static const char *const G_C[7] = {".XXXX","X....","X....","X....","X....","X....",".XXXX"};
static const char *const G_D[7] = {"XXXX.","X...X","X...X","X...X","X...X","X...X","XXXX."};
static const char *const G_E[7] = {"XXXXX","X....","X....","XXXX.","X....","X....","XXXXX"};
static const char *const G_F[7] = {"XXXXX","X....","X....","XXXX.","X....","X....","X...."};
static const char *const G_G[7] = {".XXXX","X....","X....","X.XXX","X...X","X...X",".XXXX"};
static const char *const G_H[7] = {"X...X","X...X","X...X","XXXXX","X...X","X...X","X...X"};
static const char *const G_I[7] = {"XXXXX","..X..","..X..","..X..","..X..","..X..","XXXXX"};
static const char *const G_J[7] = {"..XXX","...X.","...X.","...X.","...X.","X..X.",".XX.."};
static const char *const G_K[7] = {"X...X","X..X.","X.X..","XX...","X.X..","X..X.","X...X"};
static const char *const G_L[7] = {"X....","X....","X....","X....","X....","X....","XXXXX"};
static const char *const G_M[7] = {"X...X","XX.XX","X.X.X","X...X","X...X","X...X","X...X"};
static const char *const G_N[7] = {"X...X","XX..X","X.X.X","X..XX","X...X","X...X","X...X"};
static const char *const G_O[7] = {".XXX.","X...X","X...X","X...X","X...X","X...X",".XXX."};
static const char *const G_P[7] = {"XXXX.","X...X","X...X","XXXX.","X....","X....","X...."};
static const char *const G_Q[7] = {".XXX.","X...X","X...X","X...X","X.X.X","X..X.",".XX.X"};
static const char *const G_R[7] = {"XXXX.","X...X","X...X","XXXX.","X.X..","X..X.","X...X"};
static const char *const G_S[7] = {".XXXX","X....","X....",".XXX.","....X","....X","XXXX."};
static const char *const G_T[7] = {"XXXXX","..X..","..X..","..X..","..X..","..X..","..X.."};
static const char *const G_U[7] = {"X...X","X...X","X...X","X...X","X...X","X...X",".XXX."};
static const char *const G_V[7] = {"X...X","X...X","X...X","X...X","X...X",".X.X.","..X.."};
static const char *const G_W[7] = {"X...X","X...X","X...X","X.X.X","X.X.X","X.X.X",".X.X."};
static const char *const G_X[7] = {"X...X","X...X",".X.X.","..X..",".X.X.","X...X","X...X"};
static const char *const G_Y[7] = {"X...X","X...X",".X.X.","..X..","..X..","..X..","..X.."};
static const char *const G_Z[7] = {"XXXXX","....X","...X.","..X..",".X...","X....","XXXXX"};

static const char *const G_0[7] = {".XXX.","X...X","X..XX","X.X.X","XX..X","X...X",".XXX."};
static const char *const G_1[7] = {"..X..",".XX..","..X..","..X..","..X..","..X..",".XXX."};
static const char *const G_2[7] = {".XXX.","X...X","....X","...X.","..X..",".X...","XXXXX"};
static const char *const G_3[7] = {".XXX.","X...X","....X","..XX.","....X","X...X",".XXX."};
static const char *const G_4[7] = {"...X.","..XX.",".X.X.","X..X.","XXXXX","...X.","...X."};
static const char *const G_5[7] = {"XXXXX","X....","XXXX.","....X","....X","X...X",".XXX."};
static const char *const G_6[7] = {"..XX.",".X...","X....","XXXX.","X...X","X...X",".XXX."};
static const char *const G_7[7] = {"XXXXX","....X","...X.","..X..","..X..","..X..","..X.."};
static const char *const G_8[7] = {".XXX.","X...X","X...X",".XXX.","X...X","X...X",".XXX."};
static const char *const G_9[7] = {".XXX.","X...X","X...X",".XXXX","....X","...X.",".XX.."};

static const char *const G_SPACE[7] = {".....",".....",".....",".....",".....",".....","....."};
static const char *const G_COLON[7] = {".....","..X..",".....",".....","..X..",".....","....."};
static const char *const G_SEMI[7]  = {".....","..X..",".....",".....",".....","..X..",".X..."};
static const char *const G_DASH[7]  = {".....",".....",".....","XXXXX",".....",".....","....."};
static const char *const G_BANG[7]  = {"..X..","..X..","..X..","..X..","..X..",".....","..X.."};
static const char *const G_DOT[7]   = {".....",".....",".....",".....",".....",".X...",".X..."};
static const char *const G_COMMA[7] = {".....",".....",".....",".....",".....","..X..",".X..."};
static const char *const G_APOS[7]  = {".X...",".X...",".....",".....",".....",".....","....."};
static const char *const G_GT[7]    = {"X....",".X...","..X..","...X.","..X..",".X...","X...."};
static const char *const G_PCT[7]   = {"X...X","...X.","..X..",".X...","X...X","X...X","....."};
static const char *const G_LT[7]    = {"....X","...X.","..X..",".X...","..X..","...X.","....X"};
static const char *const G_EQ[7]    = {".....",".....","XXXXX",".....","XXXXX",".....","....."};
static const char *const G_PLUS[7]  = {".....","..X..","..X..","XXXXX","..X..","..X..","....."};
static const char *const G_STAR[7]  = {".....","..X..","X.X.X",".XXX.","X.X.X","..X..","....."};
static const char *const G_SLASH[7] = {"....X","...X.","...X.","..X..",".X...",".X...","X...."};
static const char *const G_AT[7]    = {".XXX.","X...X","X.XXX","X.X.X","X.XX.","X....",".XXX."};

static char raveos_toupper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

const char *const *font_glyph(char c) {
    c = raveos_toupper(c);
    switch (c) {
        case 'A': return G_A; case 'B': return G_B; case 'C': return G_C;
        case 'D': return G_D; case 'E': return G_E; case 'F': return G_F;
        case 'G': return G_G; case 'H': return G_H; case 'I': return G_I;
        case 'J': return G_J; case 'K': return G_K; case 'L': return G_L;
        case 'M': return G_M; case 'N': return G_N; case 'O': return G_O;
        case 'P': return G_P; case 'Q': return G_Q; case 'R': return G_R;
        case 'S': return G_S; case 'T': return G_T; case 'U': return G_U;
        case 'V': return G_V; case 'W': return G_W; case 'X': return G_X;
        case 'Y': return G_Y; case 'Z': return G_Z;
        case '0': return G_0; case '1': return G_1; case '2': return G_2;
        case '3': return G_3; case '4': return G_4; case '5': return G_5;
        case '6': return G_6; case '7': return G_7; case '8': return G_8;
        case '9': return G_9;
        case ':': return G_COLON;
        case ';': return G_SEMI;
        case '-': return G_DASH;
        case '!': return G_BANG;
        case '.': return G_DOT;
        case ',': return G_COMMA;
        case '\'': return G_APOS;
        case '>': return G_GT;
        case '%': return G_PCT;
        case '<': return G_LT;
        case '=': return G_EQ;
        case '+': return G_PLUS;
        case '*': return G_STAR;
        case '/': return G_SLASH;
        case '@': return G_AT;
        case ' ': default: return G_SPACE;
    }
}
