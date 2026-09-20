#pragma once
#include "werewolf_core.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace coffee { namespace wolf {
struct PublicMeta {
    bool armed=false,sound=false;
    uint8_t players=3;
    uint16_t discussion_s=0; // 0 means automatic for the selected player count.
    uint32_t flavor_seq=0;
};
enum class MetaFormat { Invalid, LegacyV1, CurrentV2 };
inline uint32_t crc32ieee(const uint8_t* data,std::size_t length) {
    uint32_t crc=0xffffffffu;
    for(std::size_t i=0;i<length;++i) {
        crc^=data[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return crc^0xffffffffu;
}
inline bool validMeta(const PublicMeta& m) {return validPlayers(m.players)&&validDiscussion(m.discussion_s);}
inline bool encodeMeta(const PublicMeta& m,std::array<uint8_t,20>& out) {
    if(!validMeta(m))return false;
    out.fill(0);out[0]='C';out[1]='T';out[2]='W';out[3]='2';out[4]=2;
    out[5]=m.armed?1:0;out[6]=m.sound?1:0;out[7]=m.players;
    out[8]=static_cast<uint8_t>(m.discussion_s);out[9]=static_cast<uint8_t>(m.discussion_s>>8);
    for(unsigned i=0;i<4;++i)out[12+i]=static_cast<uint8_t>(m.flavor_seq>>(8*i));
    const uint32_t crc=crc32ieee(out.data(),16);
    for(unsigned i=0;i<4;++i)out[16+i]=static_cast<uint8_t>(crc>>(8*i));
    return true;
}
inline MetaFormat decodeMeta(const uint8_t* in,std::size_t length,PublicMeta& out) {
    if(!in||length!=20||in[0]!='C'||in[1]!='T'||in[2]!='W')return MetaFormat::Invalid;
    const bool legacy=in[3]=='1'&&in[4]==1;
    const bool current=in[3]=='2'&&in[4]==2;
    if(!legacy&&!current)return MetaFormat::Invalid;
    if(in[5]>1||in[6]>1||in[10]!=0||in[11]!=0||(legacy&&in[7]!=0))return MetaFormat::Invalid;
    uint32_t crc=0;for(unsigned i=0;i<4;++i)crc|=uint32_t(in[16+i])<<(8*i);
    if(crc!=crc32ieee(in,16))return MetaFormat::Invalid;
    PublicMeta m;m.armed=in[5]!=0;m.sound=in[6]!=0;m.players=legacy?5:in[7];
    m.discussion_s=uint16_t(in[8])|(uint16_t(in[9])<<8);
    if(legacy&&m.discussion_s!=120&&m.discussion_s!=180&&m.discussion_s!=240)return MetaFormat::Invalid;
    m.flavor_seq=0;for(unsigned i=0;i<4;++i)m.flavor_seq|=uint32_t(in[12+i])<<(8*i);
    if(!validMeta(m))return MetaFormat::Invalid;
    out=m;return legacy?MetaFormat::LegacyV1:MetaFormat::CurrentV2;
}
}} // namespace
