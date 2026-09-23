#pragma once
// CAFE CARDS v1.0.0 -- portable rules reference, no network/UI/RNG seeding.
// Card identity: copy*52 + suit*13 + (rank-2); suits C,D,H,S; ranks 2..14(A).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>

namespace cafe_cards {
using Card = uint16_t;
using Rng32 = uint32_t (*)(void*);
constexpr int H = 0, AI = 1, DRAW = 2, NONE = -1;
inline int rank(Card c) { return int(c % 13) + 2; }
inline int suit(Card c) { return int((c % 52) / 13); }
inline int baccarat_value(Card c) { int r=rank(c); return r==14?1:(r>=10?0:r); }
inline int value31(Card c) { int r=rank(c); return r==14?11:(r>=10?10:r); }
inline bool valid_cards(const Card* p, int n, int copies=1) {
    if (!p || n<0 || n>416 || copies<1 || copies>8) return false;
    std::array<bool,416> seen{};
    for (int i=0;i<n;++i) { if (p[i]>=52*copies || seen[p[i]]) return false; seen[p[i]]=true; }
    return true;
}
// Caller supplies a correctly seeded source; tests use deterministic generators.
inline uint32_t uniform(Rng32 rng, void* ctx, uint32_t bound) {
    if (!rng || !bound) return 0; // Production adapter must reject invalid configuration.
    uint32_t threshold=uint32_t(-bound)%bound;
    uint32_t x;
    do { x=rng(ctx); } while (x<threshold);
    return x%bound;
}
struct Deck {
    std::array<Card,416> cards{};
    int size=0, cursor=0;
    bool init(int copies, Rng32 rng, void* ctx) {
        if ((copies!=1 && copies!=8) || !rng) return false;
        size=52*copies; cursor=0;
        for (int i=0;i<size;++i) cards[i]=Card(i);
        for (int i=size-1;i>0;--i) std::swap(cards[i],cards[uniform(rng,ctx,uint32_t(i+1))]);
        return true;
    }
    bool take(Card& out) { if (cursor<0 || cursor>=size) return false; out=cards[cursor++]; return true; }
    bool valid() const { return (size==52 || size==416) && cursor>=0 && cursor<=size && valid_cards(cards.data(),size,size/52); }
};

struct PokerValue {
    // category: high, pair, two_pair, trips, straight, flush, full, quads, straight_flush
    std::array<int,6> key{};
    bool valid=false;
};
inline PokerValue poker_value(const std::array<Card,5>& h) {
    PokerValue out;
    if (!valid_cards(h.data(),5)) return out;
    out.valid=true;
    std::array<int,15> count{};
    std::array<int,5> ranks{};
    bool flush=true;
    for (int i=0;i<5;++i) { ++count[rank(h[i])]; ranks[i]=rank(h[i]); flush=flush && suit(h[i])==suit(h[0]); }
    std::sort(ranks.begin(),ranks.end(),std::greater<int>());
    int straight=0, distinct=0;
    for(int r=2;r<=14;++r) if(count[r]) ++distinct;
    if (distinct==5 && ranks[0]-ranks[4]==4) straight=ranks[0];
    if (ranks==std::array<int,5>{14,5,4,3,2}) straight=5;
    int four=0,three=0,np=0; std::array<int,2> pair{}; std::array<int,5> singles{}; int ns=0;
    for(int r=14;r>=2;--r) {
        if(count[r]==4) four=r;
        else if(count[r]==3) three=r;
        else if(count[r]==2) pair[np++]=r;
        else if(count[r]==1) singles[ns++]=r;
    }
    auto& k=out.key;
    if(straight && flush) k={8,straight,0,0,0,0};
    else if(four) k={7,four,singles[0],0,0,0};
    else if(three && np==1) k={6,three,pair[0],0,0,0};
    else if(flush) k={5,ranks[0],ranks[1],ranks[2],ranks[3],ranks[4]};
    else if(straight) k={4,straight,0,0,0,0};
    else if(three) k={3,three,singles[0],singles[1],0,0};
    else if(np==2) k={2,pair[0],pair[1],singles[0],0,0};
    else if(np==1) k={1,pair[0],singles[0],singles[1],singles[2],0};
    else k={0,ranks[0],ranks[1],ranks[2],ranks[3],ranks[4]};
    return out;
}
inline int poker_compare(const std::array<Card,5>& a,const std::array<Card,5>& b) {
    auto x=poker_value(a),y=poker_value(b);
    if(!x.valid || !y.valid) return -2;
    return x.key>y.key?1:(x.key<y.key?-1:0);
}
enum class BetAction : uint8_t { Check,Bet,Call,Raise,Fold };
struct Street {
    std::array<int,2> paid{};
    int actor=0,unit=2,raises=0,checks=0;
    bool closed=false; int fold_winner=NONE;
    bool legal(BetAction a) const {
        if(closed || actor<0 || actor>1) return false;
        int owed=paid[1-actor]-paid[actor];
        if(owed>0) return a==BetAction::Call || a==BetAction::Fold || (a==BetAction::Raise && raises<2);
        return a==BetAction::Check || (a==BetAction::Bet && paid[0]==0 && paid[1]==0);
    }
    // Return debit through output parameter. Rejected action does not mutate state.
    bool apply(int who,BetAction a,int& debit) {
        debit=0;
        if(who!=actor || !legal(a)) return false;
        int target=std::max(paid[0],paid[1]);
        if(a==BetAction::Fold) { closed=true;fold_winner=1-who;return true; }
        if(a==BetAction::Check) { if(++checks==2) closed=true; else actor=1-who;return true; }
        if(a==BetAction::Bet || a==BetAction::Raise) {
            target+=unit; if(a==BetAction::Raise) ++raises;
            debit=target-paid[who]; paid[who]=target;checks=0;actor=1-who;return true;
        }
        debit=target-paid[who];paid[who]=target;closed=true;return true; // call
    }
};
enum class PokerPhase : uint8_t { Pre,Draw,Post,Finished };
struct PokerHand {
    Deck deck;
    std::array<std::array<Card,5>,2> hands{};
    std::array<Card,10> discards{}; int discard_count=0;
    std::array<int,2> stack{100,100}, draw_masks{-1,-1};
    int pot=0,dealer=0,winner=NONE; bool folded=false;
    PokerPhase phase=PokerPhase::Finished; Street street;
    bool start(const Deck& d,int dl,std::array<int,2> before) {
        if(dl<0 || dl>1 || d.size!=52 || d.cursor!=0 || !d.valid() || before[0]<19 || before[1]<19 || before[0]+before[1]!=200) return false;
        deck=d;dealer=dl;stack=before;pot=2;--stack[0];--stack[1];winner=NONE;folded=false;
        draw_masks={-1,-1};discard_count=0;
        int first=1-dealer;
        for(int k=0;k<5;++k) for(int x=0;x<2;++x) { int who=x?dealer:first; deck.take(hands[who][k]); }
        phase=PokerPhase::Pre;street=Street{};street.actor=first;street.unit=2;
        return true;
    }
    void award(int w) {
        winner=w;
        if(w==DRAW) { stack[0]+=pot/2;stack[1]+=pot/2; }
        else stack[w]+=pot;
        pot=0;phase=PokerPhase::Finished;
    }
    bool bet(int who,BetAction a) {
        if(phase!=PokerPhase::Pre && phase!=PokerPhase::Post) return false;
        Street next=street; int debit=0;
        if(!next.apply(who,a,debit) || debit>stack[who]) return false;
        street=next;stack[who]-=debit;pot+=debit;
        if(street.closed) {
            if(street.fold_winner!=NONE) {folded=true;award(street.fold_winner);}
            else if(phase==PokerPhase::Pre) phase=PokerPhase::Draw;
            else {int cmp=poker_compare(hands[0],hands[1]);award(cmp>0?0:(cmp<0?1:DRAW));}
        }
        return true;
    }
    bool draw(int who,int mask) {
        if(phase!=PokerPhase::Draw || who<0 || who>1 || mask<0 || mask>31 || draw_masks[who]!=-1) return false;
        draw_masks[who]=mask;
        if(draw_masks[0]<0 || draw_masks[1]<0) return true;
        // Both choices sealed. No discard recycling; replace fixed slots in nondealer/dealer order.
        for(int x=0;x<2;++x) { int p=x?dealer:1-dealer;
            for(int i=0;i<5;++i) if(draw_masks[p]&(1<<i)) {
                discards[discard_count++]=hands[p][i];deck.take(hands[p][i]);
            }
        }
        phase=PokerPhase::Post;street=Street{};street.actor=1-dealer;street.unit=4;return true;
    }
};

struct Gops {
    int n=0,index=0,burned=0; std::array<int,13> prizes{};
    std::array<uint16_t,2> remaining{};std::array<int,2> sealed{-1,-1},score{};
    bool init(int count,const std::array<int,13>& order) {
        if(count!=7 && count!=13) return false;
        uint16_t bits=0;
        for(int i=0;i<count;++i) {int p=order[i];if(p<1 || p>count || (bits&(1u<<(p-1)))) return false;bits|=uint16_t(1u<<(p-1));}
        n=count;index=burned=0;prizes=order;remaining={bits,bits};sealed={-1,-1};score={0,0};return true;
    }
    bool done() const {return n>0 && index==n;}
    bool choose(int who,int value) {
        if(n==0 || done() || who<0 || who>1 || value<1 || value>n || sealed[who]!=-1 || !(remaining[who]&(1u<<(value-1)))) return false;
        sealed[who]=value;
        if(sealed[0]<0 || sealed[1]<0) return true;
        if(sealed[0]==sealed[1]) burned+=prizes[index]; else score[sealed[0]>sealed[1]?0:1]+=prizes[index];
        for(int i=0;i<2;++i) remaining[i]&=uint16_t(~(1u<<(sealed[i]-1)));
        ++index;sealed={-1,-1};return true;
    }
    int winner() const {return !done()?NONE:(score[0]>score[1]?H:(score[0]<score[1]?AI:DRAW));}
};
inline int score31(const std::array<Card,3>& h) {
    if(!valid_cards(h.data(),3)) return -1;
    std::array<int,4> sums{};for(auto c:h) sums[suit(c)]+=value31(c);
    return *std::max_element(sums.begin(),sums.end());
}
struct ThirtyOne {
    std::array<std::array<Card,3>,2> hands{};std::array<Card,3> market{};
    std::array<uint64_t,2> publicly_known{};
    int actor=0,turns=0,knocker=NONE,winner=NONE;bool finished=false;
    enum End { None,Natural,Knocked,Limit } end=None;
    bool init(const std::array<Card,9>& deal,int first) {
        if(!valid_cards(deal.data(),9) || first<0 || first>1) return false;
        for(int k=0;k<3;++k) {hands[first][k]=deal[2*k];hands[1-first][k]=deal[2*k+1];market[k]=deal[6+k];}
        actor=first;turns=0;knocker=NONE;winner=NONE;finished=false;end=None;publicly_known={0,0};
        if(score31(hands[0])==31 || score31(hands[1])==31) finish(Natural);
        return true;
    }
    void finish(End why) {finished=true;end=why;int a=score31(hands[0]),b=score31(hands[1]);winner=a>b?H:(a<b?AI:DRAW);}
    bool swap(int who,int hi,int mi) {
        if(finished || who!=actor || hi<0 || hi>2 || mi<0 || mi>2) return false;
        Card out=hands[who][hi],in=market[mi];
        std::swap(hands[who][hi],market[mi]);
        publicly_known[who]&=~(uint64_t(1)<<out);publicly_known[who]|=(uint64_t(1)<<in);++turns;
        if(score31(hands[who])==31) finish(Natural);
        else if(knocker!=NONE) finish(Knocked);
        else if(turns>=20) finish(Limit);
        else actor=1-who;
        return true;
    }
    bool knock(int who) {
        if(finished || who!=actor || knocker!=NONE) return false;
        knocker=who;++turns;actor=1-who;return true; // exactly one final reply, even on turn 20
    }
    bool stand(int who) {
        if(finished || who!=actor || knocker==NONE || who==knocker) return false;
        ++turns;finish(Knocked);return true;
    }
};
inline bool banker_draws(int total,int player_third) {
    if(total<0 || total>9 || player_third<-1 || player_third>9) return false;
    if(player_third<0) return total<=5;
    if(total<=2) return true;
    if(total==3) return player_third!=8;
    if(total==4) return player_third>=2 && player_third<=7;
    if(total==5) return player_third>=4 && player_third<=7;
    if(total==6) return player_third==6 || player_third==7;
    return false;
}
struct BaccaratResult {
    std::array<int,3> p{-1,-1,-1},b{-1,-1,-1};
    int np=0,nb=0,used=0,pt=0,bt=0,winner=NONE;bool natural=false,valid=false;
};
inline BaccaratResult baccarat_from_values(const std::array<int,6>& v) {
    BaccaratResult r;
    for(auto x:v) if(x<0 || x>9) return r;
    r.valid=true;r.p[0]=v[0];r.b[0]=v[1];r.p[1]=v[2];r.b[1]=v[3];r.np=r.nb=2;r.used=4;
    r.pt=(v[0]+v[2])%10;r.bt=(v[1]+v[3])%10;r.natural=r.pt>=8 || r.bt>=8;
    if(!r.natural) {
        int third=-1;
        if(r.pt<=5) {third=v[r.used++];r.p[r.np++]=third;r.pt=(r.pt+third)%10;}
        if(banker_draws(r.bt,third)) {int x=v[r.used++];r.b[r.nb++]=x;r.bt=(r.bt+x)%10;}
    }
    r.winner=r.pt>r.bt?0:(r.pt<r.bt?1:DRAW);return r;
}
inline BaccaratResult baccarat_from_cards(const std::array<Card,6>& c) {
    if(!valid_cards(c.data(),6,8)) return {};
    std::array<int,6> v{};for(int i=0;i<6;++i) v[i]=baccarat_value(c[i]);return baccarat_from_values(v);
}
// Independent match aggregation: P/B are table sides, not the human/AI actors.
struct BaccaratPredictions {
    std::array<int,2> guess{-1,-1};
    bool choose(int who,int outcome) {if(who<0 || who>1 || outcome<0 || outcome>2 || guess[who]!=-1) return false;guess[who]=outcome;return true;}
    bool ready() const {return guess[0]>=0 && guess[1]>=0;}
    std::array<int,2> points(int outcome) const {if(!ready() || outcome<0 || outcome>2) return {0,0};return {int(guess[0]==outcome),int(guess[1]==outcome)};}
};
} // namespace cafe_cards
