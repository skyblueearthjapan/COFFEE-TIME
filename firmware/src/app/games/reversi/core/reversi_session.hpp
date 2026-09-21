#pragma once
#include "reversi_core.hpp"
namespace ct_rev {
enum class Mode:uint8_t {Jev=0,Pro=1,Casual=2,Local=3};
enum class Source:uint8_t {Human=0,Jev=1,Local=2,Forced=3};
enum class Closure:uint8_t {Active=0,Completed=1,Resigned=2,Aborted=3};
struct Event {uint8_t move=255; Source source=Source::Forced;};
struct Session {
    Position pos;
    Cell human=Cell::Black;
    Mode mode=Mode::Jev;
    Closure closure=Closure::Active;
    bool localOnly=false;
    uint32_t generation=1;
    std::array<uint8_t,16> id{};
    std::array<Event,128> history{};
    uint16_t count=0;
};
inline bool start(Session& s,uint8_t n,Cell human,Mode mode,const std::array<uint8_t,16>& id) {
    if((human!=Cell::Black&&human!=Cell::White)||unsigned(mode)>3) return false;
    Session v; if(!initial(n,v.pos))return false;
    v.human=human;v.mode=mode;v.localOnly=mode==Mode::Local;v.id=id;s=v;return true;
}
inline bool commit(Session& s,int move,Source source,uint16_t expectedPly) {
    if(s.closure!=Closure::Active||s.count>=128||s.generation==0xffffffffu||s.pos.ply!=expectedPly||s.count!=expectedPly) return false;
    const auto lm=legal(s.pos,s.pos.side);
    if(move==PASS) {if(source!=Source::Forced)return false;}
    else if(s.pos.side==s.human) {if(source!=Source::Human)return false;}
    else if(lm.count==1) {if(source!=Source::Forced)return false;}
    else {
        if(source!=Source::Jev&&source!=Source::Local)return false;
        if(source==Source::Jev&&s.localOnly)return false;
    }
    Session next=s;
    if(apply(next.pos,move).error!=Error::Ok)return false;
    next.history[next.count++]={uint8_t(move==PASS?255:move),source};
    if(source==Source::Local)next.localOnly=true;
    if(terminal(next.pos))next.closure=Closure::Completed;
    ++next.generation;s=next;return true;
}
inline bool freezeLocal(Session& s) {
    if(s.closure!=Closure::Active||s.generation==0xffffffffu)return false;
    if(!s.localOnly){s.localOnly=true;++s.generation;}return true;
}
inline bool close(Session& s,Closure reason) {
    if(s.closure!=Closure::Active||s.generation==0xffffffffu||
       (reason!=Closure::Resigned&&reason!=Closure::Aborted))return false;
    s.closure=reason;++s.generation;return true;
}
constexpr size_t SNAPSHOT_MAX=294;
inline void put32(uint8_t* p,uint32_t v){for(int i=0;i<4;i++)p[i]=uint8_t(v>>(8*i));}
inline uint32_t get32(const uint8_t* p){uint32_t v=0;for(int i=0;i<4;i++)v|=uint32_t(p[i])<<(8*i);return v;}
inline size_t encode(const Session& s,std::array<uint8_t,SNAPSHOT_MAX>& out) {
    if(s.count>128)return 0;
    out.fill(0);out[0]='R';out[1]='E';out[2]='V';out[3]='1';out[4]=1;
    out[5]=s.pos.n;out[6]=uint8_t(s.human);out[7]=uint8_t(s.mode);out[8]=uint8_t(s.closure);out[9]=s.localOnly?1:0;
    put32(out.data()+12,s.generation);
    std::memcpy(out.data()+16,s.id.data(),16);out[32]=uint8_t(s.count);out[33]=uint8_t(s.count>>8);
    for(int i=0;i<s.count;i++){out[34+2*i]=s.history[i].move;out[35+2*i]=uint8_t(s.history[i].source);}
    size_t bytes=38+2*s.count;put32(out.data()+bytes-4,crc32(out.data(),bytes-4));return bytes;
}
inline bool decode(const uint8_t* p,size_t bytes,Session& out) {
    if(!p||bytes<38||bytes>SNAPSHOT_MAX||std::memcmp(p,"REV1",4)||p[4]!=1||p[9]>1||p[10]||p[11])return false;
    if(get32(p+bytes-4)!=crc32(p,bytes-4)||get32(p+12)==0||p[8]>3)return false;
    uint16_t count=uint16_t(p[32])|(uint16_t(p[33])<<8);if(count>128||bytes!=size_t(38+2*count))return false;
    std::array<uint8_t,16> id;std::memcpy(id.data(),p+16,16);
    Session s;if(!start(s,p[5],Cell(p[6]),Mode(p[7]),id))return false;
    for(int i=0;i<count;i++){
        uint8_t m=p[34+2*i],source=p[35+2*i];if(source>3)return false;
        if(!commit(s,m==255?PASS:int(m),Source(source),s.pos.ply))return false;
    }
    const auto closure=Closure(p[8]);
    if(terminal(s.pos)!=(closure==Closure::Completed))return false;
    if(s.localOnly&&!p[9])return false;
    if(get32(p+12)<s.generation)return false;
    s.closure=closure;s.localOnly=p[9]!=0;s.generation=get32(p+12);out=s;return true;
}
} // namespace ct_rev
