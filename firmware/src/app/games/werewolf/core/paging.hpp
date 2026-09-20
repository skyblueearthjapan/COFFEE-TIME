#pragma once
#include "werewolf_core.hpp"
namespace coffee { namespace wolf {
constexpr uint8_t PAGE_SIZE=4;
struct SeatPage { bool valid=false; uint8_t pages=0; std::array<int8_t,PAGE_SIZE> seats{{NONE,NONE,NONE,NONE}}; };
inline SeatPage seatPage(uint8_t n,uint8_t page) {
    SeatPage out;if(!validPlayers(n))return out;
    out.pages=static_cast<uint8_t>((n+PAGE_SIZE-1)/PAGE_SIZE);
    if(page>=out.pages)return out;
    for(uint8_t i=0;i<PAGE_SIZE;++i) {
        const uint8_t s=static_cast<uint8_t>(page*PAGE_SIZE+i);
        if(s<n)out.seats[i]=static_cast<int8_t>(s);
    }
    out.valid=true;return out;
}
// Reserve A/B and PEACE are persistent separate buttons, never 'extra people'.
}} // namespace
