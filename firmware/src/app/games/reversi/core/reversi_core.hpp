#pragma once
// COFFEE TIME reference core. No LVGL, network, global mutable state or heap allocation.
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ct_rev {
constexpr int PASS = -1;
enum class Cell : uint8_t { Empty=0, Black=1, White=2 };
enum class Error : uint8_t { Ok, BadState, OutOfRange, Occupied, NoCapture, PassForbidden, Finished };
inline Cell other(Cell c) { return c==Cell::Black ? Cell::White : Cell::Black; }
inline char symbol(Cell c) { return c==Cell::Black?'B':c==Cell::White?'W':'.'; }
struct Position {
    uint8_t n=6;
    Cell side=Cell::Black;
    uint16_t ply=0;
    std::array<Cell,64> board{};
};
struct MoveList { std::array<uint8_t,64> cells{}; uint8_t count=0; };
struct Transition { Error error=Error::Ok; uint64_t flips=0; uint8_t flipped=0; bool passed=false; bool terminal=false; };
struct Counts { int black=0, white=0, empty=0; };
inline bool valid(const Position& p) {
    if ((p.n!=6 && p.n!=8) || (p.side!=Cell::Black && p.side!=Cell::White) || p.ply>128) return false;
    for (int i=0;i<64;i++) {
        if (static_cast<unsigned>(p.board[i])>2) return false;
        if (i>=p.n*p.n && p.board[i]!=Cell::Empty) return false;
    }
    return true; // Reachability must be checked by replay, not this structural check.
}
inline bool initial(uint8_t n, Position& out) {
    if(n!=6 && n!=8) return false;
    Position p; p.n=n;
    const int a=n/2-1,b=n/2;
    p.board[a*n+a]=Cell::White; p.board[b*n+b]=Cell::White;
    p.board[a*n+b]=Cell::Black; p.board[b*n+a]=Cell::Black;
    out=p; return true;
}
inline uint64_t captures(const Position& p,int square,Cell color) {
    if(!valid(p) || (color!=Cell::Black && color!=Cell::White) || square<0 || square>=p.n*p.n || p.board[square]!=Cell::Empty) return 0;
    uint64_t total=0;
    for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
        if(!dx&&!dy) continue;
        int r=square/p.n+dy, c=square%p.n+dx;
        uint64_t ray=0;
        while(r>=0 && r<p.n && c>=0 && c<p.n && p.board[r*p.n+c]==other(color)) {
            ray |= uint64_t(1) << (r*p.n+c); r+=dy; c+=dx;
        }
        if(ray && r>=0 && r<p.n && c>=0 && c<p.n && p.board[r*p.n+c]==color) total|=ray;
    }
    return total;
}
inline MoveList legal(const Position& p,Cell color) {
    MoveList out;
    if(!valid(p)) return out;
    for(int i=0;i<p.n*p.n;i++) if(captures(p,i,color)) out.cells[out.count++]=static_cast<uint8_t>(i);
    return out;
}
inline bool terminal(const Position& p) {
    return valid(p) && legal(p,Cell::Black).count==0 && legal(p,Cell::White).count==0;
}
inline Counts counts(const Position& p) {
    Counts out;
    if(!valid(p)) return out;
    for(int i=0;i<p.n*p.n;i++) {
        if(p.board[i]==Cell::Black) ++out.black;
        else if(p.board[i]==Cell::White) ++out.white;
        else ++out.empty;
    }
    return out;
}
inline char winner(const Position& p) {
    if(!terminal(p)) return '-';
    const auto x=counts(p); return x.black>x.white?'B':x.white>x.black?'W':'D';
}
inline Transition apply(Position& p,int move) {
    Transition t;
    if(!valid(p) || p.ply>=128) {t.error=Error::BadState;return t;}
    if(terminal(p)) {t.error=Error::Finished;return t;}
    Position next=p;
    if(move==PASS) {
        if(legal(p,p.side).count) {t.error=Error::PassForbidden;return t;}
        t.passed=true;
    } else {
        if(move<0 || move>=p.n*p.n) {t.error=Error::OutOfRange;return t;}
        if(p.board[move]!=Cell::Empty) {t.error=Error::Occupied;return t;}
        t.flips=captures(p,move,p.side);
        if(!t.flips) {t.error=Error::NoCapture;return t;}
        next.board[move]=p.side;
        for(int i=0;i<p.n*p.n;i++) if(t.flips & (uint64_t(1)<<i)) {next.board[i]=p.side;++t.flipped;}
    }
    next.side=other(p.side); ++next.ply;
    t.terminal=terminal(next); p=next; return t;
}
inline int frontier(const Position& p,Cell color) {
    int count=0;
    for(int r=0;r<p.n;r++) for(int c=0;c<p.n;c++) if(p.board[r*p.n+c]==color) {
        bool near=false;
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
            int y=r+dy,x=c+dx;
            if((dx||dy)&&y>=0&&y<p.n&&x>=0&&x<p.n&&p.board[y*p.n+x]==Cell::Empty) near=true;
        }
        if(near) ++count;
    }
    return count;
}
inline int risk(const Position& p,Cell color,bool diagonal) {
    int total=0, n=p.n;
    for(int r : {0,n-1}) for(int c : {0,n-1}) if(p.board[r*n+c]==Cell::Empty) {
        int ir=r==0?1:n-2, ic=c==0?1:n-2;
        if(diagonal) total+=(p.board[ir*n+ic]==color);
        else total+=(p.board[ir*n+c]==color)+(p.board[r*n+ic]==color);
    }
    return total;
}
inline int evaluate(const Position& p,Cell root) {
    const Cell opp=other(root); const auto x=counts(p);
    int diff=(root==Cell::Black?x.black-x.white:x.white-x.black);
    if(terminal(p)) return diff==0?0:(diff>0?100000:-100000)+diff;
    int corners=0;
    for(int r : {0,int(p.n)-1}) for(int c : {0,int(p.n)-1}) {
        corners+=(p.board[r*p.n+c]==root); corners-=(p.board[r*p.n+c]==opp);
    }
    int w= x.empty>p.n*p.n/2 ? -1 : x.empty>p.n*p.n/4 ? 1 : 6;
    return 120*corners+8*(int(legal(p,root).count)-int(legal(p,opp).count))
        -4*(frontier(p,root)-frontier(p,opp))
        -25*(risk(p,root,true)-risk(p,opp,true))
        -10*(risk(p,root,false)-risk(p,opp,false))+w*diff;
}
inline int local_move(const Position& p) {
    const auto moves=legal(p,p.side);
    if(!moves.count) return PASS;
    int best=moves.cells[0], score=-200000;
    for(int k=0;k<moves.count;k++) {
        Position child=p;
        if(apply(child,moves.cells[k]).error!=Error::Ok) return PASS;
        int s=evaluate(child,p.side);
        if(s>score) {score=s;best=moves.cells[k];}
    }
    return best; // Equal evaluations: ascending row-major square index.
}
inline int hit_test(int x,int y,uint8_t n) {
    if((n!=6&&n!=8)||x<84||x>=396||y<84||y>=396) return -1;
    int unit=312/n; return ((y-84)/unit)*n+(x-84)/unit;
}
inline uint32_t crc32(const uint8_t* data,size_t size) {
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<size;i++) {
        crc^=data[i];
        for(int j=0;j<8;j++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return crc^0xffffffffu;
}
} // namespace ct_rev
