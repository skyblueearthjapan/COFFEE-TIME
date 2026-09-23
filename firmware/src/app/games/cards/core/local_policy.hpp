#pragma once
#include "../core/cards_core.hpp"
// Baseline opponent, not Jev and not an optimal card engine.
// Only the acting AI's own hand and public state may be passed to these functions.
namespace cafe_cards {
inline int local_poker_draw(const std::array<Card,5>& own) {
    auto v=poker_value(own); if(!v.valid)return -1;
    const int cat=v.key[0];
    if(cat==4||cat==5||cat==6||cat==8)return 0;
    std::array<int,15> counts{};for(auto c:own)++counts[rank(c)];
    int mask=0;
    if(cat==1||cat==2||cat==3||cat==7) {
        for(int i=0;i<5;++i)if(counts[rank(own[i])]==1)mask|=1<<i;
        return mask;
    }
    // High-card hand: prefer keeping a four-card flush draw.
    std::array<int,4> suits{};for(auto c:own)++suits[suit(c)];
    for(int s=0;s<4;++s)if(suits[s]==4) {
        for(int i=0;i<5;++i)if(suit(own[i])!=s)mask|=1<<i;
        return mask;
    }
    std::array<int,5> slots{0,1,2,3,4};
    std::sort(slots.begin(),slots.end(),[&](int a,int b){return rank(own[a])!=rank(own[b])?rank(own[a])>rank(own[b]):a<b;});
    for(int i=2;i<5;++i)mask|=1<<slots[i];
    return mask;
}
inline BetAction local_poker_bet(const std::array<Card,5>& own,const Street& s,uint32_t saved_roll) {
    auto v=poker_value(own);int cat=v.key[0],r=int(saved_roll%100);
    bool strong=cat>=2;
    if(s.paid[0]==s.paid[1]) {
        bool attack=strong || (cat==1&&r<45) || (cat==0&&r<12);
        return attack?BetAction::Bet:BetAction::Check;
    }
    if(s.raises<2 && (strong || (cat==1&&r<15) || (cat==0&&r<5)))return BetAction::Raise;
    if(cat>=1 || r<25)return BetAction::Call;
    return BetAction::Fold;
}
// remaining sorted ascending; jitter is -1,0,+1 drawn BEFORE receiving a human bid.
inline int local_gops(const std::array<int,13>& remaining,int count,int prize,int n,int jitter) {
    if(count<1||count>13||(n!=7&&n!=13)||prize<1||prize>n||jitter<-1||jitter>1)return -1;
    int index=((prize-1)*(count-1)+(n-1)/2)/(n-1);
    index=std::max(0,std::min(count-1,index+jitter));return remaining[index];
}
struct Local31 { int hand_slot=-1,market_slot=-1;bool knock=false,stand=false; };
inline Local31 local_thirty_one(const std::array<Card,3>& own,const std::array<Card,3>& market,bool final_reply) {
    Local31 a;int current=score31(own),best=-1;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
        auto h=own;h[i]=market[j];int s=score31(h);
        if(s>best){best=s;a.hand_slot=i;a.market_slot=j;}
    }
    if(final_reply && best<=current){a={};a.stand=true;}
    else if(!final_reply && (current>=27 || best<=current)){a={};a.knock=true;}
    return a;
}
} // namespace cafe_cards
