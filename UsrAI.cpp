#include "UsrAI.h"
tagGame tagUsrGame;
ins UsrIns;
/*##########DO NOT MODIFY THE CODE ABOVE##########*/

#include <algorithm>
#include <cmath>
#include <climits>
#include <chrono>
#include <map>
#include <queue>
#include <set>
#include <vector>
#include <iostream>

namespace {
struct Pos {
    double x,y;
    Pos(double a=0,double b=0):x(a),y(b){}
};
double dist(Pos a,Pos b){return std::hypot(a.x-b.x,a.y-b.y);}
double reachDist(Pos a,Pos b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
int frames(double seconds){return std::max(1,int(seconds*1000/std::max(1,TimePerFrame)));}
int sizeOf(int type){return type==BUILDING_HOME || type==BUILDING_ARROWTOWER || type==BUILDING_DOCK?SIZELEN_SMALL:SIZELEN_MIDDLE;}
Pos pos(const tagHuman& h){return Pos(h.DR/BLOCKSIDELENGTH,h.UR/BLOCKSIDELENGTH);}
Pos pos(const tagResource& r){return Pos(r.DR/BLOCKSIDELENGTH,r.UR/BLOCKSIDELENGTH);}
Pos pos(const tagBuilding& b){return Pos(b.BlockDR+sizeOf(b.Type)*.5,b.BlockUR+sizeOf(b.Type)*.5);}
bool ranged(int sort){return sort==AT_BOWMAN || sort==AT_COMPOSITE_BOWMAN || sort==AT_IMPROVED || sort==AT_CHARIOT_ARCHER || sort==AT_SLINGER;}
bool training(int option){return option==BUILDING_CENTER_CREATEFARMER || option==BUILDING_RANGE_CREATE_BOWMAN ||
    option==BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN || option==BUILDING_ARMYCAMP_CREATE_CLUBMAN;}
double rangeOf(int sort){
    switch(sort){case AT_BOWMAN:return DIS_BOWMAN;case AT_COMPOSITE_BOWMAN:return DIS_COMPOSITE_BOWMAN;
    case AT_IMPROVED:return DIS_IMPROVEDBOWMAN1;case AT_CHARIOT_ARCHER:return DIS_CHARIOT_ARCHER;case AT_SLINGER:return DIS_SLINGER;
    case AT_STONE_THROWER:return DIS_STONE_THROWER;case AT_PRIEST:return DIS_PRIEST;default:return 1;}
}
double enemyRange(int sort){return rangeOf(sort)+(ranged(sort)?2:0);}
struct Cost {
    int wood,food,stone,gold,population;
    Cost(int w=0,int f=0,int s=0,int g=0,int p=0):wood(w),food(f),stone(s),gold(g),population(p){}
};
enum OrderType {Go,Use,Build,Project};
struct Order {
    OrderType kind=Go;
    int sn=-1,target=-1,option=-1,priority=0;
    Pos point;
    Cost cost;
    bool research=false;
};
struct Unit {
    Pos position,motionAnchor;
    Order last;
    int sent=-10000,retry=0,progress=0,harvest=0,value=-1,cargo=-1;
    int moved=0,nudged=-10000;
    int role=-1,job=-1,source=-1,assigned=-10000;
    int chopSN=-1,chopBlood=-1;bool chopping=false;
    int depot=-1,deliveryProgress=0,deliveryDistance=INT_MAX;
    int blocked=-1,blockedUntil=0;
    bool converted=false;
};
struct Pending {Order order;int frame=0;Pos origin;int cargo=-1;};
struct Tech {int level=0,building=-1,start=0;bool accepted=false,seen=false;};
struct Enemy {Pos point;int seen=0,sort=-1;};
enum Phase {Defend,Advance,Converting};
struct State {
    int frame=-1,economy=-10000,tactics=-10000,report=-10000,balance=-10000,buildTime=-10000;
    int baseSN=-1,factorySN=-1,issued=0,errors=0,unknown=0,resourceScout=-1,scoutFrame=-10000;
    Pos scoutGoal;
    int buildWish=-1,buildWood=0,buildWorker=-1,buildSite=-1,finish=0;
    int launchAvailable=0,launchHealthy=0,launchStrong=0,launchReason=0,fullSince=-1;
    bool thirdRaidSeen=false;
    int thirdRaidClearSince=-1;
    Pos base,factory,front,march,leash,assembly,screen,screenCore;
    int marchTime=-10000,marchProgress=0,batchTime=-1,clearSince=-1;
    double marchDistance=1e30;
    Phase phase=Defend;
    bool committed=false,announced=false,guardScreenActive=false,guardsScouted=false;
    bool towerWaveReleased=false;
    Pos towerRally;
    int towerRallyFactory=-1,towerReady=0,towerFront=0,towerSafe=0;
    int clearVeto=0;
    int screenSince=-1,screenFire=-1;
    bool screenDone=false;
    std::map<int,Unit> units;
    std::map<int,Pending> pending;
    std::map<int,Tech> tech;
    std::map<int,Enemy> enemies;
    std::set<int> seenEnemyTowers,removedEnemyTowers;
    std::map<int,int> blockedSites,visited,established,blockedFrontiers,blockedSurvey;
    std::set<int> dispatched,batch,stalledFarms;
    std::vector<Pos> granarySites,farmSites;
};

class Bot {
public:
    const tagInfo& info;
    State& s;
    int now,seconds,width,height;
    Cost money;
    Pos base,focus,rally;
    const tagArmy* hero=nullptr;
    bool homeDanger=false;
    std::vector<const tagFarmer*> workers;
    std::vector<const tagArmy*> army;
    std::map<int,const tagHuman*> humans;
    std::map<int,const tagBuilding*> buildings;
    std::map<int,const tagResource*> resources;
    std::map<int,Order> orders;
    std::vector<unsigned char> walkable;
    std::map<int,std::vector<int>> paths;

    Bot(const tagInfo& i,State& state);
    bool inside(int x,int y)const{return x>=0 && y>=0 && x<width && y<height;}
    bool land(int x,int y)const{return inside(x,y) && (*info.theMap)[x][y].type!=MAPPATTERN_UNKNOWN && (*info.theMap)[x][y].height>=0;}
    Pos clamp(Pos p)const{return Pos(std::max(.5,std::min(width-.5,p.x)),std::max(.5,std::min(height-.5,p.y)));}
    int count(int type,bool complete=false)const;
    int level(int action)const{auto t=s.tech.find(action);return t==s.tech.end()?0:t->second.level;}
    bool pending(int sn)const;
    bool affordable(Cost c)const;
    void debit(Cost c,int sign=1);
    bool propose(Order c);
    bool action(int sn,int target,int priority);
    bool move(int sn,Pos destination,int priority,double tolerance=1);
    bool project(const tagBuilding& b,int option,Cost cost,int priority,bool research=false);
    void reconcile();
    void grid();
    const std::vector<int>& field(Pos destination,bool safe=false);
    int walkDistance(Pos from,Pos to);
    Pos waypoint(Pos from,Pos goal,bool safe=false,int steps=7);
    Pos frontier(Pos from,Pos goal);
    double danger(Pos p,double margin=3)const;
    bool workerThreat(Pos p,int workerSN=-1)const;
    bool occupied(Pos p,double gap=.5)const;
    bool site(int type,Pos p)const;
    Pos safePoint(Pos p,Pos wanted,bool protectHero=false);
    bool constructing(const tagFarmer& f)const;
    int resourceRole(int sn)const;
    int sourceClass(int sn)const;
    const tagBuilding* depotFor(const tagFarmer& f,Pos origin);
    bool deposit(const tagFarmer& f,int priority=55);
    bool escapeDelivery(const tagFarmer& f,Pos goal,int priority);
    Pos armyCenter()const;
    bool towerBusy(const tagBuilding& tower)const;
    int remainingEnemyTowers()const{
        return std::max(0,std::max(5,int(s.seenEnemyTowers.size()))-int(s.removedEnemyTowers.size()));
    }
    bool guardsClear()const;
    void production();
    void construction();
    void economy();
    void targets(int desired[3])const;
    bool build(int type,Pos anchor,int priority,int attempt=0);
    bool reserveBuild(int type,Pos anchor,int priority);
    void tactics();
};

Bot::Bot(const tagInfo& i,State& state):info(i),s(state),now(i.GameFrame),
    seconds(int(i.GameFrame*TimePerFrame/1000)),width(int(i.theMap->size())),height(int((*i.theMap)[0].size())),
    money(i.Wood,i.Meat,i.Stone,i.Gold,std::max(0,int((i.Human_MaxNum-i.Human_Num)*2))) {
    for(const auto& b:i.buildings)if(b.Blood>0){
        buildings[b.SN]=&b;
        if(training(b.Project))money.population-=2;
        if(b.Type==BUILDING_CENTER){s.baseSN=b.SN;s.base=pos(b);}
        if(b.Type==BUILDING_GRANARY){
            Pos p(b.BlockDR,b.BlockUR);bool known=false;
            for(Pos q:s.granarySites)if(dist(p,q)<.1)known=true;
            if(!known)s.granarySites.push_back(p);
        }
        if(b.Type==BUILDING_FARM){
            Pos p(b.BlockDR,b.BlockUR);bool known=false;
            for(Pos q:s.farmSites)if(dist(p,q)<.1)known=true;
            if(!known)s.farmSites.push_back(p);
        }
    }
    for(const auto& f:i.farmers)if(f.Blood>0){humans[f.SN]=&f;if(f.FarmerSort==FARMERTYPE_FARMER)workers.push_back(&f);}
    for(const auto& a:i.armies)if(a.Blood>0){
        humans[a.SN]=&a;
        if(s.enemies.count(a.SN))s.units[a.SN].converted=true;
        if(a.Sort==AT_PRIEST){if(!hero || a.SN<hero->SN)hero=&a;}
        else if(a.Sort!=AT_SHIP)army.push_back(&a);

    }
    std::sort(workers.begin(),workers.end(),[](const tagFarmer* a,const tagFarmer* b){return a->SN<b->SN;});
    std::sort(army.begin(),army.end(),[](const tagArmy* a,const tagArmy* b){return a->SN<b->SN;});
    for(const auto& r:i.resources)if(r.Cnt>0)resources[r.SN]=&r;
    // All four supplied maps start with five enemy towers. Explored buildings
    // remain in enemy_buildings until destroyed or converted, even out of sight.
    // Count each confirmed removal once; undiscovered towers still count alive.
    std::set<int> liveEnemyTowers;
    for(const auto& b:i.enemy_buildings)if(b.Type==BUILDING_ARROWTOWER){
        s.seenEnemyTowers.insert(b.SN);
        if(b.Blood>0){liveEnemyTowers.insert(b.SN);s.removedEnemyTowers.erase(b.SN);}
    }
    for(int sn:s.seenEnemyTowers)if(!liveEnemyTowers.count(sn))s.removedEnemyTowers.insert(sn);
    for(const auto& b:i.enemy_buildings)if(b.Blood>0 && b.Type==BUILDING_SIEGE){s.factorySN=b.SN;s.factory=pos(b);s.leash=Pos(b.BlockDR,b.BlockUR);}
    base=s.base;focus=s.factorySN>=0?s.factory:clamp(Pos(width-base.x+(base.x<width*.5?9:-9),height-base.y+(base.y<height*.5?9:-9)));
    if(s.factorySN<0)s.leash=focus; // Provisional guard center until the factory is discovered.
    rally=base;double best=1e30;
    for(const auto& b:i.buildings)if(b.Type==BUILDING_ARROWTOWER && b.Blood>0 && b.Percent>=100 && dist(pos(b),base)<best){best=dist(pos(b),base);rally=pos(b);}
    for(const auto& a:i.enemy_armies)if(a.Blood>0 && a.Sort!=AT_SHIP){
        s.enemies[a.SN]=Enemy{pos(a),now,a.Sort};
        if(dist(pos(a),base)<23)homeDanger=true;
    }
    for(auto it=s.enemies.begin();it!=s.enemies.end();){
        bool covered=false,visible=false;
        for(const auto& a:i.enemy_armies)if(a.SN==it->first && a.Blood>0)visible=true;
        for(auto a:army)if(dist(pos(*a),it->second.point)<2.5)covered=true;
        if(humans.count(it->first) || now-it->second.seen>frames(25) || (!visible && covered))it=s.enemies.erase(it);else ++it;
    }
    for(const auto& entry:humans){
        auto& u=s.units[entry.first];const auto& h=*entry.second;
        int value=h.Blood;
        auto b=buildings.find(h.WorkObjectSN);
        if(b!=buildings.end())value+=b->second->Blood+b->second->Percent*100000;
        auto r=resources.find(h.WorkObjectSN);if(r!=resources.end())value+=r->second->Blood*100000;
        for(auto f:workers)if(f->SN==h.SN){
            // Damage or healing does not prove that a civilian job is advancing.
            value-=h.Blood;
            value+=f->Resource*1000;
            // Tree health is shared. Credit chopping only to a continuously
            // working villager already at the tree, not to a distant/idle claimant.
            if(r!=resources.end() && r->second->Type==RESOURCE_TREE){
                value-=r->second->Blood*100000;
                const double distance=reachDist(pos(*f),pos(*r->second));
                bool chopping=f->NowState==HUMAN_STATE_WORKING && distance<=1.6;
                if(chopping)for(auto other:workers)if(other->SN!=f->SN &&
                    other->WorkObjectSN==r->first && other->NowState==HUMAN_STATE_WORKING){
                    const double otherDistance=reachDist(pos(*other),pos(*r->second));
                    if(otherDistance<distance-.05 || (std::abs(otherDistance-distance)<=.05 && other->SN<f->SN))chopping=false;
                }
                if(chopping && u.chopping && u.chopSN==r->first && u.chopBlood>r->second->Blood)u.progress=now;
                u.chopSN=r->first;u.chopBlood=r->second->Blood;u.chopping=chopping;
            }else {u.chopSN=-1;u.chopBlood=-1;u.chopping=false;}
            // Other workers changing the same tree cannot make an idle villager productive.
            if(f->NowState==HUMAN_STATE_IDLE)value=f->Resource*1000;
            if(f->Resource!=u.cargo){u.harvest=now;if(f->Resource>u.cargo)u.blocked=-1;u.cargo=f->Resource;}
            int role=resourceRole(f->WorkObjectSN);
            if(role>=0){if(u.role!=role)u.assigned=now;u.role=role;u.job=f->WorkObjectSN;u.source=sourceClass(f->WorkObjectSN);}
        }
        // Native movement is only about .06 tiles per 40 ms frame. Keep an
        // anchor until displacement accumulates instead of resetting it every frame.
        if(dist(pos(h),u.motionAnchor)>.3){u.progress=now;u.moved=now;u.motionAnchor=pos(h);}
        if(value!=u.value)u.progress=now;
        u.value=value;u.position=pos(h);
        int key=int(u.position.x)*height+int(u.position.y);s.visited[key]=now;
    }
    reconcile();
    for(auto it=s.units.begin();it!=s.units.end();)if(!humans.count(it->first) && !buildings.count(it->first) && !pending(it->first)){
        s.dispatched.erase(it->first);s.batch.erase(it->first);it=s.units.erase(it);
    }else ++it;
    for(auto it=s.blockedSites.begin();it!=s.blockedSites.end();)if(it->second<=now)it=s.blockedSites.erase(it);else ++it;
    s.guardScreenActive=s.phase!=Defend && s.factorySN>=0 && !s.committed && !guardsClear();
}

int Bot::count(int type,bool complete)const{
    int n=0;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==type && (!complete || b.Percent>=100) &&
        (type!=BUILDING_FARM || (b.Cnt>0 && !s.stalledFarms.count(b.SN))))++n;
    return n;
}
bool Bot::pending(int sn)const{for(const auto& p:s.pending)if(p.second.order.sn==sn)return true;return false;}
bool Bot::affordable(Cost c)const{return (!c.wood || money.wood>=c.wood) && (!c.food || money.food>=c.food) &&
    (!c.stone || money.stone>=c.stone) && (!c.gold || money.gold>=c.gold) && (!c.population || money.population>=c.population);}
void Bot::debit(Cost c,int sign){money.wood-=sign*c.wood;money.food-=sign*c.food;money.stone-=sign*c.stone;money.gold-=sign*c.gold;money.population-=sign*c.population;}
bool Bot::propose(Order c){
    auto old=orders.find(c.sn);
    if(old!=orders.end() && old->second.priority>=c.priority)return false;
    if((pending(c.sn) || now<s.units[c.sn].retry) && c.priority<100)return false;
    auto h=humans.find(c.sn);
    if(h!=humans.end())for(auto f:workers)if(f->SN==c.sn && constructing(*f) &&
        !(c.kind==Use && c.target==f->WorkObjectSN) && c.priority<100)return false;
    if(old!=orders.end())debit(old->second.cost,-1);
    if(!affordable(c.cost)){if(old!=orders.end())debit(old->second.cost);return false;}
    debit(c.cost);orders[c.sn]=c;return true;
}
bool Bot::action(int sn,int target,int priority){
    if(target<0 || (!humans.count(sn) && !buildings.count(sn)))return false;
    if(target==s.factorySN && (!hero || sn!=hero->SN))return false;
    if(resources.count(target) && resourceRole(target)<0)for(auto f:workers)if(f->SN==sn)return false;
    auto& u=s.units[sn];auto h=humans.find(sn);
    if(h!=humans.end() && h->second->WorkObjectSN==target &&
        (h->second->NowState==HUMAN_STATE_ATTACKING ||
         (h->second->NowState!=HUMAN_STATE_IDLE && now-u.progress<frames(20)) || now-u.sent<frames(2)))return false;
    if(hero && sn==hero->SN && h->second->WorkObjectSN==target && hero->ConvertCooldown>0)return false;
    auto b=buildings.find(sn);if(b!=buildings.end() && b->second->Project==target)return false;
    Order o;o.kind=Use;o.sn=sn;o.target=target;o.priority=priority;return propose(o);
}
bool Bot::move(int sn,Pos destination,int priority,double tolerance){
    auto h=humans.find(sn);if(h==humans.end())return false;
    destination=clamp(destination);auto& u=s.units[sn];
    if(dist(pos(*h->second),destination)<tolerance)return false;
    const tagArmy* fighter=nullptr;for(auto a:army)if(a->SN==sn)fighter=a;
    if(fighter){
        auto busy=[&](Pos q){
            if(occupied(q,.05))return true;
            for(const auto& other:humans)if(other.first!=sn && int(pos(*other.second).x)==int(q.x) && int(pos(*other.second).y)==int(q.y))return true;
            for(const auto& other:orders)if(other.first!=sn && other.second.kind==Go &&
                int(other.second.point.x)==int(q.x) && int(other.second.point.y)==int(q.y))return true;
            return false;
        };
        if(busy(destination)){
            const auto& reachable=field(pos(*fighter));Pos selected=destination;double best=1e30;
            const Pos core=s.factorySN>=0?s.leash:focus;
            const bool outside=ranged(fighter->Sort) && s.screenSince>=0 && dist(destination,core)>=27;
            for(int dx=-2;dx<=2;++dx)for(int dy=-2;dy<=2;++dy){
                int x=int(destination.x)+dx,y=int(destination.y)+dy;
                if(!inside(x,y) || !walkable[x*height+y] || reachable[x*height+y]<0)continue;
                Pos q(x+.5,y+.5);if(busy(q) || (outside && dist(q,core)<27))continue;
                double score=dist(q,destination)+reachable[x*height+y]*.04+danger(q,0)*.3;
                if(score<best){best=score;selected=q;}
            }
            if(best<1e30)destination=selected;
        }
    }
    Pos current(h->second->DR0/BLOCKSIDELENGTH,h->second->UR0/BLOCKSIDELENGTH);
    for(auto f:workers)if(f->SN==sn && priority<100 && f->NowState==HUMAN_STATE_WALKING &&
        u.last.kind==Go && dist(pos(*f),u.last.point)>std::max(.3,tolerance) && now-u.moved<frames(6))return false;
    if(h->second->NowState==HUMAN_STATE_WALKING && dist(current,destination)<tolerance && now-u.moved<frames(12))return false;
    if(now-u.sent<frames(priority>=100?.25:.8))return false;
    Order o;o.kind=Go;o.sn=sn;o.point=destination;o.priority=priority;return propose(o);
}
bool Bot::project(const tagBuilding& b,int option,Cost cost,int priority,bool research){
    if(b.Blood<=0 || b.Percent<100 || b.Project!=ACT_NULL)return false;
    if(research && s.tech[option].building>=0)return false;
    if(research)for(const auto& q:orders)if(q.second.research && q.second.option==option)return false;
    Order o;o.kind=Project;o.sn=b.SN;o.option=option;o.cost=cost;o.priority=priority;o.research=research;
    return propose(o);
}
void Bot::reconcile(){
    for(auto& entry:s.tech){
        auto& t=entry.second;if(t.building<0)continue;
        auto b=buildings.find(t.building);
        if(b==buildings.end()){t.building=-1;t.seen=false;continue;}
        if(b->second->Project==entry.first){t.seen=true;t.accepted=true;}
        else if(t.seen && b->second->Project==ACT_NULL){++t.level;t.building=-1;t.seen=false;t.accepted=false;}
        else if(now-t.start>frames(5)){t.building=-1;t.accepted=false;}
    }
    for(auto it=s.pending.begin();it!=s.pending.end();){
        const auto& p=it->second;const auto& o=p.order;bool observed=false;
        auto h=humans.find(o.sn);auto b=buildings.find(o.sn);
        if(o.kind==Use && h!=humans.end())observed=h->second->WorkObjectSN==o.target && h->second->NowState!=HUMAN_STATE_IDLE;
        if(o.kind==Use && b!=buildings.end())observed=b->second->Project==o.target;
        if(o.kind==Go && h!=humans.end())observed=dist(pos(*h->second),o.point)<1 ||
            (h->second->NowState==HUMAN_STATE_WALKING && dist(Pos(h->second->DR0/BLOCKSIDELENGTH,h->second->UR0/BLOCKSIDELENGTH),o.point)<1);
        if(o.kind==Build)for(const auto& q:info.buildings)if(q.Blood>0 && q.Type==o.option && q.BlockDR==int(o.point.x) && q.BlockUR==int(o.point.y))observed=true;
        if(o.kind==Project && b!=buildings.end())observed=b->second->Project==o.option;
        for(auto f:workers)if(f->SN==o.sn && s.units[o.sn].depot==o.target && o.kind==Use)
            if(f->Resource<p.cargo || (f->NowState==HUMAN_STATE_WALKING && dist(pos(*f),p.origin)>.3))observed=true;
        auto ret=info.ins_ret.find(it->first);
        if(ret==info.ins_ret.end() && !observed && now-p.frame<frames(4)){debit(o.cost);++it;continue;}
        bool ok=observed || (ret!=info.ins_ret.end() && ret->second==ACTION_SUCCESS);
        if(!ok){
            if(ret==info.ins_ret.end())++s.unknown;else ++s.errors;
            if(o.kind==Build && ret!=info.ins_ret.end())std::cout<<"AI rejected t="<<seconds<<" build="<<o.option<<" site="<<int(o.point.x)<<','<<int(o.point.y)<<" actor="<<o.sn<<" code="<<ret->second<<std::endl;
            s.units[o.sn].retry=now+frames(o.kind==Project?2:1);
            if(o.kind==Build)s.blockedSites[int(o.point.x)*height+int(o.point.y)]=now+frames(30);
            if(o.research){s.tech[o.option].building=-1;s.tech[o.option].seen=false;}
        }else if(o.research)s.tech[o.option].accepted=true;
        it=s.pending.erase(it);
    }
}

void Bot::grid(){
    if(!walkable.empty())return;
    walkable.resize(width*height);
    for(int x=0;x<width;++x)for(int y=0;y<height;++y)walkable[x*height+y]=land(x,y);
    auto mark=[&](const tagBuilding& b){if(b.Blood>0)for(int x=b.BlockDR;x<b.BlockDR+sizeOf(b.Type);++x)
        for(int y=b.BlockUR;y<b.BlockUR+sizeOf(b.Type);++y)if(inside(x,y))walkable[x*height+y]=0;};
    for(const auto& b:info.buildings)mark(b);for(const auto& b:info.enemy_buildings)mark(b);
    for(const auto& r:info.resources)if(r.Type==RESOURCE_TREE || r.Type==RESOURCE_GOLD || r.Type==RESOURCE_STONE){
        int n=r.Type==RESOURCE_TREE?1:SIZELEN_SMALL;
        for(int x=r.BlockDR;x<r.BlockDR+n;++x)for(int y=r.BlockUR;y<r.BlockUR+n;++y)
            if(inside(x,y))walkable[x*height+y]=0;
    }
}
const std::vector<int>& Bot::field(Pos destination,bool safe){
    grid();destination=clamp(destination);int key=(int(destination.x)*height+int(destination.y))*2+int(safe);
    auto existing=paths.find(key);if(existing!=paths.end())return existing->second;
    std::vector<int> route(width*height,-1);std::queue<int> open;
    const tagBuilding* goalBuilding=nullptr;
    auto findGoal=[&](const tagBuilding& b){if(b.Blood>0 && destination.x>=b.BlockDR && destination.x<b.BlockDR+sizeOf(b.Type) &&
        destination.y>=b.BlockUR && destination.y<b.BlockUR+sizeOf(b.Type))goalBuilding=&b;};
    for(const auto& b:info.buildings)findGoal(b);for(const auto& b:info.enemy_buildings)findGoal(b);
    const tagResource* goalResource=nullptr;
    for(const auto& r:info.resources)if(r.Type==RESOURCE_TREE || r.Type==RESOURCE_GOLD || r.Type==RESOURCE_STONE){
        int n=r.Type==RESOURCE_TREE?1:SIZELEN_SMALL;
        if(destination.x>=r.BlockDR && destination.x<r.BlockDR+n && destination.y>=r.BlockUR && destination.y<r.BlockUR+n)goalResource=&r;
    }
    int seed=-1;double nearest=1e30;
    for(int x=std::max(0,int(destination.x)-3);x<std::min(width,int(destination.x)+4);++x)
        for(int y=std::max(0,int(destination.y)-3);y<std::min(height,int(destination.y)+4);++y)if(walkable[x*height+y]){
            Pos p(x+.5,y+.5);double d=dist(p,destination);
            if(safe && (danger(p,4)>0 || (s.guardScreenActive && dist(p,s.leash)<27)))continue;
            if(std::abs((*info.theMap)[x][y].height-(*info.theMap)[int(destination.x)][int(destination.y)].height)>1)continue;
            if(goalBuilding && x>=goalBuilding->BlockDR-1 && x<=goalBuilding->BlockDR+sizeOf(goalBuilding->Type) &&
                y>=goalBuilding->BlockUR-1 && y<=goalBuilding->BlockUR+sizeOf(goalBuilding->Type)){
                route[x*height+y]=0;open.push(x*height+y);continue;
            }
            if(goalResource){int n=goalResource->Type==RESOURCE_TREE?1:SIZELEN_SMALL;
                if(x>=goalResource->BlockDR-1 && x<=goalResource->BlockDR+n && y>=goalResource->BlockUR-1 && y<=goalResource->BlockUR+n){
                    route[x*height+y]=0;open.push(x*height+y);continue;
                }
            }
            if(d<nearest){nearest=d;seed=x*height+y;}
        }
    if(open.empty() && seed>=0 && !goalBuilding && !goalResource){route[seed]=0;open.push(seed);}
    while(!open.empty()){
        int index=open.front();open.pop();int x=index/height,y=index%height;
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){
            int nx=x+dx,ny=y+dy;if((!dx && !dy) || !inside(nx,ny))continue;
            int next=nx*height+ny;if(!walkable[next] || route[next]>=0)continue;
            if(dx && dy && (!walkable[nx*height+y] || !walkable[x*height+ny]))continue;
            if(std::abs((*info.theMap)[x][y].height-(*info.theMap)[nx][ny].height)>1)continue;
            Pos p(nx+.5,ny+.5);
            if(safe && (danger(p,4)>0 || (s.guardScreenActive && dist(p,s.leash)<27)))continue;
            route[next]=route[index]+1;open.push(next);
        }
    }
    return paths.emplace(key,std::move(route)).first->second;
}
int Bot::walkDistance(Pos from,Pos to){
    const auto& route=field(to);int best=INT_MAX;
    if(!inside(int(from.x),int(from.y)))return best;
    int cell=int(from.x)*height+int(from.y);
    if(walkable[cell])return route[cell]<0?INT_MAX:route[cell];
    if(!land(int(from.x),int(from.y)))return best;
    for(int dx=-2;dx<=2;++dx)for(int dy=-2;dy<=2;++dy){int x=int(from.x)+dx,y=int(from.y)+dy;
        if(inside(x,y) && route[x*height+y]>=0 && std::abs((*info.theMap)[x][y].height-(*info.theMap)[int(from.x)][int(from.y)].height)<=1)
            best=std::min(best,route[x*height+y]+std::max(std::abs(dx),std::abs(dy)));}
    return best;
}
Pos Bot::waypoint(Pos from,Pos goal,bool safe,int steps){
    const auto& route=field(goal,safe);int start=-1;double nearest=1e30;
    if(!inside(int(from.x),int(from.y)))return from;
    int cell=int(from.x)*height+int(from.y);
    if(walkable[cell] && route[cell]<0)return from;
    if(walkable[cell]){start=cell;nearest=dist(from,Pos(int(from.x)+.5,int(from.y)+.5));}
    for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int x=int(from.x)+dx,y=int(from.y)+dy;
        if(!inside(x,y) || route[x*height+y]<0 || (dx && dy && (!walkable[x*height+int(from.y)] || !walkable[int(from.x)*height+y])))continue;double d=dist(from,Pos(x+.5,y+.5));
        if(d<nearest){nearest=d;start=x*height+y;}}
    if(start<0)return from;
    if(start!=cell && !walkable[cell])return Pos(start/height+.5,start%height+.5);
    int current=start;
    for(int step=0;step<steps && route[current]>0;++step){int x=current/height,y=current%height,best=current;
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int nx=x+dx,ny=y+dy;
            if(!inside(nx,ny) || (!dx && !dy) || (dx && dy && (!walkable[nx*height+y] || !walkable[x*height+ny])))continue;
            int next=nx*height+ny;if(route[next]>=0 && route[next]<route[best] &&
                std::abs((*info.theMap)[x][y].height-(*info.theMap)[nx][ny].height)<=1)best=next;
        }
        if(best==current)break;current=best;
    }
    return Pos(current/height+.5,current%height+.5);
}
Pos Bot::frontier(Pos from,Pos goal){
    const auto& available=field(from);Pos best=from;double score=-1e30;
    // Unknown terrain is used only to estimate a route. Every actual destination
    // remains on reachable explored land, so known river banks are never mistaken for bridges.
    auto possible=[&](int x,int y){return inside(x,y) && (walkable[x*height+y] || (*info.theMap)[x][y].type==MAPPATTERN_UNKNOWN);};
    std::vector<int> estimate(width*height,-1);std::queue<int> open;
    goal=clamp(goal);int gx=int(goal.x),gy=int(goal.y);
    if(possible(gx,gy)){estimate[gx*height+gy]=0;open.push(gx*height+gy);}
    else for(int dx=-2;dx<=2;++dx)for(int dy=-2;dy<=2;++dy){int x=gx+dx,y=gy+dy;
        if(possible(x,y)){estimate[x*height+y]=0;open.push(x*height+y);}}
    while(!open.empty()){
        int cell=open.front();open.pop();int x=cell/height,y=cell%height;
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int nx=x+dx,ny=y+dy;
            if((!dx && !dy) || !possible(nx,ny) || estimate[nx*height+ny]>=0 ||
                (dx && dy && (!possible(nx,y) || !possible(x,ny))))continue;
            if(land(x,y) && land(nx,ny) && std::abs((*info.theMap)[x][y].height-(*info.theMap)[nx][ny].height)>1)continue;
            estimate[nx*height+ny]=estimate[cell]+1;open.push(nx*height+ny);
        }
    }
    for(int x=0;x<width;++x)for(int y=0;y<height;++y)if(available[x*height+y]>=0){
        int unseen=0;for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)
            if(inside(x+dx,y+dy) && (*info.theMap)[x+dx][y+dy].type==MAPPATTERN_UNKNOWN)++unseen;
        if(estimate[x*height+y]<0)continue;
        Pos p(x+.5,y+.5);double value=-available[x*height+y]-estimate[x*height+y]*1.06+unseen*.25;
        if(unseen==0 && dist(p,goal)>4)continue;
        bool blocked=false;
        for(const auto& old:s.blockedFrontiers)if(old.second>now && dist(p,Pos(old.first/height+.5,old.first%height+.5))<6)blocked=true;
        if(blocked)continue;
        auto visited=s.visited.find(x*height+y);if(visited!=s.visited.end() && now-visited->second<frames(45))value-=12;
        if(danger(p,1)>0)value-=20;
        if(value>score){score=value;best=p;}
    }
    return best;
}
double Bot::danger(Pos p,double margin)const{
    double risk=0;
    for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP)
        risk+=std::max(0.0,enemyRange(e.Sort)+margin-reachDist(p,pos(e)));
    for(const auto& b:info.enemy_buildings)if(b.Blood>0 && b.Type==BUILDING_ARROWTOWER)
        risk+=std::max(0.0,double(DIS_ARROWTOWER)+margin-reachDist(p,Pos(b.BlockDR,b.BlockUR)));
    return risk;
}
bool Bot::workerThreat(Pos p,int workerSN)const{
    // Enemy waves normally prefer the priest, but towers and fallback targets
    // can still attack farmers. Only an explicit target or splash impact dodges;
    // proximity alone never interrupts a worker's harvest or delivery.
    for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP){
        const double reach=enemyRange(e.Sort),distance=reachDist(p,pos(e));
        if(workerSN>=0 && e.WorkObjectSN==workerSN && distance<=reach+.25)return true;
        const bool engaged=e.NowState==HUMAN_STATE_ATTACKING || e.NowState==HUMAN_STATE_WORKING;
        if(engaged && e.Sort==AT_STONE_THROWER){
            Pos impact;bool known=false;
            auto h=humans.find(e.WorkObjectSN);auto b=buildings.find(e.WorkObjectSN);
            if(h!=humans.end()){impact=pos(*h->second);known=true;}
            else if(b!=buildings.end()){impact=pos(*b->second);known=true;}
            const double blast=double(Missile_Boulders_Range)+double(CRASHBOX_SINGLEOB)/double(BLOCKSIDELENGTH)+.15;
            if(known && reachDist(pos(e),impact)<=reach+1 && dist(p,impact)<=blast)return true;
        }
    }
    for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER){
        const int dx=tower.BlockDR-int(p.x),dy=tower.BlockUR-int(p.y),radius=int(DIS_ARROWTOWER);
        if(dx*dx+dy*dy>radius*radius)continue;
        if(workerSN>=0 && tower.Project==workerSN)return true;
    }
    return false;
}
bool Bot::occupied(Pos p,double gap)const{
    for(const auto& b:info.buildings)if(b.Blood>0 && p.x>b.BlockDR-gap && p.x<b.BlockDR+sizeOf(b.Type)+gap && p.y>b.BlockUR-gap && p.y<b.BlockUR+sizeOf(b.Type)+gap)return true;
    for(const auto& b:info.enemy_buildings)if(b.Blood>0 && p.x>b.BlockDR-gap && p.x<b.BlockDR+sizeOf(b.Type)+gap && p.y>b.BlockUR-gap && p.y<b.BlockUR+sizeOf(b.Type)+gap)return true;
    for(const auto& r:info.resources){
        if(r.Type==RESOURCE_TREE || r.Type==RESOURCE_GOLD || r.Type==RESOURCE_STONE){
            int n=r.Type==RESOURCE_TREE?1:SIZELEN_SMALL;
            if(p.x>r.BlockDR-gap && p.x<r.BlockDR+n+gap && p.y>r.BlockUR-gap && p.y<r.BlockUR+n+gap)return true;
        }else if(r.Cnt>0 && dist(p,pos(r))<.9+gap)return true;
    }
    return false;
}
bool Bot::site(int type,Pos p)const{
    int n=sizeOf(type),x=int(p.x),y=int(p.y);if(!inside(x,y) || !inside(x+n,y+n) || s.blockedSites.count(x*height+y))return false;
    int z=(*info.theMap)[x][y].height;
    for(int a=x;a<x+n;++a)for(int b=y;b<y+n;++b)
        if(!land(a,b) || (*info.theMap)[a][b].height!=z || occupied(Pos(a+.5,b+.5),.1))return false;
    for(const auto& h:humans){Pos q=pos(*h.second);if(q.x>=x && q.x<x+n && q.y>=y && q.y<y+n)return false;}
    for(const auto& h:info.enemy_armies)if(h.Blood>0){Pos q=pos(h);if(q.x>=x && q.x<x+n && q.y>=y && q.y<y+n)return false;}
    for(const auto& h:info.enemy_farmers)if(h.Blood>0){Pos q=pos(h);if(q.x>=x && q.x<x+n && q.y>=y && q.y<y+n)return false;}
    // Leave a real lane around every depot and field; a visible open footprint alone is insufficient.
    for(const auto& entry:buildings){const tagBuilding& q=*entry.second;int m=sizeOf(q.Type);
        if(x<q.BlockDR+m+1 && x+n+1>q.BlockDR && y<q.BlockUR+m+1 && y+n+1>q.BlockUR)return false;}
    for(const auto& q:orders)if(q.second.kind==Build){int m=sizeOf(q.second.option);
        if(x<int(q.second.point.x)+m+1 && x+n+1>int(q.second.point.x) && y<int(q.second.point.y)+m+1 && y+n+1>int(q.second.point.y))return false;}
    for(const auto& q:s.pending)if(q.second.order.kind==Build){const auto& o=q.second.order;int m=sizeOf(o.option);
        if(x<int(o.point.x)+m+1 && x+n+1>int(o.point.x) && y<int(o.point.y)+m+1 && y+n+1>int(o.point.y))return false;}
    return danger(Pos(x+n*.5,y+n*.5),3)==0;
}
Pos Bot::safePoint(Pos p,Pos wanted,bool protectHero){
    grid();const auto& accessible=field(p);Pos best=p;double score=1e30;
    for(int dx=-8;dx<=8;++dx)for(int dy=-8;dy<=8;++dy){Pos q=clamp(Pos(int(p.x)+dx+.5,int(p.y)+dy+.5));
        if(!walkable[int(q.x)*height+int(q.y)] || accessible[int(q.x)*height+int(q.y)]<0)continue;
        if(protectHero){
            bool body=false;for(const auto& h:humans)if((!hero || h.first!=hero->SN) &&
                int(pos(*h.second).x)==int(q.x) && int(pos(*h.second).y)==int(q.y))body=true;
            if(body)continue;
        }
        if(protectHero && s.guardScreenActive && dist(q,s.leash)<27)continue;
        double value=danger(q,protectHero?5:3)*15+dist(q,wanted)+dist(q,p)*.25;
        if(value<score){score=value;best=q;}
    }
    return best;
}
bool Bot::constructing(const tagFarmer& f)const{
    auto b=buildings.find(f.WorkObjectSN);if(b==buildings.end() || b->second->Percent>=100)return false;
    auto u=s.units.find(f.SN);
    return u!=s.units.end() && now-u->second.progress<frames(30);
}
int Bot::resourceRole(int sn)const{
    auto b=buildings.find(sn);if(b!=buildings.end() && b->second->Type==BUILDING_FARM && b->second->Percent>=100 && b->second->Cnt>0)return 1;
    auto r=resources.find(sn);if(r==resources.end())return -1;
    if(r->second->Type==RESOURCE_TREE)return 0;if(r->second->Type==RESOURCE_GOLD)return 2;
    if(r->second->Type==RESOURCE_BUSH)return 1;
    return -1;
}
int Bot::sourceClass(int sn)const{
    auto b=buildings.find(sn);if(b!=buildings.end() && b->second->Type==BUILDING_FARM)return 4;
    auto r=resources.find(sn);if(r==resources.end())return -1;
    switch(r->second->Type){case RESOURCE_TREE:return 0;case RESOURCE_GOLD:case RESOURCE_STONE:return 1;case RESOURCE_BUSH:return 2;default:return -1;}
}
const tagBuilding* Bot::depotFor(const tagFarmer& f,Pos origin){
    const tagBuilding* best=nullptr;double score=1e30;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Percent>=100 &&
        (b.Type==BUILDING_CENTER || (b.Type==BUILDING_GRANARY && f.ResourceSort==HUMAN_GRANARYFOOD) ||
         (b.Type==BUILDING_STOCK && (f.ResourceSort==HUMAN_WOOD || f.ResourceSort==HUMAN_GOLD || f.ResourceSort==HUMAN_STONE || f.ResourceSort==HUMAN_STOCKFOOD)))){
        if(b.SN==s.units[f.SN].blocked && now<s.units[f.SN].blockedUntil)continue;
        int walk=walkDistance(origin,pos(b));if(walk==INT_MAX)continue;
        int crowd=0;for(auto other:workers)if(other->SN!=f.SN && other->Resource>0 && dist(pos(*other),pos(b))<4)++crowd;
        double value=walk+crowd*.7+danger(pos(b))*10;if(value<score){score=value;best=&b;}
    }
    return best;
}
bool Bot::escapeDelivery(const tagFarmer& f,Pos goal,int priority){
    grid();const auto& route=field(goal);int x=int(pos(f).x),y=int(pos(f).y);
    auto occupiedBy=[&](int xx,int yy,int self){
        for(const auto& h:humans)if(h.first!=self && int(pos(*h.second).x)==xx && int(pos(*h.second).y)==yy)return h.first;
        return -1;
    };
    auto open=[&](int xx,int yy,int self){return inside(xx,yy) && walkable[xx*height+yy] && occupiedBy(xx,yy,self)<0;};
    auto crossing=[&](Pos p,Pos a,Pos b){
        double dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
        double t=len>0?((p.x-a.x)*dx+(p.y-a.y)*dy)/len:0;
        t=std::max(0.0,std::min(1.0,t));return dist(p,Pos(a.x+t*dx,a.y+t*dy))<.36;
    };
    std::set<int> blockers;
    Pos choice=pos(f);double best=1e30;
    for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int xx=x+dx,yy=y+dy;
        if((!dx && !dy) || !inside(xx,yy) || !walkable[xx*height+yy] || route[xx*height+yy]<0)continue;
        if(std::abs((*info.theMap)[x][y].height-(*info.theMap)[xx][yy].height)>1)continue;
        if(dx && dy && (!walkable[xx*height+y] || !walkable[x*height+yy]))continue;
        Pos p(xx+.5,yy+.5);if(danger(p,1)>0)continue;
        bool occupied=false;
        for(const auto& h:humans)if(h.first!=f.SN &&
            ((int(pos(*h.second).x)==xx && int(pos(*h.second).y)==yy) || crossing(pos(*h.second),pos(f),p))){
            occupied=true;blockers.insert(h.first);
        }
        if(occupied)continue;
        if(dx && dy && (!open(xx,y,f.SN) || !open(x,yy,f.SN)))continue;
        double score=route[xx*height+yy]+dist(pos(f),p)*.2;
        if(dist(s.units[f.SN].last.point,p)<.1 && now-s.units[f.SN].progress>frames(3))score+=8;
        if(score<best){best=score;choice=p;}
    }
    if(best<1e30 && move(f.SN,choice,priority,.2))return true;
    // Native bodies can share a cell. A stationary lumberjack in that same cell
    // can block the only exit just as completely as a carrier in the next cell.
    for(int sn:blockers){const tagFarmer* helper=nullptr;
        for(auto worker:workers)if(worker->SN==sn)helper=worker;
        if(!helper || constructing(*helper) || pending(sn) || orders.count(sn))continue;
        auto work=buildings.find(helper->WorkObjectSN);
        if(work!=buildings.end() && work->second->Percent<100)continue;
        Unit& u=s.units[sn];
        if((helper->Resource==0 && helper->NowState!=HUMAN_STATE_IDLE && resourceRole(helper->WorkObjectSN)!=0) ||
            now-u.sent<frames(2) || now-u.moved<frames(4) || now-u.nudged<frames(15))continue;
        if(helper->NowState==HUMAN_STATE_WORKING && resourceRole(helper->WorkObjectSN)==0 &&
            helper->Resource>0 && now-u.harvest<frames(6) && dist(pos(*helper),pos(f))>.6)continue;
        int sx=int(pos(*helper).x),sy=int(pos(*helper).y);
        std::map<int,int> steps;std::queue<int> cells;int start=sx*height+sy;
        steps[start]=0;cells.push(start);Pos next=pos(*helper);double scoreBest=1e30;
        while(!cells.empty()){
            int cell=cells.front();cells.pop();int xx=cell/height,yy=cell%height,depth=steps[cell];
            Pos p(xx+.5,yy+.5);
            if(depth>0 && dist(p,pos(f))>1.4 && route[cell]>=0 && danger(p,1)==0){
                double score=depth+route[cell]*.12;
                if(score<scoreBest){scoreBest=score;next=p;}
            }
            if(depth>=3)continue;
            for(int a=-1;a<=1;++a)for(int b=-1;b<=1;++b){int nx=xx+a,ny=yy+b,index=nx*height+ny;
                if((!a && !b) || !open(nx,ny,sn) || steps.count(index) || route[index]<0)continue;
                if(a && b && (!open(nx,yy,sn) || !open(xx,ny,sn)))continue;
                if(std::abs((*info.theMap)[xx][yy].height-(*info.theMap)[nx][ny].height)>1)continue;
                bool collision=false;
                Pos from=depth?Pos(xx+.5,yy+.5):pos(*helper),to(nx+.5,ny+.5);
                if(danger(to,1)>0)continue;
                for(const auto& h:humans)if(h.first!=sn && crossing(pos(*h.second),from,to)){
                    Pos body=pos(*h.second);
                    // Existing overlap must still be able to separate in the outward direction.
                    bool separating=dist(body,from)<.36 && dist(body,to)>dist(body,from)+.1 &&
                        (body.x-from.x)*(to.x-from.x)+(body.y-from.y)*(to.y-from.y)<=0;
                    if(!separating)collision=true;
                }
                if(collision)continue;steps[index]=depth+1;cells.push(index);
            }
        }
        if(scoreBest<1e30 && move(sn,next,std::max(priority,58),.2)){u.nudged=now;return true;}
    }
    return false;
}
bool Bot::deposit(const tagFarmer& f,int priority){
    Unit& u=s.units[f.SN];if(f.Resource<=0){u.depot=-1;return false;}
    // Gather exports WORKING even during the engine's walk to a depot. Preserve
    // that round trip instead of replacing it with a fresh HumanMove relation.
    if(f.NowState==HUMAN_STATE_WORKING && now-u.moved<frames(6)){
        Pos destination(f.DR0/BLOCKSIDELENGTH,f.UR0/BLOCKSIDELENGTH),source=pos(f);
        // The final native haul survives resource exhaustion. Cnt0 sources are
        // visible collision objects, but deliberately absent from assignable resources.
        const tagResource* nativeSource=nullptr;
        for(const auto& r:info.resources)if(r.SN==f.WorkObjectSN){nativeSource=&r;break;}
        auto b=buildings.find(f.WorkObjectSN);
        const bool farmSource=b!=buildings.end() && b->second->Type==BUILDING_FARM && b->second->Percent>=100;
        if(nativeSource)source=pos(*nativeSource);else if(farmSource)source=pos(*b->second);
        if((nativeSource || farmSource) && dist(destination,source)>1){
            const tagBuilding* nativeDepot=nullptr;double nearest=1e30;
            for(const auto& depot:info.buildings)if(depot.Blood>0 && depot.Percent>=100 &&
                (depot.Type==BUILDING_CENTER || (depot.Type==BUILDING_GRANARY && f.ResourceSort==HUMAN_GRANARYFOOD) ||
                 (depot.Type==BUILDING_STOCK && (f.ResourceSort==HUMAN_WOOD || f.ResourceSort==HUMAN_GOLD ||
                  f.ResourceSort==HUMAN_STONE || f.ResourceSort==HUMAN_STOCKFOOD)))){
                double d=reachDist(destination,pos(depot));
                if(d<=sizeOf(depot.Type)*.5+.6 && d<nearest){nearest=d;nativeDepot=&depot;}
            }
            if(nativeDepot){
                u.depot=nativeDepot->SN;u.deliveryDistance=walkDistance(pos(f),pos(*nativeDepot));u.deliveryProgress=now;
                return true;
            }
        }
    }
    auto old=buildings.find(u.depot);
    if(old!=buildings.end() && old->second->Percent>=100){
        int walk=walkDistance(pos(f),pos(*old->second));
        if(walk<u.deliveryDistance){u.deliveryDistance=walk;u.deliveryProgress=now;}
        if(pending(f.SN) || now-u.sent<frames(2))return true;
        if(reachDist(pos(f),pos(*old->second))>sizeOf(old->second->Type)*.5+.28 && now-u.moved>=frames(6) &&
            escapeDelivery(f,pos(*old->second),priority))return true;
        if(u.last.kind==Use && u.last.target==u.depot && f.NowState!=HUMAN_STATE_IDLE && now-u.deliveryProgress<frames(18))return true;
        if(u.last.kind==Go && f.NowState==HUMAN_STATE_WALKING && dist(pos(f),u.last.point)>.3 && now-u.deliveryProgress<frames(18))return true;
        if((u.last.kind==Use && u.last.target==u.depot && f.NowState==HUMAN_STATE_IDLE) || now-u.deliveryProgress>=frames(18)){
            // Acknowledgement is not an actual delivery. Try another accessible entrance/depot.
            u.blocked=u.depot;u.blockedUntil=now+frames(30);old=buildings.end();
        }
    }
    const tagBuilding* target=old!=buildings.end()?old->second:depotFor(f,pos(f));
    if(!target){u.blocked=-1;target=depotFor(f,pos(f));}if(!target)return false;
    if(u.depot!=target->SN){u.depot=target->SN;u.deliveryDistance=walkDistance(pos(f),pos(*target));u.deliveryProgress=now;}
    const double close=sizeOf(target->Type)*.5+.28;
    if(reachDist(pos(f),pos(*target))>close){
        // Approach on known land first. The last fraction of a tile matters for unloading.
        grid();Pos entrance=pos(f);double best=1e30;int n=sizeOf(target->Type);
        for(int x=target->BlockDR-1;x<=target->BlockDR+n;++x)for(int y=target->BlockUR-1;y<=target->BlockUR+n;++y){
            if(!inside(x,y) || !walkable[x*height+y])continue;
            Pos q(x+.5,y+.5),center=pos(*target);
            if(x<target->BlockDR)q.x=target->BlockDR-.24;
            if(x>=target->BlockDR+n)q.x=target->BlockDR+n+.24;
            if(y<target->BlockUR)q.y=target->BlockUR-.24;
            if(y>=target->BlockUR+n)q.y=target->BlockUR+n+.24;
            if(reachDist(q,center)>close)continue;
            int walk=walkDistance(pos(f),q);if(walk==INT_MAX)continue;
            double score=walk;
            for(auto other:workers)if(other->SN!=f.SN && dist(pos(*other),q)<1)score+=2;
            if(score<best){best=score;entrance=q;}
        }
        if(best<1e30){Pos step=dist(pos(f),entrance)<2?entrance:waypoint(pos(f),entrance);
            move(f.SN,step,priority,.1);return true;}
    }
    action(f.SN,target->SN,priority);return true;
}
// Economy and combat methods follow. They share this one command budget and unit history.

Cost buildingCost(int type){
    switch(type){case BUILDING_CENTER:return Cost(BUILD_CENTER_WOOD);case BUILDING_HOME:return Cost(BUILD_HOUSE_WOOD);case BUILDING_ARMYCAMP:return Cost(BUILD_ARMYCAMP_WOOD);
    case BUILDING_RANGE:return Cost(BUILD_RANGE_WOOD);case BUILDING_MARKET:return Cost(BUILD_MARKET_WOOD);
    case BUILDING_GRANARY:return Cost(BUILD_GRANARY_WOOD);case BUILDING_STOCK:return Cost(BUILD_STOCK_WOOD);
    case BUILDING_FARM:return Cost(BUILD_FARM_WOOD);case BUILDING_ARROWTOWER:return Cost(0,0,BUILD_ARROWTOWER_STONE);
    default:return Cost(INT_MAX);}
}
int resourceCapacity(const tagResource& r){return r.Type==RESOURCE_TREE?1:(r.Type==RESOURCE_GOLD?4:(r.Type==RESOURCE_BUSH?2:0));}

int foodDropDistance(Bot& bot,Pos origin,int sort){
    int best=INT_MAX;
    for(const auto& b:bot.info.buildings)if(b.Blood>0 && b.Percent>=100 &&
        (b.Type==BUILDING_CENTER || (b.Type==BUILDING_GRANARY && sort==HUMAN_GRANARYFOOD) ||
         (b.Type==BUILDING_STOCK && sort==HUMAN_STOCKFOOD)))
        best=std::min(best,bot.walkDistance(origin,pos(b)));
    return best;
}

const int goldHaulLimit=6;
int goldDropDistance(Bot& bot,Pos origin){return foodDropDistance(bot,origin,HUMAN_STOCKFOOD);}
std::vector<const tagResource*> localGoldMines(Bot& bot){
    std::vector<const tagResource*> result;
    for(const auto& r:bot.info.resources)if(r.Type==RESOURCE_GOLD && r.Cnt>0 &&
        dist(pos(r),bot.base)<32 && bot.danger(pos(r),3)==0 && bot.walkDistance(bot.base,pos(r))<=36)result.push_back(&r);
    std::sort(result.begin(),result.end(),[&](const tagResource* a,const tagResource* b){
        return bot.walkDistance(bot.base,pos(*a))-a->Cnt*.005<bot.walkDistance(bot.base,pos(*b))-b->Cnt*.005;
    });
    return result;
}
int readyGoldCapacity(Bot& bot){
    int capacity=0;
    for(auto mine:localGoldMines(bot))if(goldDropDistance(bot,pos(*mine))<=goldHaulLimit)capacity+=resourceCapacity(*mine);
    return capacity;
}

int farmDropDistance(Bot& bot,Pos topLeft,int n){
    const auto& access=bot.field(bot.base);int worst=-1;
    for(int x=int(topLeft.x)-1;x<=int(topLeft.x)+n;++x)for(int y=int(topLeft.y)-1;y<=int(topLeft.y)+n;++y)
        if(bot.inside(x,y) && access[x*bot.height+y]>=0 &&
            (x<int(topLeft.x) || x>=int(topLeft.x)+n || y<int(topLeft.y) || y>=int(topLeft.y)+n)){
            int nearest=INT_MAX;
            for(const auto& b:bot.info.buildings)if(b.Blood>0 && b.Percent>=100 &&
                (b.Type==BUILDING_CENTER || b.Type==BUILDING_GRANARY)){
                int distance=bot.field(pos(b))[x*bot.height+y];if(distance>=0)nearest=std::min(nearest,distance);
            }
            worst=std::max(worst,nearest);
        }
    return worst<0?INT_MAX:worst;
}

struct FoodSupply {int farms=0,readyFarms=0,freeFarms=0,natural=0,berries=0,berrySlots=0,productive=0;};
FoodSupply foodSupply(Bot& bot){
    FoodSupply result;std::set<int> jobs;
    for(auto f:bot.workers){const auto& u=bot.s.units[f->SN];
        if(f->NowState!=HUMAN_STATE_IDLE && bot.resourceRole(f->WorkObjectSN)==1 && bot.now-u.progress<frames(30))jobs.insert(f->WorkObjectSN);
        else if(u.depot>=0 && f->Resource>0 && bot.resourceRole(u.job)==1)jobs.insert(u.job);
    }
    for(const auto& b:bot.info.buildings)if(b.Blood>0 && b.Type==BUILDING_FARM && !bot.s.stalledFarms.count(b.SN)){
        if(b.Percent<100){++result.farms;continue;}
        if(b.Cnt<60)continue;
        if(bot.workerThreat(pos(b)) || farmDropDistance(bot,Pos(b.BlockDR,b.BlockUR),sizeOf(b.Type))>8)continue;
        ++result.farms;++result.readyFarms;if(!jobs.count(b.SN))++result.freeFarms;
    }
    for(auto f:bot.workers){const auto& u=bot.s.units[f->SN];
        if(bot.constructing(*f) || bot.pending(f->SN) || bot.resourceRole(u.job)!=1 || bot.now-u.harvest>=frames(30) || f->Resource<=0)continue;
        auto r=bot.resources.find(u.job);auto b=bot.buildings.find(u.job);
        if(r!=bot.resources.end() && r->second->Type==RESOURCE_BUSH && !bot.workerThreat(pos(*f),f->SN) &&
            foodDropDistance(bot,pos(*r->second),HUMAN_GRANARYFOOD)<=10){
            ++result.natural;++result.productive;++result.berries;
        }else if(b!=bot.buildings.end() && b->second->Type==BUILDING_FARM && b->second->Percent>=100 && b->second->Cnt>0 &&
            !bot.workerThreat(pos(*f),f->SN) && farmDropDistance(bot,Pos(b->second->BlockDR,b->second->BlockUR),sizeOf(b->second->Type))<=8)++result.productive;
    }
    for(const auto& r:bot.info.resources)if(r.Type==RESOURCE_BUSH && r.Cnt>0 && dist(pos(r),bot.base)<=30 &&
        bot.danger(pos(r),3)==0 && bot.walkDistance(bot.base,pos(r))<=36 &&
        foodDropDistance(bot,pos(r),HUMAN_GRANARYFOOD)<=10)result.berrySlots+=resourceCapacity(r);
    return result;
}

int rangeGoal(Bot& bot,const FoodSupply& supply,bool aging){
    const bool bronze=bot.info.civilizationStage>=CIVILIZATION_BRONZEAGE;
    const int foodSlots=supply.readyFarms+std::min(4,supply.berries);
    int wanted=1;
    if(bronze){
        wanted=foodSlots>=12?5:(foodSlots>=8?4:(foodSlots>=5?3:(foodSlots>=3?2:1)));
        if(bot.info.Meat>=200 && bot.info.Gold>=100)wanted=std::max(wanted,2);
        if(bot.info.Meat>=400 && bot.info.Gold>=200)wanted=std::max(wanted,3);
        if(bot.info.Meat>=600 && bot.info.Gold>=300)wanted=std::max(wanted,4);
    }else if(bot.info.civilizationStage>=CIVILIZATION_TOOLAGE){
        if(aging)wanted=foodSlots>=6?3:(foodSlots>=4?2:1);
        else if(supply.readyFarms>=4 && foodSlots>=6 && bot.workers.size()>=14)wanted=2;
    }
    if((bronze || aging) && bot.money.wood>=400 && bot.info.Meat>=200 && bot.info.Gold>=100)
        wanted=std::max(wanted,std::min(6,bot.count(BUILDING_RANGE)+1));
    return std::max(wanted,std::min(6,bot.s.established[BUILDING_RANGE]));
}

int workerGoal(Bot& bot,const FoodSupply& supply,bool aging){
    if(bot.info.civilizationStage<CIVILIZATION_TOOLAGE)return 12;
    if(bot.info.civilizationStage<CIVILIZATION_BRONZEAGE){
        return !aging && bot.info.Meat<650 && supply.readyFarms>=8 && supply.freeFarms>0?20:18;
    }
    if(!bot.level(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW))return 18;
    int wanted=20;
    // Expand a real food district, not a fixed civilian quota. Empty plots or
    // plots under construction must exist before adding their operators.
    if((bot.s.phase!=Defend || bot.army.size()>=12) &&
        (supply.freeFarms>0 || supply.farms>supply.readyFarms)){
        int idle=0;for(auto f:bot.workers)if(f->NowState==HUMAN_STATE_IDLE && f->Resource==0 &&
            !bot.constructing(*f) && !bot.pending(f->SN) && !bot.orders.count(f->SN))++idle;
        if(idle<=1)wanted=std::min(28,std::max(20,supply.readyFarms+std::min(4,supply.berrySlots)+14));
    }
    return wanted;
}

void Bot::targets(int desired[3])const{
    int n=int(workers.size());bool bronze=info.civilizationStage>=CIVILIZATION_BRONZEAGE,aging=false;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_CENTER && b.Project==BUILDING_CENTER_UPGRADE)aging=true;
    int wood=n<12?std::max(3,n/3):7,gold=aging && info.civilizationStage==CIVILIZATION_TOOLAGE?4:0;
    // Begin a small gold reserve while food is accumulating for Bronze Age.
    // Waiting until the age finishes also postpones the mine depot and bow production.
    if(!bronze && !aging && n>=16 && info.Gold<200 &&
        count(BUILDING_RANGE,true) && count(BUILDING_MARKET,true))gold=info.Meat>=450?3:1;
    if(bronze){
        wood=count(BUILDING_FARM,true)<9?8:6;
        if(info.Wood>450)wood=3;
        gold=info.Gold>350?2:(info.Gold<240?6:4);
        if(info.Meat>=360 && info.Gold<200)gold=info.Meat>=800?8:6;
        if(info.Gold>250 && info.Meat<200){gold=1;if(info.Wood>=180)wood=4;}
    }else if(info.Wood>300 || (count(BUILDING_FARM,true)>=8 && info.Wood>=150))wood=5;
    int foodFloor=n>=16?10:std::min(5,n);
    if(bronze && n>=20 && info.Meat<600){
        const int lines=std::min(5,count(BUILDING_RANGE));
        foodFloor=std::min(n-6,std::max(12,lines*3+3));
    }
    if(n>=16 && (bronze || aging) && info.Meat>=360 && info.Gold<200)foodFloor=8;
    // Keep timber arriving for the mine depot and ranges while easing a gold shortage.
    gold=std::min(gold,std::max(0,n-foodFloor-4));
    desired[0]=std::min(wood,std::max(0,n-gold-foodFloor));
    desired[2]=std::min(gold,std::max(0,n-desired[0]-foodFloor));
    desired[1]=std::max(0,n-desired[0]-desired[2]);
}

void Bot::production(){
    bool bronze=info.civilizationStage>=CIVILIZATION_BRONZEAGE;
    const FoodSupply supply=foodSupply(*this);
    const bool foodEmergency=info.Meat<200 && supply.productive<4;
    int bows=0,melee=0,ranges=count(BUILDING_RANGE,true),farmCount=count(BUILDING_FARM,true);
    for(auto a:army){if(ranged(a->Sort))++bows;else if(a->Sort!=AT_STONE_THROWER)++melee;}
    bool ageReady=info.civilizationStage==CIVILIZATION_STONEAGE?
        int(count(BUILDING_ARMYCAMP,true)>0)+int(count(BUILDING_GRANARY,true)>0)+int(count(BUILDING_STOCK,true)>0)>=2:
        count(BUILDING_RANGE,true) && count(BUILDING_MARKET,true);
    int ageFood=info.civilizationStage==CIVILIZATION_STONEAGE?BUILDING_CENTER_UPGRADE_TOOLAGE_FOOD:BUILDING_CENTER_UPGRADE_BRONZEAGE_FOOD;
    Cost ageCost(0,ageFood,0,info.civilizationStage==CIVILIZATION_STONEAGE?0:BUILDING_CENTER_UPGRADE_BRONZEAGE_GOLD);
    bool aging=false;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_CENTER && b.Project==BUILDING_CENTER_UPGRADE)aging=true;
    const int wantedWorkers=workerGoal(*this,supply,aging);
    for(const auto& b:info.buildings)if(b.Type==BUILDING_CENTER && b.Blood>0){
        if(!bronze && ageReady && affordable(ageCost)){
            if(!project(b,BUILDING_CENTER_UPGRADE,ageCost,96) && b.Project==BUILDING_CENTER_CREATEFARMER && affordable(ageCost))debit(ageCost);
        }else if(int(workers.size())<std::min(20,wantedWorkers) && (!bronze || level(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW))){
            project(b,BUILDING_CENTER_CREATEFARMER,Cost(0,BUILDING_CENTER_CREATEFARMER_FOOD,0,0,2),80);
        }
    }
    if(bronze && !level(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW)){
        bool unlocking=false;
        for(const auto& b:info.buildings)if(b.Type==BUILDING_RANGE &&
            project(b,BUILDING_RANGE_UPGRADE_COMPOSITE_BOW,Cost(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_WOOD,BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_FOOD),94,true)){unlocking=true;break;}
        // The unlock and a single next field must survive optional investments.
        if(!unlocking && s.tech[BUILDING_RANGE_UPGRADE_COMPOSITE_BOW].building<0 &&
            (!foodEmergency || money.food>=BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_FOOD)){
            money.wood=std::max(0,money.wood-BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_WOOD);
            money.food=std::max(0,money.food-BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_FOOD);
        }
    }
    // Protect the next age's immediate unlock while the center is upgrading.
    // This reserve precedes extra basic troops, optional technologies and ranges.
    if(aging){
        money.wood=std::max(0,money.wood-BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_WOOD);
        money.food=std::max(0,money.food-BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_FOOD);
    }
    for(const auto& b:info.buildings)if(b.Type==BUILDING_GRANARY && info.civilizationStage>=CIVILIZATION_TOOLAGE &&
        level(BUILDING_GRANARY_ARROWTOWER)==0 && count(BUILDING_ARROWTOWER)<1){
        if(project(b,BUILDING_GRANARY_ARROWTOWER,Cost(0,BUILDING_GRANARY_ARROWTOWER_FOOD),95,true))break;
    }
    // Clubs are an opening raid response only. Do not spend the composite
    // army's food and population replacing dead clubs during the counterattack.
    const int meleeGoal=!bronze && s.phase==Defend && homeDanger?2:0;
    int committedMelee=melee;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_ARMYCAMP){
        if(b.Project==BUILDING_ARMYCAMP_CREATE_CLUBMAN)++committedMelee;
        else for(const auto& pending:s.pending)if(pending.second.order.kind==Project &&
            pending.second.order.sn==b.SN && pending.second.order.option==BUILDING_ARMYCAMP_CREATE_CLUBMAN){++committedMelee;break;}
    }
    for(const auto& b:info.buildings)if(b.Type==BUILDING_ARMYCAMP && committedMelee<meleeGoal){
        if(project(b,BUILDING_ARMYCAMP_CREATE_CLUBMAN,Cost(0,BUILDING_ARMYCAMP_CREATE_CLUBMAN_FOOD,0,0,2),82))break;
    }
    // The first wood upgrade pays for fields and depots and adds bow range.
    // Reserve its wood once its food is available; waiting for spare wood never finishes it.
    if((bronze || aging || (info.civilizationStage==CIVILIZATION_TOOLAGE && info.Meat>=550 && farmCount>=3)) &&
        !foodEmergency && !level(BUILDING_MARKET_WOOD_UPGRADE) && int(workers.size())>=14 && bows>=2 &&
        (!bronze || level(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW))){
        Cost c(BUILDING_MARKET_WOOD_UPGRADE_WOOD,BUILDING_MARKET_WOOD_UPGRADE_FOOD);
        bool started=false;
        for(const auto& b:info.buildings)if(b.Type==BUILDING_MARKET && money.food>=c.food+40 && project(b,BUILDING_MARKET_WOOD_UPGRADE,c,91,true)){started=true;break;}
        if(!started && s.tech[BUILDING_MARKET_WOOD_UPGRADE].building<0 && count(BUILDING_MARKET,true)){
            money.wood=std::max(0,money.wood-c.wood);
            money.food=std::max(0,money.food-c.food);
        }
    }
    int armor=level(BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER);
    if((bronze || aging) && info.civilizationStage>=CIVILIZATION_TOOLAGE && armor<(bronze?2:1) && bows>=(armor?10:6) && (!armor || level(BUILDING_MARKET_WOOD_UPGRADE)>=2) &&
        (!bronze || level(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW))){
        Cost c=armor?Cost(0,BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER_2_FOOD,0,BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER_2_GOLD):Cost(0,BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER_FOOD);
        bool started=false;
        for(const auto& b:info.buildings)if(b.Type==BUILDING_STOCK && project(b,BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER,c,86,true)){started=true;break;}
        // Six bows need the first armor before the raid and the first march.
        // Stable field capacity can fund it even when troop queues keep the
        // current food balance low; preserve actual famine recovery otherwise.
        if(armor==0 && bronze && supply.readyFarms>=4 && !started &&
            s.tech[BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER].building<0 && count(BUILDING_STOCK,true))
            money.food=std::max(0,money.food-c.food);
        if(armor==1 && bronze && info.Gold>=c.gold && !started && s.tech[BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER].building<0 && count(BUILDING_STOCK,true))
            money.food=std::max(0,money.food-c.food);
    }
    const int desiredRanges=rangeGoal(*this,supply,aging);
    int woodReserve=(!foodEmergency && count(BUILDING_RANGE)<desiredRanges?BUILD_RANGE_WOOD:BUILD_FARM_WOOD);
    if(info.Human_MaxNum<50 && info.Human_Num+8>=info.Human_MaxNum)woodReserve+=BUILD_HOUSE_WOOD;
    for(const auto& b:info.buildings)if(b.Type==BUILDING_MARKET && int(workers.size())>=16 &&
        (bronze?level(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW)>0:bows>=5)){
        int woodLevel=level(BUILDING_MARKET_WOOD_UPGRADE);
        Cost c=woodLevel?Cost(BUILDING_MARKET_CRAFT_UPGRADE_WOOD,BUILDING_MARKET_CRAFT_UPGRADE_FOOD):Cost(BUILDING_MARKET_WOOD_UPGRADE_WOOD,BUILDING_MARKET_WOOD_UPGRADE_FOOD);
        int trainingFood=bronze?std::min(4,ranges)*BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_FOOD:80;
        const bool readyFirstArmy=s.phase!=Defend || int(army.size())>=14;
        const int rangeTrainingFood=std::min(1,ranges)*BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_FOOD;
        if(!foodEmergency && woodLevel==1 && bronze && (ranges>=2 || readyFirstArmy) && bows>=8 && armor>=1 &&
            info.Gold>=40 && money.food>=c.food+rangeTrainingFood){
            if(project(b,BUILDING_MARKET_WOOD_UPGRADE,c,87,true))continue;
            if(s.tech[BUILDING_MARKET_WOOD_UPGRADE].building<0)money.wood=std::max(0,money.wood-c.wood);
        }
        // Accumulate the matching bow range before optional armor two and endless
        // troop queues consume each food delivery. Keep the next troop's food
        // available, and never reserve this technology during a food emergency.
        if(!foodEmergency && woodLevel==1 && bronze && bows>=8 && armor>=1 &&
            (ranges>=2 || readyFirstArmy) && info.Gold>=40 &&
            s.tech[BUILDING_MARKET_WOOD_UPGRADE].building<0){
            money.food=std::max(0,money.food-c.food);
            money.wood=std::max(0,money.wood-c.wood);
        }
        c=Cost(BUILDING_MARKET_GOLD_UPGRADE_WOOD,BUILDING_MARKET_GOLD_UPGRADE_FOOD);
        const int fundedBows=std::min(std::min(4,ranges),std::max(1,money.gold/std::max(1,BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_GOLD)));
        const int goldTrainingFood=fundedBows*BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_FOOD;
        if(!foodEmergency && bronze && ranges>=2 && !level(BUILDING_MARKET_GOLD_UPGRADE) && info.Gold<300 &&
            money.wood>=c.wood+woodReserve && money.food>=c.food+goldTrainingFood &&
            project(b,BUILDING_MARKET_GOLD_UPGRADE,c,75,true))continue;
        c=Cost(BUILDING_MARKET_FARM_UPGRADE_WOOD,BUILDING_MARKET_FARM_UPGRADE_FOOD);
        if(!foodEmergency && bronze && farmCount>=10 && bows>=18 && !level(BUILDING_MARKET_FARM_UPGRADE) &&
            money.wood>=c.wood+woodReserve && money.food>=c.food+trainingFood)
            project(b,BUILDING_MARKET_FARM_UPGRADE,c,65,true);
    }
    int committedBows=bows;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_RANGE){
        if(b.Project==BUILDING_RANGE_CREATE_BOWMAN)++committedBows;
        else for(const auto& pending:s.pending)if(pending.second.order.kind==Project &&
            pending.second.order.sn==b.SN && pending.second.order.option==BUILDING_RANGE_CREATE_BOWMAN){++committedBows;break;}
    }
    for(const auto& b:info.buildings)if(b.Type==BUILDING_RANGE){
        if(bronze && level(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW))
            project(b,BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN,Cost(0,BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_FOOD,0,BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_GOLD,2),70);
        else if(info.civilizationStage>=CIVILIZATION_TOOLAGE && committedBows<(homeDanger?4:2) &&
            (!bronze || s.tech[BUILDING_RANGE_UPGRADE_COMPOSITE_BOW].building>=0))
            if(project(b,BUILDING_RANGE_CREATE_BOWMAN,Cost(BUILDING_RANGE_CREATE_BOWMAN_WOOD,BUILDING_RANGE_CREATE_BOWMAN_FOOD,0,0,2),72))++committedBows;
    }
    // Extra operators follow funded recruitment. Do not fill population with
    // civilians while the first army or emergency replacements still lack food.
    if(bronze && level(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW) && int(workers.size())>=20 &&
        int(workers.size())<wantedWorkers && (s.phase!=Defend || bows>=12) &&
        info.Human_MaxNum-info.Human_Num>=3 && (army.size()>=12 || info.Meat>=400)){
        for(const auto& b:info.buildings)if(b.Type==BUILDING_CENTER && b.Blood>0)
            project(b,BUILDING_CENTER_CREATEFARMER,Cost(0,BUILDING_CENTER_CREATEFARMER_FOOD,0,0,2),60);
    }
}

bool Bot::build(int type,Pos anchor,int priority,int attempt){
    s.buildWish=type;s.buildWood=money.wood;s.buildWorker=-1;s.buildSite=-1;
    Cost cost=buildingCost(type);if(!affordable(cost))return false;
    int n=sizeOf(type);const auto& access=field(base);
    const tagResource* depotMine=nullptr;
    if(type==BUILDING_STOCK)for(const auto& r:info.resources)if(r.Type==RESOURCE_GOLD && r.Cnt>0 &&
        dist(pos(r),anchor)<.1){depotMine=&r;break;}
    auto accessible=[&](Pos p){
        for(int x=int(p.x)-1;x<=int(p.x)+n;++x)for(int y=int(p.y)-1;y<=int(p.y)+n;++y)
            if(inside(x,y) && (x<int(p.x) || x>=int(p.x)+n || y<int(p.y) || y>=int(p.y)+n) && access[x*height+y]>=0)return true;
        return false;
    };
    Pos chosen;double best=1e30;
    auto consider=[&](Pos p,double value){
        if(!site(type,p) || !accessible(p))return;
        Pos center(p.x+n*.5,p.y+n*.5);
        if(depotMine && walkDistance(anchor,center)>goldHaulLimit+2)return;
        if(type==BUILDING_FARM){
            if(dist(center,base)<4)return;
            for(const auto& b:info.buildings)if(b.Blood>0 && (b.Type==BUILDING_RANGE || b.Type==BUILDING_ARMYCAMP) && dist(center,pos(b))<5)return;
            for(const auto& r:info.resources)if(r.Cnt>0 && r.Type==RESOURCE_TREE && dist(center,pos(r))<2)return;
        }
        if(type==BUILDING_RANGE || type==BUILDING_ARMYCAMP || type==BUILDING_MARKET){
            if(dist(center,base)<5)return;
            for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_FARM && dist(center,pos(b))<5)return;
        }
        if(value<best){best=value;chosen=p;}
    };
    if(type==BUILDING_FARM){
        for(const auto& depot:info.buildings)if(depot.Blood>0 && depot.Percent>=100 && depot.Type==BUILDING_GRANARY){
            const auto& route=field(pos(depot));
            for(int dx=-10;dx<=10;++dx)for(int dy=-10;dy<=10;++dy){
                Pos p(depot.BlockDR+dx,depot.BlockUR+dy);if(!inside(int(p.x),int(p.y)))continue;
                int drop=-1;
                for(int x=int(p.x)-1;x<=int(p.x)+n;++x)for(int y=int(p.y)-1;y<=int(p.y)+n;++y)
                    if(inside(x,y) && (x<int(p.x) || x>=int(p.x)+n || y<int(p.y) || y>=int(p.y)+n) && route[x*height+y]>=0)drop=std::max(drop,route[x*height+y]);
                // The far side must also return cheaply after the new footprint blocks its centre.
                if(drop<0 || drop>8)continue;
                double value=drop+dist(Pos(p.x+n*.5,p.y+n*.5),pos(depot))*.15;
                for(Pos old:s.farmSites)if(dist(old,p)<.1)value-=1;
                consider(p,value);
            }
        }
    }else{
        // A granary needs two real, non-overlapping field sites. Search every
        // tile offset, then measure the delivery lanes with all three finished
        // footprints present; a fixed four-tile lattice misses irregular land.
        std::map<int,bool> fieldSites;
        auto granaryPlots=[&](Pos p){
            const int m=sizeOf(BUILDING_FARM);const Pos center(p.x+n*.5,p.y+n*.5);
            std::vector<Pos> plots;
            for(int a=-7;a<=7;++a)for(int b=-7;b<=7;++b){
                Pos f(p.x+a,p.y+b),fc(f.x+m*.5,f.y+m*.5);
                if(f.x<p.x+n+1 && f.x+m+1>p.x && f.y<p.y+n+1 && f.y+m+1>p.y)continue;
                if(reachDist(fc,center)>6 || !inside(int(f.x),int(f.y)))continue;
                int key=int(f.x)*height+int(f.y);auto cached=fieldSites.find(key);
                if(cached==fieldSites.end()){
                    bool usable=false;
                    if(dist(fc,base)>=4 && site(BUILDING_FARM,f))
                        for(int x=int(f.x)-1;x<=int(f.x)+m;++x)for(int y=int(f.y)-1;y<=int(f.y)+m;++y)
                            if(inside(x,y) && access[x*height+y]>=0 &&
                                (x<int(f.x) || x>=int(f.x)+m || y<int(f.y) || y>=int(f.y)+m))usable=true;
                    for(const auto& q:info.buildings)if(q.Blood>0 &&
                        (q.Type==BUILDING_RANGE || q.Type==BUILDING_ARMYCAMP) && dist(fc,pos(q))<5)usable=false;
                    for(const auto& q:info.resources)if(q.Cnt>0 && q.Type==RESOURCE_TREE && dist(fc,pos(q))<2)usable=false;
                    cached=fieldSites.emplace(key,usable).first;
                }
                if(cached->second)plots.push_back(f);
            }
            std::sort(plots.begin(),plots.end(),[&](Pos a,Pos b){return dist(a,p)<dist(b,p);});
            int trials=0;
            for(size_t a=0;a<plots.size();++a)for(size_t b=a+1;b<plots.size();++b){
                Pos first=plots[a],second=plots[b];
                if(first.x<second.x+m+1 && first.x+m+1>second.x &&
                    first.y<second.y+m+1 && first.y+m+1>second.y)continue;
                if(++trials>24)return false;
                const int span=n+18,left=int(p.x)-9,bottom=int(p.y)-9;
                auto local=[&](int x,int y){return (x-left)*span+y-bottom;};
                auto freePlot=[&](int x,int y){
                    if(x<left || x>=left+span || y<bottom || y>=bottom+span ||
                        !inside(x,y) || !walkable[x*height+y])return false;
                    for(Pos top:{p,first,second})if(x>=int(top.x) && x<int(top.x)+m &&
                        y>=int(top.y) && y<int(top.y)+m)return false;
                    return true;
                };
                // Only the local eight-tile delivery radius is visited.
                std::vector<int> route(span*span,-1);std::queue<int> open;
                int cx=int(p.x)+n/2,cy=int(p.y)+n/2;
                for(int x=int(p.x)-1;x<=int(p.x)+n;++x)for(int y=int(p.y)-1;y<=int(p.y)+n;++y)
                    if(freePlot(x,y) && std::abs((*info.theMap)[x][y].height-(*info.theMap)[cx][cy].height)<=1 &&
                        (x<int(p.x) || x>=int(p.x)+n || y<int(p.y) || y>=int(p.y)+n)){
                        route[local(x,y)]=0;open.push(x*height+y);
                    }
                while(!open.empty()){
                    int cell=open.front();open.pop();int x=cell/height,y=cell%height;
                    if(route[local(x,y)]>=8)continue;
                    for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int nx=x+dx,ny=y+dy;
                        if((!dx && !dy) || !freePlot(nx,ny) || route[local(nx,ny)]>=0 ||
                            (dx && dy && (!freePlot(nx,y) || !freePlot(x,ny))) ||
                            std::abs((*info.theMap)[x][y].height-(*info.theMap)[nx][ny].height)>1)continue;
                        route[local(nx,ny)]=route[local(x,y)]+1;open.push(nx*height+ny);
                    }
                }
                bool both=true;
                for(Pos top:{first,second}){
                    bool edge=false;
                    for(int x=int(top.x)-1;x<=int(top.x)+m;++x)for(int y=int(top.y)-1;y<=int(top.y)+m;++y)
                        if(freePlot(x,y) && access[x*height+y]>=0 &&
                            (x<int(top.x) || x>=int(top.x)+m || y<int(top.y) || y>=int(top.y)+m)){
                            edge=true;if(route[local(x,y)]<0)both=false;
                        }
                    if(!edge)both=false;
                }
                if(both)return true;
            }
            return false;
        };
        auto search=[&](Pos origin,int radius,bool fallback){
            std::vector<std::pair<double,Pos>> candidates;
            for(int dx=-radius;dx<=radius;++dx)for(int dy=-radius;dy<=radius;++dy){
                Pos p(int(origin.x)+dx-n/2,int(origin.y)+dy-n/2),center(p.x+n*.5,p.y+n*.5);
                if(fallback && std::abs(int(p.x)+n/2-int(anchor.x))<=16 &&
                    std::abs(int(p.y)+n/2-int(anchor.y))<=16)continue;
                if(fallback && (dist(center,base)>24 || !inside(int(center.x),int(center.y)) ||
                    access[int(center.x)*height+int(center.y)]<0 || access[int(center.x)*height+int(center.y)]>30))continue;
                double value=dist(center,origin)+dist(center,base)*.03;
                if(type==BUILDING_GRANARY){
                    bool restored=false;for(Pos old:s.granarySites)if(dist(p,old)<.1)restored=true;
                    if(!restored && dist(center,base)<8)continue;
                    if(restored)value-=4;
                }
                if(site(type,p) && accessible(p))candidates.push_back(std::make_pair(value,p));
            }
            std::sort(candidates.begin(),candidates.end(),[](const std::pair<double,Pos>& a,const std::pair<double,Pos>& b){return a.first<b.first;});
            // Keep the nearest sites first, then rotate the remaining bounded
            // batch so a crowded zone cannot permanently hide a usable site.
            const size_t limit=type==BUILDING_GRANARY?std::min(size_t(48),candidates.size()):candidates.size();
            const size_t fixed=std::min(size_t(16),candidates.size()),rest=candidates.size()-fixed;
            const size_t batches=(rest+31)/32,start=batches?(size_t(seconds)%batches)*32:0;
            for(size_t checked=0;checked<limit;++checked){
                const size_t index=type==BUILDING_GRANARY && checked>=fixed?fixed+(start+checked-fixed)%rest:checked;
                const auto& candidate=candidates[index];
                Pos p=candidate.second;
                if(type==BUILDING_GRANARY){
                    bool restored=false;for(Pos old:s.granarySites)if(dist(p,old)<.1)restored=true;
                    if(!restored && !granaryPlots(p))continue;
                }
                consider(p,candidate.first);
                if(best<1e30)return;
            }
        };
        search(anchor,type==BUILDING_GRANARY?16:12,false);
        // A flank clipped by the map edge or forest must not prevent expansion.
        // Keep the preferred field zone first; fallback remains near the base,
        // explored, safe, reachable, and subject to the same field/lane tests.
        if(type==BUILDING_GRANARY && best==1e30)search(base,24,true);
    }

    if(best==1e30)return false;s.buildSite=int(chosen.x)*height+int(chosen.y);
    // A warehouse or field must not close a river crossing or a forest exit.
    // Check the finished footprint against the already reachable perimeter before issuing a build.
    auto freeAfter=[&](int x,int y){return inside(x,y) && walkable[x*height+y] &&
        !(x>=int(chosen.x) && x<int(chosen.x)+n && y>=int(chosen.y) && y<int(chosen.y)+n);};
    std::vector<int> remaining(width*height,-1);std::queue<int> open;
    for(int i=0;i<width*height;++i)if(access[i]==0 && freeAfter(i/height,i%height)){remaining[i]=0;open.push(i);}
    while(!open.empty()){
        int cell=open.front();open.pop();int x=cell/height,y=cell%height;
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int nx=x+dx,ny=y+dy;
            if((!dx && !dy) || !freeAfter(nx,ny) || remaining[nx*height+ny]>=0 ||
                (dx && dy && (!freeAfter(nx,y) || !freeAfter(x,ny))) ||
                std::abs((*info.theMap)[x][y].height-(*info.theMap)[nx][ny].height)>1)continue;
            remaining[nx*height+ny]=remaining[cell]+1;open.push(nx*height+ny);
        }
    }
    for(int x=int(chosen.x)-1;x<=int(chosen.x)+n;++x)for(int y=int(chosen.y)-1;y<=int(chosen.y)+n;++y)
        if(inside(x,y) && freeAfter(x,y) && access[x*height+y]>=0 && remaining[x*height+y]<0){
            s.blockedSites[s.buildSite]=now+frames(30);return attempt<2?build(type,anchor,priority,attempt+1):false;
        }
    if(depotMine){
        // Validate the finished warehouse, not a shortcut through its empty plot.
        std::vector<int> delivery(width*height,-1);std::queue<int> cells;
        for(int x=int(chosen.x)-1;x<=int(chosen.x)+n;++x)for(int y=int(chosen.y)-1;y<=int(chosen.y)+n;++y)
            if(freeAfter(x,y) && remaining[x*height+y]>=0 &&
                std::abs((*info.theMap)[x][y].height-(*info.theMap)[int(chosen.x)+n/2][int(chosen.y)+n/2].height)<=1){
                delivery[x*height+y]=0;cells.push(x*height+y);
            }
        while(!cells.empty()){
            int cell=cells.front();cells.pop();int x=cell/height,y=cell%height;
            if(delivery[cell]>=goldHaulLimit)continue;
            for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int nx=x+dx,ny=y+dy;
                if((!dx && !dy) || !freeAfter(nx,ny) || delivery[nx*height+ny]>=0 ||
                    (dx && dy && (!freeAfter(nx,y) || !freeAfter(x,ny))) ||
                    std::abs((*info.theMap)[x][y].height-(*info.theMap)[nx][ny].height)>1)continue;
                delivery[nx*height+ny]=delivery[cell]+1;cells.push(nx*height+ny);
            }
        }
        bool shortTrip=false;int mx=int(anchor.x),my=int(anchor.y);
        for(int dx=-2;dx<=2;++dx)for(int dy=-2;dy<=2;++dy){int x=mx+dx,y=my+dy;
            if(freeAfter(x,y) && delivery[x*height+y]>=0 &&
                std::abs((*info.theMap)[x][y].height-(*info.theMap)[mx][my].height)<=1 &&
                delivery[x*height+y]+std::max(std::abs(dx),std::abs(dy))<=goldHaulLimit)shortTrip=true;
        }
        if(!shortTrip){s.blockedSites[s.buildSite]=now+frames(30);return attempt<2?build(type,anchor,priority,attempt+1):false;}
    }
    {
        // Measure delivery after this foundation occupies the land. A short
        // route through an empty plot must not be counted as a usable farm lane.
        std::vector<int> foodRoute(width*height,-1);std::queue<int> cells;
        auto seed=[&](int bx,int by,int size){
            int cx=bx+size/2,cy=by+size/2;
            for(int x=bx-1;x<=bx+size;++x)for(int y=by-1;y<=by+size;++y)
                if(freeAfter(x,y) && std::abs((*info.theMap)[x][y].height-(*info.theMap)[cx][cy].height)<=1 &&
                    (x<bx || x>=bx+size || y<by || y>=by+size) && foodRoute[x*height+y]<0){
                    foodRoute[x*height+y]=0;cells.push(x*height+y);
                }
        };
        for(const auto& b:info.buildings)if(b.Blood>0 && b.Percent>=100 && (b.Type==BUILDING_GRANARY || b.Type==BUILDING_CENTER))
            seed(b.BlockDR,b.BlockUR,sizeOf(b.Type));
        if(type==BUILDING_GRANARY)seed(int(chosen.x),int(chosen.y),n);
        while(!cells.empty()){
            int cell=cells.front();cells.pop();if(foodRoute[cell]>=8)continue;
            int x=cell/height,y=cell%height;
            for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int nx=x+dx,ny=y+dy;
                if((!dx && !dy) || !freeAfter(nx,ny) || foodRoute[nx*height+ny]>=0 ||
                    (dx && dy && (!freeAfter(nx,y) || !freeAfter(x,ny))) ||
                    std::abs((*info.theMap)[x][y].height-(*info.theMap)[nx][ny].height)>1)continue;
                foodRoute[nx*height+ny]=foodRoute[cell]+1;cells.push(nx*height+ny);
            }
        }
        auto shortTrip=[&](int bx,int by,int size){
            bool edge=false;
            for(int x=bx-1;x<=bx+size;++x)for(int y=by-1;y<=by+size;++y)
                if(freeAfter(x,y) && remaining[x*height+y]>=0 && (x<bx || x>=bx+size || y<by || y>=by+size)){
                    edge=true;if(foodRoute[x*height+y]<0)return false;
                }
            return edge;
        };
        bool valid=type!=BUILDING_FARM || shortTrip(int(chosen.x),int(chosen.y),n);
        for(const auto& farm:info.buildings)if(farm.Blood>0 && farm.Type==BUILDING_FARM && !s.stalledFarms.count(farm.SN) &&
            (farm.Percent<100 || farm.Cnt>=60) &&
            farmDropDistance(*this,Pos(farm.BlockDR,farm.BlockUR),sizeOf(farm.Type))<=8 &&
            !shortTrip(farm.BlockDR,farm.BlockUR,sizeOf(farm.Type)))valid=false;
        if(!valid){s.blockedSites[s.buildSite]=now+frames(30);return attempt<2?build(type,anchor,priority,attempt+1):false;}
    }
    const tagFarmer* builder=nullptr;best=1e30;
    for(auto f:workers){
        if(f->SN==s.resourceScout || orders.count(f->SN) || pending(f->SN) || s.units[f->SN].retry>now || constructing(*f))continue;
        auto& u=s.units[f->SN];if(u.depot>=0 && f->Resource>0)continue;
        int walk=walkDistance(pos(*f),Pos(chosen.x+n*.5,chosen.y+n*.5));if(walk==INT_MAX)continue;
        double value=walk+(f->Resource>0?1000:0)+(f->NowState!=HUMAN_STATE_IDLE?5:0)+(u.source==4?8:0);
        if(value<best){best=value;builder=f;}
    }
    if(!builder)return false;s.buildWorker=builder->SN;
    if(builder->Resource>0)return deposit(*builder,priority);
    Order o;o.kind=Build;o.sn=builder->SN;o.option=type;o.point=chosen;o.cost=cost;o.priority=priority;
    return propose(o);
}

bool Bot::reserveBuild(int type,Pos anchor,int priority){
    // Save the required investment while wood arrives or an available builder unloads.
    // A funded request with no usable site must not block all other construction.
    const bool funded=affordable(buildingCost(type));
    if(build(type,anchor,priority))return true;
    if(!funded)return true;
    return s.buildSite>=0 && !s.blockedSites.count(s.buildSite);
}

void Bot::construction(){
    std::map<int,int> completed;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Percent>=100)++completed[b.Type];
    for(const auto& entry:completed)s.established[entry.first]=std::max(s.established[entry.first],entry.second);
    int unfinished=0;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Percent<100){
        auto& history=s.units[b.SN];int value=b.Percent*100000+b.Blood;
        if(history.value!=value){history.value=value;history.progress=now;s.stalledFarms.erase(b.SN);}
        bool active=false;for(auto f:workers)if(f->WorkObjectSN==b.SN && f->NowState==HUMAN_STATE_WORKING)active=true;
        if(b.Type==BUILDING_FARM && b.Blood>=b.MaxBlood && (!active || now-history.progress>=frames(30)) && now-history.progress>=frames(8)){s.stalledFarms.insert(b.SN);continue;}
        if(s.stalledFarms.count(b.SN))continue;
        ++unfinished;
        if(active && now-history.progress<frames(30))continue;
        const tagFarmer* chosen=nullptr;double best=1e30;
        for(auto f:workers)if(f->SN!=s.resourceScout && !orders.count(f->SN) && !pending(f->SN) && !constructing(*f) && f->Resource==0){
            int walk=walkDistance(pos(*f),pos(b));if(walk==INT_MAX)continue;
            double score=walk+(f->NowState==HUMAN_STATE_IDLE?0:6)+(s.units[f->SN].source==4?8:0);
            if(score<best){best=score;chosen=f;}
        }
        if(chosen)action(chosen->SN,b.SN,90);
    }
    if(now-s.buildTime<frames(1) || unfinished>=2)return;s.buildTime=now;
    bool bronze=info.civilizationStage>=CIVILIZATION_BRONZEAGE,aging=false;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_CENTER && b.Project==BUILDING_CENTER_UPGRADE)aging=true;
    if(info.Human_MaxNum<50 && info.Human_Num+(bronze?10:6)>=info.Human_MaxNum && count(BUILDING_HOME)==count(BUILDING_HOME,true)){
        if(build(BUILDING_HOME,base,92))return;
    }
    if(!count(BUILDING_CENTER) && build(BUILDING_CENTER,base,96))return;
    Pos productionZone=clamp(Pos(base.x+(focus.x-base.x)*.18,base.y+(focus.y-base.y)*.18));
    bool rangePending=false;
    for(const auto& p:s.pending)if(p.second.order.kind==Build && p.second.order.option==BUILDING_RANGE)rangePending=true;
    // The market unlocks food production. Open it before spending the first
    // harvests on a range, then keep a small defensive line while fields grow.
    if(info.civilizationStage>=CIVILIZATION_TOOLAGE && count(BUILDING_GRANARY,true) && !count(BUILDING_MARKET)){
        build(BUILDING_MARKET,productionZone,88);return;
    }
    if(!count(BUILDING_ARMYCAMP)){build(BUILDING_ARMYCAMP,Pos(base.x+(focus.x-base.x)*.15,base.y+(focus.y-base.y)*.15),88);return;}
    if(!count(BUILDING_GRANARY)){
        Pos anchor=base;double d=1e30;
        for(Pos p:s.granarySites)if(dist(p,base)<d){d=dist(p,base);anchor=p;}
        build(BUILDING_GRANARY,anchor,87);return;
    }
    if(info.civilizationStage<CIVILIZATION_TOOLAGE)return;
    if(!count(BUILDING_RANGE) && count(BUILDING_MARKET,true) && !count(BUILDING_FARM) &&
        !homeDanger && build(BUILDING_FARM,base,87))return;
    if(count(BUILDING_ARMYCAMP,true) && !count(BUILDING_RANGE) && !rangePending){build(BUILDING_RANGE,productionZone,87);return;}
    if(count(BUILDING_GRANARY,true) && !count(BUILDING_MARKET)){build(BUILDING_MARKET,productionZone,86);return;}
    if(level(BUILDING_GRANARY_ARROWTOWER) && count(BUILDING_ARROWTOWER)<1 && money.stone>=BUILD_ARROWTOWER_STONE){
        Pos front=clamp(Pos(base.x+(focus.x-base.x)*.1,base.y+(focus.y-base.y)*.1));
        if(build(BUILDING_ARROWTOWER,front,94))return;
    }
    // Restore a destroyed field's depot even if a distant berry granary survives.
    for(Pos p:s.granarySites){
        bool depot=false;for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_GRANARY && dist(pos(b),Pos(p.x+sizeOf(b.Type)*.5,p.y+sizeOf(b.Type)*.5))<5)depot=true;
        int fields=0;for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_FARM && b.Cnt>0 && dist(pos(b),p)<11)++fields;
        if(!depot && fields>=2 && build(BUILDING_GRANARY,p,85))return;
    }
    int desired[3];targets(desired);
    const FoodSupply supply=foodSupply(*this);
    const int farmSlots=supply.farms,freeFarms=supply.freeFarms,natural=supply.natural;
    const int foodSlots=supply.readyFarms+natural;
    const int wildCredit=std::min(2,supply.berries);
    const int farmFloor=bronze?16:12;
    const int farmGoal=std::min(22,std::max(farmFloor,desired[1]+2-wildCredit));
    bool needFarm=count(BUILDING_MARKET,true) && farmSlots<farmGoal && freeFarms<2;
    const bool foodEmergency=info.Meat<200 && supply.productive<4;
    Pos d(focus.x-base.x,focus.y-base.y);double length=std::max(1.0,dist(base,focus));
    Pos fieldZone=clamp(Pos(base.x-d.y/length*12-d.x/length*3,base.y+d.x/length*12-d.y/length*3));
    const tagBuilding* remoteFarm=nullptr;int longest=8;
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_FARM && b.Percent>=100 && b.Cnt>=60 &&
        !s.stalledFarms.count(b.SN) && danger(pos(b),3)==0){
        int drop=farmDropDistance(*this,Pos(b.BlockDR,b.BlockUR),sizeOf(b.Type));bool building=false;
        for(const auto& q:info.buildings)if(q.Blood>0 && q.Type==BUILDING_GRANARY && q.Percent<100 && dist(pos(q),pos(b))<10)building=true;
        for(const auto& q:s.pending)if(q.second.order.kind==Build && q.second.order.option==BUILDING_GRANARY &&
            dist(Pos(q.second.order.point.x+1.5,q.second.order.point.y+1.5),pos(b))<10)building=true;
        if(!building && drop>longest){longest=drop;remoteFarm=&b;}
    }
    auto woodCluster=[&](int minimumDrop,int minimumWood)->const tagResource*{
        const tagResource* forest=nullptr;double best=0;
        for(const auto& r:info.resources)if(r.Type==RESOURCE_TREE && r.Cnt>0 && dist(pos(r),base)<30 && danger(pos(r),3)==0){
            int gatherers=0,remaining=0;
            for(auto f:workers){
                const auto& u=s.units[f->SN];auto source=resources.find(u.job);
                bool working=resourceRole(f->WorkObjectSN)==0 || (u.depot>=0 && f->Resource>0 && resourceRole(u.job)==0);
                if(working && now-u.progress<frames(30) && !constructing(*f) && source!=resources.end() &&
                    source->second->Type==RESOURCE_TREE && dist(pos(*source->second),pos(r))<5)++gatherers;
            }
            if(gatherers<3)continue;
            for(const auto& q:info.resources)if(q.Type==RESOURCE_TREE && q.Cnt>0 && dist(pos(q),pos(r))<5)remaining+=q.Cnt;
            if(remaining<minimumWood)continue;
            int drop=INT_MAX;bool building=false;
            for(const auto& b:info.buildings)if(b.Blood>0 && (b.Type==BUILDING_STOCK || b.Type==BUILDING_CENTER)){
                if(b.Percent>=100)drop=std::min(drop,walkDistance(pos(r),pos(b)));
                else if(dist(pos(b),pos(r))<12)building=true;
            }
            for(const auto& p:s.pending)if(p.second.order.kind==Build && p.second.order.option==BUILDING_STOCK &&
                dist(Pos(p.second.order.point.x+1.5,p.second.order.point.y+1.5),pos(r))<12)building=true;
            if(drop!=INT_MAX && drop>minimumDrop && !building && drop+gatherers>best){best=drop+gatherers;forest=&r;}
        }
        return forest;
    };
    // Food rescue still comes first, but a ready gold drop-off precedes extra
    // fields and ranges. Never send an advance miner on a long return journey.
    if(foodEmergency && count(BUILDING_MARKET,true)){
        if(remoteFarm && count(BUILDING_GRANARY)<5 && reserveBuild(BUILDING_GRANARY,pos(*remoteFarm),87))return;
        if(needFarm){
            if(reserveBuild(BUILDING_FARM,base,86))return;
            const int barns=(supply.readyFarms<8 && freeFarms==0)?7:5;
            if(count(BUILDING_GRANARY)<barns && reserveBuild(BUILDING_GRANARY,fieldZone,85))return;
        }
    }
    if(!foodEmergency && desired[2]>0 && info.Gold<240 &&
        (bronze || aging || foodSlots>=4 || info.Meat>=600) && readyGoldCapacity(*this)<desired[2]){
        for(auto mine:localGoldMines(*this)){
            if(mine->Cnt<=100 || goldDropDistance(*this,pos(*mine))<=goldHaulLimit)continue;
            bool beingBuilt=false;
            auto nearby=[&](Pos center){return dist(center,pos(*mine))<=goldHaulLimit+3;};
            for(const auto& b:info.buildings)if(b.Blood>0 && b.Type==BUILDING_STOCK && b.Percent<100 && nearby(pos(b)))beingBuilt=true;
            for(const auto& p:s.pending)if(p.second.order.kind==Build && p.second.order.option==BUILDING_STOCK &&
                nearby(Pos(p.second.order.point.x+1.5,p.second.order.point.y+1.5)))beingBuilt=true;
            for(const auto& p:orders)if(p.second.kind==Build && p.second.option==BUILDING_STOCK &&
                nearby(Pos(p.second.point.x+1.5,p.second.point.y+1.5)))beingBuilt=true;
            if(!beingBuilt && reserveBuild(BUILDING_STOCK,pos(*mine),85))return;
        }
    }
    // Open a permanent food district before extra recruitment lines. Animal
    // workers cannot substitute for the first four fields or postpone them.
    if(needFarm && farmSlots<4 && (!bronze || info.Meat<400)){
        if(reserveBuild(BUILDING_FARM,base,86))return;
        if(count(BUILDING_GRANARY)<4 && reserveBuild(BUILDING_GRANARY,fieldZone,85))return;
    }
    const int ranges=count(BUILDING_RANGE),wantedRanges=rangeGoal(*this,supply,aging);
    const bool canExpandRange=!rangePending && ranges==count(BUILDING_RANGE,true);
    // Prepare the second line in Tool Age, before repeatedly expanding to eight
    // fields. The shared budget still protects age advance and the composite unlock.
    if(!foodEmergency && ranges<std::min(2,wantedRanges) && canExpandRange &&
        reserveBuild(BUILDING_RANGE,productionZone,84))return;
    // Opening berry reserves must not postpone permanent food production.
    if(!bronze && !aging && needFarm && farmSlots<8){
        if(reserveBuild(BUILDING_FARM,base,86))return;
        if(count(BUILDING_GRANARY)<4 && reserveBuild(BUILDING_GRANARY,fieldZone,85))return;
    }
    // One early depot can fund both field renewal and extra ranges when wood trips are genuinely long.
    // Requiring a large active forest and at most two existing depots prevents repeated early investments.
    int plannedStocks=count(BUILDING_STOCK);
    for(const auto& p:s.pending)if(p.second.order.kind==Build && p.second.order.option==BUILDING_STOCK)++plannedStocks;
    if((bronze || farmSlots>=8) && foodSlots>=2 && info.Meat>=100 && plannedStocks<=2){
        const tagResource* forest=woodCluster(12,750);
        if(forest && reserveBuild(BUILDING_STOCK,pos(*forest),81))return;
    }
    // Build the third line during age advancement. Later lines are prepared
    // from existing food capacity, without waiting for the unlock to finish.
    if(!foodEmergency && ranges<wantedRanges && canExpandRange &&
        reserveBuild(BUILDING_RANGE,productionZone,83))return;
    if(needFarm && foodSlots<4 && build(BUILDING_FARM,base,84))return;
    if(remoteFarm && count(BUILDING_GRANARY)<5 && reserveBuild(BUILDING_GRANARY,pos(*remoteFarm),82))return;
    if(needFarm && farmSlots>=4 && desired[1]-wildCredit>farmSlots+2 &&
        count(BUILDING_GRANARY)<(farmSlots>=8?3:2)){
        // Save a new field depot before the remaining small plots consume every wood delivery.
        if(reserveBuild(BUILDING_GRANARY,fieldZone,82))return;
    }
    if(needFarm){
        if(build(BUILDING_FARM,base,80))return;
        // An exhausted field district can have five depots yet no usable plot.
        // Expand only after a funded field search failed, with no empty field
        // while the food supply still needs more productive plots.
        const bool noPlot=s.buildSite<0 || s.blockedSites.count(s.buildSite);
        const int barns=(noPlot && money.wood>=BUILD_FARM_WOOD && info.Meat<400 && freeFarms==0)?7:5;
        if(count(BUILDING_GRANARY)<barns && reserveBuild(BUILDING_GRANARY,fieldZone,79))return;
    }
    // Shorter wood deliveries help only after food and the first extra range exist.
    // Keep one field's cost available and never freeze construction to save for this optional depot.
    const int woodDepots=bronze?5:3;
    if(plannedStocks<woodDepots && foodSlots>=8 && (!bronze || count(BUILDING_RANGE,true)>=2) &&
        money.wood>=BUILD_STOCK_WOOD+BUILD_FARM_WOOD){
        const tagResource* forest=woodCluster(8,250);
        if(forest)build(BUILDING_STOCK,pos(*forest),81);
    }
}

void Bot::economy(){
    int desired[3];targets(desired);int assigned[3]={0,0,0};std::map<int,int> load,roles;
    // Until a local depot finishes, keep potential miners working at home.
    const int readyMiners=std::min(desired[2],readyGoldCapacity(*this));
    desired[0]+=desired[2]-readyMiners;desired[2]=readyMiners;
    std::vector<const tagFarmer*> idle;
    // One early civilian surveys the safe resource ring, then returns before the raids.
    // The army is never sent ahead to discover the opposite base.
    bool knownMine=false;
    for(const auto& r:info.resources)if(r.Type==RESOURCE_GOLD && r.Cnt>100 && dist(pos(r),base)<32 &&
        danger(pos(r),3)==0 && walkDistance(base,pos(r))<36)knownMine=true;
    const tagFarmer* scout=nullptr;
    const bool surveyTime=seconds>=20 && (seconds<=360 || army.size()>=5);
    if(surveyTime && workers.size()>=9 && !homeDanger && !knownMine){
        for(auto f:workers)if(f->SN==s.resourceScout && f->Resource==0 && !constructing(*f))scout=f;
        if(!scout)for(auto f:workers)if(f->Blood*4>=f->MaxBlood*3 && f->Resource==0 && !orders.count(f->SN) &&
            !pending(f->SN) && !constructing(*f) && s.units[f->SN].source!=4 && danger(pos(*f),3)==0){
            scout=f;s.resourceScout=f->SN;s.scoutFrame=-10000;break;
        }
    }
    if(scout){
        auto goalNeedsVision=[&](){
            int x=int(s.scoutGoal.x),y=int(s.scoutGoal.y);
            for(int dx=-4;dx<=4;dx+=2)for(int dy=-4;dy<=4;dy+=2)
                if(inside(x+dx,y+dy) && (*info.theMap)[x+dx][y+dy].type==MAPPATTERN_UNKNOWN)return true;
            return false;
        };
        if(now-s.units[scout->SN].progress>=frames(12) && dist(pos(*scout),s.scoutGoal)>2){
            s.blockedSurvey[int(s.scoutGoal.x)*height+int(s.scoutGoal.y)]=now+frames(60);
            s.scoutFrame=-10000;
        }
        // Finish revealing one resource frontier before choosing the next one.
        // Periodic rescoring while travelling otherwise sends the scout back across the base.
        if(now-s.scoutFrame>=frames(60) || dist(pos(*scout),s.scoutGoal)<1.5 ||
            !goalNeedsVision() || danger(s.scoutGoal,3)>0){
            const auto& accessible=field(pos(*scout));Pos selected=pos(*scout);double best=-1e30;
            for(int x=0;x<width;++x)for(int y=0;y<height;++y){
                Pos p(x+.5,y+.5);if(accessible[x*height+y]<0 || dist(p,base)>26 || danger(p,3)>0 || occupied(p,.2))continue;
                bool blocked=false;
                for(const auto& old:s.blockedSurvey)if(old.second>now && dist(p,Pos(old.first/height+.5,old.first%height+.5))<3)blocked=true;
                if(blocked)continue;
                int unknown=0;
                for(int dx=-4;dx<=4;dx+=2)for(int dy=-4;dy<=4;dy+=2)
                    if(inside(x+dx,y+dy) && (*info.theMap)[x+dx][y+dy].type==MAPPATTERN_UNKNOWN)++unknown;
                if(!unknown)continue;
                double score=unknown*2-accessible[x*height+y]*.5-dist(p,base)*.15;
                auto visited=s.visited.find(x*height+y);if(visited!=s.visited.end() && now-visited->second<frames(30))score-=15;
                if(score>best){best=score;selected=p;}
            }
            s.scoutGoal=selected;s.scoutFrame=now;
        }
        move(scout->SN,waypoint(pos(*scout),s.scoutGoal),64,.5);
    }else if(s.resourceScout>=0){
        auto& old=s.units[s.resourceScout];if(old.depot<0){old.job=-1;old.role=-1;}s.resourceScout=-1;
    }
    auto safeJob=[&](int target,Pos point,int workerSN){
        // A returning worker may resume its established site despite a nearby
        // battle. Keep the wider caution buffer when choosing an entirely new job.
        return target==s.units[workerSN].job?!workerThreat(point,workerSN):danger(point,3)==0;
    };
    auto safeFood=[&](const tagResource& r,int workerSN){
        if(dist(pos(r),base)>30 || !safeJob(r.SN,pos(r),workerSN) || (s.factorySN>=0 && dist(pos(r),s.factory)<26))return false;
        for(const auto& a:info.resources)if(a.Type==RESOURCE_LION && a.Blood>0 && dist(pos(a),pos(r))<6)return false;
        return true;
    };
    auto cap=[&](int target){auto r=resources.find(target);return r!=resources.end()?resourceCapacity(*r->second):1;};
    for(auto f:workers){
        if(scout && f->SN==scout->SN)continue;
        auto& u=s.units[f->SN];
        auto repair=buildings.find(f->WorkObjectSN);
        if(repair!=buildings.end() && repair->second->Percent>=100 && repair->second->Blood<repair->second->MaxBlood &&
            f->NowState==HUMAN_STATE_WORKING && now-u.progress<frames(30))continue;
        if(constructing(*f) || orders.count(f->SN) || pending(f->SN)){
            int role=resourceRole(f->WorkObjectSN);
            if(role<0 && f->NowState==HUMAN_STATE_IDLE && f->Resource==0 && u.source==0 &&
                u.last.kind==Use && u.last.target==u.job && resourceRole(u.job)==0)role=0;
            if(role<0 && (u.depot>=0 || (pending(f->SN) && resourceRole(u.job)==2)))role=resourceRole(u.job);
            if(role>=0){++assigned[role];++load[u.job];roles[f->SN]=role;}continue;
        }
        if(workerThreat(pos(*f),f->SN)){move(f->SN,safePoint(pos(*f),rally),100);continue;}
        if(u.depot>=0 && f->Resource>0){
            if(deposit(*f)){int role=resourceRole(u.job);if(role>=0){++assigned[role];++load[u.job];roles[f->SN]=role;}continue;}
        }else if(u.depot>=0)u.depot=-1;
        if(f->NowState==HUMAN_STATE_IDLE && f->Resource>0){
            deposit(*f);int role=resourceRole(u.job);if(role>=0){++assigned[role];++load[u.job];roles[f->SN]=role;}continue;
        }
        int role=resourceRole(f->WorkObjectSN);
        int stalledJob=f->WorkObjectSN;
        // Failed native paths can erase WorkObjectSN while leaving the worker
        // idle. Retain the attempted tree's recovery clock in that case too.
        if(role<0 && f->NowState==HUMAN_STATE_IDLE && f->Resource==0 && u.source==0 &&
            u.last.kind==Use && u.last.target==u.job && resourceRole(u.job)==0){role=0;stalledJob=u.job;}
        if(role<0 && f->NowState==HUMAN_STATE_WORKING && f->WorkObjectSN==u.job && u.source==0 &&
            (now-u.harvest<frames(6) || now-u.moved<frames(6))){
            // Native integer Cnt0 can still hold a fractional tail. Let the
            // existing Gather consume it while cargo or movement really advances.
            bool residualTree=false;
            for(const auto& source:info.resources)if(source.SN==f->WorkObjectSN && source.Type==RESOURCE_TREE &&
                source.Cnt==0 && source.Blood==0){residualTree=true;break;}
            if(residualTree){++assigned[0];++load[u.job];roles[f->SN]=0;continue;}
        }
        if(role<0 && f->NowState==HUMAN_STATE_WALKING && now-u.progress<frames(20) && resourceRole(u.job)>=0)role=resourceRole(u.job);
        const int stall=frames(role==0?(f->NowState==HUMAN_STATE_IDLE?8:12):30);
        const int retryGap=frames(role==0?8:2);
        if(role>=0 && now-u.progress<stall &&
            !(f->NowState==HUMAN_STATE_IDLE && f->Resource==0 && now-u.sent>=retryGap)){
            if(f->Resource>=10 && now-u.harvest>frames(60)){if(deposit(*f)){++assigned[role];++load[u.job];roles[f->SN]=role;continue;}}
            // A native tree round trip can share a tree with a previous worker.
            // Capacity restricts new assignments, not an advancing existing job.
            if(role==0 || load[u.job]<cap(u.job)){++assigned[role];++load[u.job];roles[f->SN]=role;continue;}
        }
        if(role>=0 && now-u.progress>=stall && u.blocked!=stalledJob){u.blocked=stalledJob;u.blockedUntil=now+frames(30);}
        idle.push_back(f);
    }
    // A small, infrequent transfer corrects shortages without oscillating productive roles.
    std::map<int,int> transfers;
    if(now-s.balance>=frames(20)){
        s.balance=now;
        for(int n=0;n<2;++n){int wanted=-1,surplus=-1;
            for(int k=0;k<3;++k){if(wanted<0 || desired[k]-assigned[k]>desired[wanted]-assigned[wanted])wanted=k;
                if(surplus<0 || assigned[k]-desired[k]>assigned[surplus]-desired[surplus])surplus=k;}
            // A tied timber deficit must not select an ineligible farm-to-wood
            // transfer and prevent the newly prepared gold jobs from filling.
            if(info.Meat>=360 && info.Gold<200 && desired[2]>assigned[2] && assigned[1]>desired[1]+1){wanted=2;surplus=1;}
            if(wanted==surplus || desired[wanted]<=assigned[wanted] || assigned[surplus]<=desired[surplus]+1)break;
            const tagFarmer* choice=nullptr;double best=1e30;
            for(auto f:workers)if(roles.count(f->SN) && roles[f->SN]==surplus && !transfers.count(f->SN)){
                auto& u=s.units[f->SN];
                const bool farmToGold=wanted==2 && info.Meat>=360 && info.Gold<200 && desired[2]>assigned[2];
                if(f->Resource>0 || u.depot>=0 || (u.source==4 && !farmToGold) || orders.count(f->SN) || pending(f->SN) || constructing(*f) || now-u.assigned<frames(40))continue;
                double score=dist(pos(*f),base)+(f->NowState==HUMAN_STATE_IDLE?-8:0);
                if(score<best){best=score;choice=f;}
            }
            if(!choice)break;transfers[choice->SN]=wanted;--assigned[surplus];--load[s.units[choice->SN].job];idle.push_back(choice);
        }
    }
    // Reserve native approaching endpoints as well as current bodies. Adjacent
    // trees may have only one shared harvesting cell; a different SN is not a
    // different working position.
    grid();std::map<int,int> woodEntrances;
    for(auto f:workers)if(f->NowState!=HUMAN_STATE_IDLE && resourceRole(f->WorkObjectSN)==0){
        Pos destination(f->DR0/BLOCKSIDELENGTH,f->UR0/BLOCKSIDELENGTH);
        const auto* tree=resources.at(f->WorkObjectSN);
        int x=int(destination.x),y=int(destination.y);
        if(inside(x,y) && walkable[x*height+y] && reachDist(destination,pos(*tree))<=1.6)
            woodEntrances[f->SN]=x*height+y;
    }
    auto treeEntrance=[&](const tagFarmer& f,const tagResource& tree,Pos& stand,double& penalty){
        const auto& access=field(pos(f));double best=1e30;int free=0,crowd=0;
        for(auto other:workers)if(other->SN!=f.SN && dist(pos(*other),pos(tree))<3)++crowd;
        for(int x=tree.BlockDR-1;x<=tree.BlockDR+1;++x)for(int y=tree.BlockUR-1;y<=tree.BlockUR+1;++y){
            if(!inside(x,y) || !walkable[x*height+y] || access[x*height+y]<0 ||
                std::abs((*info.theMap)[x][y].height-(*info.theMap)[tree.BlockDR][tree.BlockUR].height)>1)continue;
            const int cell=x*height+y;Pos q(x+.5,y+.5);bool occupied=false;
            for(const auto& other:humans)if(other.first!=f.SN && dist(pos(*other.second),q)<.65)occupied=true;
            for(const auto& entrance:woodEntrances)if(entrance.first!=f.SN && entrance.second==cell)occupied=true;
            if(occupied)continue;++free;
            double score=access[cell]+dist(pos(f),q)*.1;
            if(score<best){best=score;stand=q;}
        }
        penalty=crowd*3+4./std::max(1,free);
        return free>0;
    };
    for(auto f:idle){
        if(orders.count(f->SN) || pending(f->SN))continue;auto& u=s.units[f->SN];
        int preferred=transfers.count(f->SN)?transfers[f->SN]:0;
        if(!transfers.count(f->SN))for(int k=1;k<3;++k)if(desired[k]-assigned[k]>desired[preferred]-assigned[preferred])preferred=k;
        int chosen=-1,role=-1;double best=1e30;Pos chosenEntrance;
        for(int pass=0;pass<2 && chosen<0;++pass){
            for(const auto& r:info.resources){int kind=resourceRole(r.SN);
                if(kind<0 || (pass==0 && kind!=preferred) || (r.SN==u.blocked && now<u.blockedUntil) || load[r.SN]>=resourceCapacity(r))continue;
                if(kind==1?!safeFood(r,f->SN):(dist(pos(r),base)>32 || !safeJob(r.SN,pos(r),f->SN)))continue;
                if(kind==2){
                    if(desired[2]==0 || assigned[2]>=desired[2])continue;
                    if(goldDropDistance(*this,pos(r))>goldHaulLimit)continue;
                }
                int walk=walkDistance(pos(*f),pos(r));if(walk==INT_MAX || (kind==1 && walk>38))continue;
                tagFarmer cargo=*f;cargo.ResourceSort=kind==1?HUMAN_GRANARYFOOD:r.ProductSort;
                const tagBuilding* depot=depotFor(cargo,pos(r));if(!depot)continue;
                int trip=walkDistance(pos(r),pos(*depot));if(trip==INT_MAX || (kind==1 && trip>32) || (kind==2 && trip>goldHaulLimit))continue;
                if(kind==1 && seconds>=180){
                    bool fieldAvailable=false;
                    for(const auto& field:info.buildings)if(field.Blood>0 && field.Type==BUILDING_FARM && field.Percent>=100 && field.Cnt>0 &&
                        !s.stalledFarms.count(field.SN) && load[field.SN]<1 && safeJob(field.SN,pos(field),f->SN) &&
                        walkDistance(pos(*f),pos(field))<INT_MAX && farmDropDistance(*this,Pos(field.BlockDR,field.BlockUR),sizeOf(field.Type))<=8)fieldAvailable=true;
                    if((trip>10 || walk>20) && (fieldAvailable || info.Meat>=200))continue;
                }
                Pos entrance;double woodCrowd=0;
                if(kind==0 && !treeEntrance(*f,r,entrance,woodCrowd))continue;
                double score=walk+std::min(30,trip)*(kind==1?1.1:.65)+load[r.SN]*1.5+woodCrowd;
                if(r.SN==u.job && kind==preferred)score-=4;
                if(kind==1)score-=8;
                if(score<best){best=score;chosen=r.SN;role=kind;chosenEntrance=entrance;}
            }
            for(const auto& b:info.buildings)if((pass || preferred==1) && b.Blood>0 && b.Type==BUILDING_FARM && b.Percent>=100 && b.Cnt>0 &&
                !s.stalledFarms.count(b.SN) && load[b.SN]<1 && !(b.SN==u.blocked && now<u.blockedUntil) && safeJob(b.SN,pos(b),f->SN)){
                int walk=walkDistance(pos(*f),pos(b));if(walk==INT_MAX)continue;
                tagFarmer cargo=*f;cargo.ResourceSort=HUMAN_GRANARYFOOD;const tagBuilding* depot=depotFor(cargo,pos(b));
                if(!depot)continue;int trip=farmDropDistance(*this,Pos(b.BlockDR,b.BlockUR),sizeOf(b.Type));if(trip>8)continue;
                double score=walk+trip*.65+(b.SN==u.job?-5:0)-8;
                if(score<best){best=score;chosen=b.SN;role=1;}
            }
        }
        if(chosen<0){if(f->Resource>0)deposit(*f);continue;}
        if(f->Resource>0){
            if(deposit(*f)){int oldRole=resourceRole(u.job);if(oldRole>=0){++assigned[oldRole];++load[u.job];}}continue;
        }
        if(action(f->SN,chosen,45) || (f->WorkObjectSN==chosen && f->NowState!=HUMAN_STATE_IDLE)){
            if(u.role!=role || u.assigned<0)u.assigned=now;
            u.job=chosen;u.role=role;u.source=sourceClass(chosen);++assigned[role];++load[chosen];
            if(role==0)woodEntrances[f->SN]=int(chosenEntrance.x)*height+int(chosenEntrance.y);
        }
    }
    // Repair only critical structures; healthy productive farms keep their worker.
    for(const auto& b:info.buildings)if(b.Blood>0 && b.Percent>=100 && (b.Type==BUILDING_CENTER || b.Type==BUILDING_ARROWTOWER) && b.Blood<b.MaxBlood*.7){
        bool active=false;for(auto f:workers)if(f->WorkObjectSN==b.SN && f->NowState!=HUMAN_STATE_IDLE)active=true;
        if(active)continue;const tagFarmer* chosen=nullptr;double best=1e30;
        for(auto f:workers)if(f->Resource==0 && !orders.count(f->SN) && !pending(f->SN) && !constructing(*f) && s.units[f->SN].source!=4){
            double score=dist(pos(*f),pos(b))+(f->NowState==HUMAN_STATE_IDLE?0:8);if(score<best){best=score;chosen=f;}}
        if(chosen)action(chosen->SN,b.SN,88);
    }
}

namespace combat {
bool melee(int sort){
    return sort==AT_CLUBMAN || sort==AT_SWORDSMAN || sort==AT_BROADSWORDSMAN ||
        sort==AT_CAVALRY || sort==AT_CHARIOT || sort==AT_HOPLITE;
}
bool important(int type){
    return type==BUILDING_CENTER || type==BUILDING_RANGE || type==BUILDING_ARMYCAMP || type==BUILDING_STABLE ||
        type==BUILDING_MARKET || type==BUILDING_STOCK || type==BUILDING_GRANARY || type==BUILDING_FARM;
}
double bowRange(const Bot& b,const tagArmy& a,Pos from,Pos target){
    double value=rangeOf(a.Sort);
    auto u=b.s.units.find(a.SN);
    const bool converted=u!=b.s.units.end() && u->second.converted;
    int wood=b.level(BUILDING_MARKET_WOOD_UPGRADE),addition=0;
    if(converted)wood=EnemyTechnologyMaxCivilization<0 || EnemyTechnologyMaxCivilization>=CIVILIZATION_BRONZEAGE?2:
        (EnemyTechnologyMaxCivilization>=CIVILIZATION_TOOLAGE?1:0);
    if(a.Sort==AT_BOWMAN || a.Sort==AT_IMPROVED || a.Sort==AT_COMPOSITE_BOWMAN || a.Sort==AT_CHARIOT_ARCHER){
        if(wood>=1)addition+=BUILDING_MARKET_WOOD_UPGRADE_ADDITION_DISSHOOT;
        if(wood>=2)addition+=BUILDING_MARKET_CRAFT_UPGRADE_ADDITION_DISSHOOT;
    }
    value+=addition;
    int x=int(from.x),y=int(from.y),tx=int(target.x),ty=int(target.y),vision=0;
    if(a.Sort==AT_BOWMAN)vision=VISION_BOWMAN;
    if(a.Sort==AT_IMPROVED)vision=VISION_IMPROVEDBOWMAN1;
    if(a.Sort==AT_COMPOSITE_BOWMAN)vision=VISION_COMPOSITE_BOWMAN;
    if(a.Sort==AT_CHARIOT_ARCHER)vision=VISION_CHARIOT_ARCHER;
    if(a.Sort==AT_SLINGER)vision=VISION_SLINGER;
    if(a.Sort==AT_STONE_THROWER)vision=VISION_STONE_THROWER;
    if(vision && b.inside(x,y) && b.inside(tx,ty)){
        int rise=(*b.info.theMap)[x][y].height-(*b.info.theMap)[tx][ty].height;
        if(rise>0)value=std::min(value+rise,double(vision+addition));
    }
    return value;
}
const tagArmy* enemy(const Bot& b,int sn){
    for(const auto& a:b.info.enemy_armies)if(a.SN==sn && a.Blood>0 && a.Sort!=AT_SHIP)return &a;
    return nullptr;
}
bool siegeShot(const Bot& b,Pos target){
    if(b.s.factorySN>=0 && reachDist(target,b.s.factory)<sizeOf(BUILDING_SIEGE)*.5+2.5)return false;
    for(const auto& h:b.humans)if(reachDist(target,pos(*h.second))<2.2)return false;
    return true;
}
void hold(Bot& b,const tagHuman& h,int priority=88){
    if(h.WorkObjectSN<0 && h.NowState!=HUMAN_STATE_WALKING)return;
    if(b.now-b.s.units[h.SN].sent<frames(.8))return;
    Order stop;stop.kind=Go;stop.sn=h.SN;stop.point=pos(h);stop.priority=priority;b.propose(stop);
}
bool towerSelects(const tagBuilding& tower,Pos p){
    int dx=tower.BlockDR-int(p.x),dy=tower.BlockUR-int(p.y);
    return dx*dx+dy*dy<=int(DIS_ARROWTOWER)*int(DIS_ARROWTOWER);
}
const tagHuman* towerTarget(const Bot& b,const tagBuilding& tower){
    auto h=b.humans.find(tower.Project);
    if(h==b.humans.end() || h->first==b.hero->SN || h->second->Blood<=0 || !towerSelects(tower,pos(*h->second)))return nullptr;
    return h->second;
}
double towerDps(const Bot& b,const tagHuman& target){
    int defense=0;
    const tagArmy* soldier=nullptr;for(auto a:b.army)if(a->SN==target.SN)soldier=a;
    if(soldier){
        if(soldier->Sort==AT_SLINGER)defense=DEFSHOOT_SLINGER;
        if(soldier->Sort==AT_HOPLITE)defense=DEFSHOOT_HOPLITE;
        if(soldier->Sort==AT_CLUBMAN || soldier->Sort==AT_SWORDSMAN || soldier->Sort==AT_BROADSWORDSMAN ||
            soldier->Sort==AT_HOPLITE || soldier->Sort==AT_SLINGER){
            auto u=b.s.units.find(target.SN);
            const bool enemyShield=u!=b.s.units.end() && u->second.converted &&
                (EnemyTechnologyMaxCivilization<0 || EnemyTechnologyMaxCivilization>=CIVILIZATION_BRONZEAGE);
            if(enemyShield || b.level(BUILDING_STOCK_UPGRADE_MISSILE_DEFENSE_INFANTRY)>0)
                defense+=BUILDING_STOCK_UPGRADE_MISSILE_DEFENSE_INFANTRY_ADDITION_DEFENSE_INFANTRY;
        }
    }
    const bool upgraded=EnemyTechnologyMaxCivilization<0 || EnemyTechnologyMaxCivilization>=CIVILIZATION_BRONZEAGE;
    const int attack=ATK_BUILD_ARROWTOWER+(upgraded?BUILDING_GRANARY_UPGRADE_ARROWTOWER_ADDITION_ATK:0);
    const double interval=std::max(.2,double((upgraded?THROWMISSION_ARROWTOWER_UPGRADED:THROWMISSION_ARROWTOWN_TIMER)+1)*TimePerFrame/1000);
    return std::max(1,attack-defense)/interval;
}
double routeLength(const std::vector<Pos>& corridor){
    double value=0;for(size_t i=1;i<corridor.size();++i)value+=dist(corridor[i-1],corridor[i]);return value;
}
bool finalCorridor(Bot& b,std::vector<Pos>& corridor,bool weighted=true){
    b.grid();
    const Pos origin=pos(*b.hero);int ox=int(origin.x),oy=int(origin.y);
    if(!b.inside(ox,oy) || !b.land(ox,oy))return false;
    std::vector<double> exposure(b.width*b.height,0),cost(b.width*b.height,1e30);
    std::vector<int> parent(b.width*b.height,-1);
    std::vector<unsigned char> available=b.walkable;
    if(!weighted){
        auto mark=[&](Pos p){if(b.inside(int(p.x),int(p.y)) && (int(p.x)!=ox || int(p.y)!=oy))available[int(p.x)*b.height+int(p.y)]=0;};
        for(const auto& h:b.humans)if(h.first!=b.hero->SN)mark(pos(*h.second));
        for(const auto& h:b.info.enemy_armies)if(h.Blood>0)mark(pos(h));
        for(const auto& h:b.info.enemy_farmers)if(h.Blood>0)mark(pos(h));
        for(const auto& r:b.info.resources)if(r.Cnt>0 &&
            (r.Type==RESOURCE_GAZELLE || r.Type==RESOURCE_ELEPHANT || r.Type==RESOURCE_LION))mark(pos(r));
    }
    if(weighted)for(const auto& tower:b.info.enemy_buildings)if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER){
        const tagHuman* locked=towerTarget(b,tower);
        for(int x=std::max(0,tower.BlockDR-int(DIS_ARROWTOWER));x<std::min(b.width,tower.BlockDR+int(DIS_ARROWTOWER)+1);++x)
            for(int y=std::max(0,tower.BlockUR-int(DIS_ARROWTOWER));y<std::min(b.height,tower.BlockUR+int(DIS_ARROWTOWER)+1);++y)
                if(towerSelects(tower,Pos(x+.5,y+.5)))exposure[x*b.height+y]+=locked && locked->Blood>=30?.35:8;
    }
    struct Node{double cost;int cell;bool operator<(const Node& other)const{return cost>other.cost;}};
    std::priority_queue<Node> queue;
    const int start=ox*b.height+oy;
    auto seed=[&](int x,int y){if(!b.inside(x,y) || !available[x*b.height+y] ||
        std::abs((*b.info.theMap)[x][y].height-(*b.info.theMap)[ox][oy].height)>1)return;
        int index=x*b.height+y;cost[index]=dist(origin,Pos(x+.5,y+.5));queue.push(Node{cost[index],index});};
    if(available[start])seed(ox,oy);
    else for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)if(std::abs(dx)+std::abs(dy)==1)seed(ox+dx,oy+dy);
    int finish=-1,n=sizeOf(BUILDING_SIEGE),sx=int(b.s.leash.x),sy=int(b.s.leash.y);
    while(!queue.empty()){
        Node current=queue.top();queue.pop();if(current.cost>cost[current.cell]+.001)continue;
        int x=current.cell/b.height,y=current.cell%b.height;
        if(x>=sx-1 && x<=sx+n && y>=sy-1 && y<=sy+n &&
            reachDist(Pos(x+.5,y+.5),b.s.factory)<=n*.5+1){finish=current.cell;break;}
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int nx=x+dx,ny=y+dy;
            if((!dx && !dy) || !b.inside(nx,ny) || !available[nx*b.height+ny] ||
                (dx && dy && (!available[nx*b.height+y] || !available[x*b.height+ny])) ||
                std::abs((*b.info.theMap)[x][y].height-(*b.info.theMap)[nx][ny].height)>1)continue;
            int next=nx*b.height+ny;double value=current.cost+(dx && dy?std::sqrt(2.0):1)+exposure[next];
            if(value>=cost[next])continue;cost[next]=value;parent[next]=current.cell;queue.push(Node{value,next});
        }
    }
    if(finish<0)return false;
    std::vector<Pos> reverse;
    for(int current=finish;current>=0;current=parent[current])reverse.push_back(Pos(current/b.height+.5,current%b.height+.5));
    corridor.push_back(origin);for(auto it=reverse.rbegin();it!=reverse.rend();++it)if(dist(*it,corridor.back())>.1)corridor.push_back(*it);
    return true;
}
bool stationaryTowerShield(const Bot& b,const tagBuilding& tower,int minimum=30){
    const tagHuman* target=towerTarget(b,tower);
    if(!target || target->Blood<minimum || target->NowState==HUMAN_STATE_WALKING)return false;
    auto order=b.orders.find(target->SN);
    return order==b.orders.end() || order->second.kind!=Go || dist(order->second.point,pos(*target))<=.15;
}
bool shieldCapacity(const Bot& b,const std::vector<const tagBuilding*>& relevant,const std::vector<Pos>& corridor){
    if(relevant.empty())return true;
    const double travelPerBlock=double(BLOCKSIDELENGTH)/std::max(.1,double(SPEED_PRIEST))*TimePerFrame/1000;
    std::vector<double> arrival(corridor.size(),0);
    for(size_t i=1;i<corridor.size();++i)arrival[i]=arrival[i-1]+dist(corridor[i-1],corridor[i])*travelPerBlock;
    const double finish=(arrival.empty()?0:arrival.back())+6+
        std::max(0,b.hero->ConvertCooldown)/1000.+.8;
    std::map<int,double> incoming;
    for(const auto& tower:b.info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER){
        const tagHuman* target=towerTarget(b,tower);if(target)incoming[target->SN]+=towerDps(b,*target);
    }
    double damage=0;
    for(auto tower:relevant){
        const tagHuman* target=towerTarget(b,*tower);if(!target || target->Blood<12)return false;
        const double lifetime=target->Blood/std::max(.1,incoming[target->SN]);
        int first=0;while(first<int(corridor.size()) && !towerSelects(*tower,corridor[first]))++first;
        const double firstArrival=first<int(arrival.size())?arrival[first]:finish;
        const double exposed=std::max(0.0,finish-std::max(lifetime,firstArrival));
        damage+=exposed*towerDps(b,*b.hero);
    }
    return damage<std::max(0,b.hero->Blood-30);
}
// Once guards are clear and a route exists, up to two towers never delay
// the finish. A healthy priest also accepts three without waiting for shields.
bool forceConversionReady(const Bot& b){
    if(!b.hero || b.hero->Blood<=0 || b.s.factorySN<0)return false;
    const tagBuilding* factory=nullptr;
    for(const auto& building:b.info.enemy_buildings)if(building.SN==b.s.factorySN &&
        building.Type==BUILDING_SIEGE && building.Blood>0 && building.Percent>=100)factory=&building;
    if(!factory)return false;
    const int towers=b.remainingEnemyTowers();
    return towers<=2 || (towers==3 && b.hero->Blood>=60);
}
// Evaluate the final walk without requiring escorts. Cleared guards and a
// stable clear interval remain mandatory at the caller. With fewer than four
// towers, accept an earlier, deliberately risky conversion while still budgeting damage.
bool directConversionSafe(const Bot& b,const std::vector<Pos>& plain){
    if(!b.hero || b.s.factorySN<0 || b.hero->Blood<=1 || plain.empty() ||
        dist(plain.front(),pos(*b.hero))>.3 || routeLength(plain)>24 ||
        b.walkable.size()!=size_t(b.width*b.height))return false;
    const tagBuilding* factory=nullptr;
    for(const auto& building:b.info.enemy_buildings)if(building.SN==b.s.factorySN &&
        building.Type==BUILDING_SIEGE && building.Blood>0 && building.Percent>=100)factory=&building;
    if(!factory || reachDist(plain.back(),pos(*factory))>sizeOf(BUILDING_SIEGE)*.5+1)return false;
    const double travelPerBlock=double(BLOCKSIDELENGTH)/std::max(.1,double(SPEED_PRIEST))*TimePerFrame/1000;
    const double travel=routeLength(plain)*travelPerBlock;
    const double cooldown=std::max(0,b.hero->ConvertCooldown)/1000.;
    int finishTowers=0;
    for(const auto& tower:b.info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER &&
        (dist(pos(tower),pos(*factory))<=32 || tower.Project==b.hero->SN))++finishTowers;
    const bool fewTowers=finishTowers<4;
    // For up to three towers, health, walking and unspent cooldown are checked
    // together in the exposure budget below, without an extra fixed HP floor.
    const bool rush=fewTowers || (finishTowers==4 && b.hero->Blood>=90 &&
        routeLength(plain)<=12 && cooldown<=travel+.5);
    // Cooldown continues while walking. Once in range, conversion takes at
    // most six seconds; keep transition slack and an extra arrow per tower.
    const double finish=std::max(travel,cooldown)+6+.2;
    // With zero to three towers budget the fastest two-second roll; four
    // towers still require three seconds and high health. A slow conversion
    // roll can lose the priest after commitment.
    const double rushFinish=std::max(travel,cooldown)+(fewTowers?2.:3.)+.2;
    const bool safeInTime=b.seconds+finish+1<GAME_LOSE_SEC;
    const bool rushInTime=rush && b.seconds+rushFinish+1<GAME_LOSE_SEC;
    if(!safeInTime && !rushInTime)return false;
    const int radius=int(DIS_ARROWTOWER);
    std::vector<std::pair<Pos,double>> samples;
    samples.push_back(std::make_pair(plain.front(),0.));double arrival=0;
    for(size_t i=1;i<plain.size();++i){
        const double length=dist(plain[i-1],plain[i]);if(length>1.6)return false;
        int count=std::max(1,int(std::ceil(length/.25)));
        for(int j=1;j<=count;++j){const double part=double(j)/count;
            Pos q(plain[i-1].x+(plain[i].x-plain[i-1].x)*part,plain[i-1].y+(plain[i].y-plain[i-1].y)*part);
            samples.push_back(std::make_pair(q,arrival+length*part*travelPerBlock));
        }
        arrival+=length*travelPerBlock;
    }
    int lowest=INT_MAX,previousX=int(plain.front().x),previousY=int(plain.front().y);
    for(const auto& sample:samples){
        const Pos q=sample.first;int x=int(q.x),y=int(q.y);
        if(!b.land(x,y) || !b.walkable[x*b.height+y] ||
            std::abs((*b.info.theMap)[x][y].height-(*b.info.theMap)[previousX][previousY].height)>1 ||
            (x!=previousX && y!=previousY &&
             (!b.walkable[x*b.height+previousY] || !b.walkable[previousX*b.height+y])))return false;
        for(const auto& human:b.humans)if(human.first!=b.hero->SN &&
            int(pos(*human.second).x)==x && int(pos(*human.second).y)==y)return false;
        for(const auto& human:b.info.enemy_armies)if(human.Blood>0 && int(pos(human).x)==x && int(pos(human).y)==y)return false;
        for(const auto& human:b.info.enemy_farmers)if(human.Blood>0 && int(pos(human).x)==x && int(pos(human).y)==y)return false;
        for(const auto& animal:b.info.resources)if(animal.Cnt>0 &&
            (animal.Type==RESOURCE_GAZELLE || animal.Type==RESOURCE_ELEPHANT || animal.Type==RESOURCE_LION) &&
            int(pos(animal).x)==x && int(pos(animal).y)==y)return false;
        lowest=std::min(lowest,(*b.info.theMap)[x][y].height);previousX=x;previousY=y;
        for(int dx=-radius;dx<=radius;++dx)for(int dy=-radius;dy<=radius;++dy){
            if(dx*dx+dy*dy>radius*radius || !b.inside(x+dx,y+dy))continue;
            if((*b.info.theMap)[x+dx][y+dy].type==MAPPATTERN_UNKNOWN)return false;
        }
    }
    const bool upgraded=EnemyTechnologyMaxCivilization<0 || EnemyTechnologyMaxCivilization>=CIVILIZATION_BRONZEAGE;
    const double period=std::max(.2,double((upgraded?THROWMISSION_ARROWTOWER_UPGRADED:THROWMISSION_ARROWTOWN_TIMER)+1)*TimePerFrame/1000);
    auto arrowDamage=[&](const tagBuilding& tower,const tagHuman& target,int ground){
        int rise=0;if(b.inside(tower.BlockDR,tower.BlockUR))rise=std::max(0,(*b.info.theMap)[tower.BlockDR][tower.BlockUR].height-ground);
        return std::ceil(towerDps(b,target)*period-.00001)+rise;
    };
    auto fixedTarget=[&](const tagBuilding& tower)->const tagHuman*{
        const tagHuman* target=towerTarget(b,tower);
        if(!target || target->NowState==HUMAN_STATE_WALKING)return nullptr;
        auto order=b.orders.find(target->SN);
        if(order!=b.orders.end() && order->second.kind==Go && dist(order->second.point,pos(*target))>.3)return nullptr;
        return target;
    };
    std::map<int,double> incoming,inFlight;
    for(const auto& tower:b.info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER){
        const tagHuman* target=fixedTarget(tower);if(!target)continue;
        const Pos p=pos(*target);if(!b.inside(int(p.x),int(p.y)))continue;
        const double shot=arrowDamage(tower,*target,(*b.info.theMap)[int(p.x)][int(p.y)].height);
        incoming[target->SN]+=shot/period;inFlight[target->SN]+=shot;
    }
    double damage=0,rushDamage=0;
    // Enemy towers refresh an out-of-range target at most every 20 frames.
    // Integrate each real coverage interval instead of charging from the first
    // touched tower cell until the final conversion, even after leaving it.
    const double releaseDelay=std::max(.8,20.*double(TimePerFrame)/1000.);
    for(const auto& tower:b.info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER){
        std::vector<std::pair<double,double>> exposure;
        auto cover=[&](double begin,double end){
            end=std::min(finish,end+releaseDelay);
            if(end<=begin)return;
            if(!exposure.empty() && begin<=exposure.back().second)
                exposure.back().second=std::max(exposure.back().second,end);
            else exposure.push_back(std::make_pair(begin,end));
        };
        // A lock visible in this snapshot may still launch arrows until the
        // next native refresh even if the priest has just left the circle.
        if(tower.Project==b.hero->SN)cover(0,0);
        for(size_t i=1;i<samples.size();++i){
            const Pos a=samples[i-1].first,q=samples[i].first;
            bool covered=towerSelects(tower,a) || towerSelects(tower,q);
            if(int(a.x)!=int(q.x) && int(a.y)!=int(q.y))
                covered=covered || towerSelects(tower,Pos(a.x,q.y)) || towerSelects(tower,Pos(q.x,a.y));
            if(covered)cover(samples[i-1].second,samples[i].second);
        }
        if(towerSelects(tower,plain.back()))cover(arrival,finish);
        if(exposure.empty())continue;
        double lifetime=0;const tagHuman* target=fixedTarget(tower);
        if(target && incoming[target->SN]>0)
            lifetime=std::max(0.,double(target->Blood)-inFlight[target->SN])/incoming[target->SN];
        const double shot=arrowDamage(tower,*b.hero,lowest);
        for(const auto& interval:exposure){
            const double exposed=std::max(0.,interval.second-std::max(interval.first,lifetime));
            if(exposed>0)damage+=(std::ceil(exposed/period)+1)*shot;
            const double rushExposed=std::max(0.,std::min(interval.second,rushFinish)-std::max(interval.first,lifetime));
            if(rushExposed>0)rushDamage+=std::ceil(rushExposed/period)*shot;
        }
    }
    return (safeInTime && damage<double(b.hero->Blood)-1.) ||
        (rushInTime && rushDamage<double(b.hero->Blood)-1.);
}

bool escapeStep(Bot& b,Pos origin,Pos wanted,Pos& step){
    const int ox=int(origin.x),oy=int(origin.y),start=ox*b.height+oy;
    if(!b.inside(ox,oy) || !b.land(ox,oy))return false;
    const double initial=b.danger(origin,4),radius=dist(origin,b.s.leash);
    std::vector<unsigned char> bodies(b.width*b.height,0);
    auto markBody=[&](Pos p){if(b.inside(int(p.x),int(p.y)))bodies[int(p.x)*b.height+int(p.y)]=1;};
    for(const auto& h:b.humans)if(h.first!=b.hero->SN)markBody(pos(*h.second));
    for(const auto& h:b.info.enemy_armies)if(h.Blood>0)markBody(pos(h));
    for(const auto& h:b.info.enemy_farmers)if(h.Blood>0)markBody(pos(h));
    for(const auto& r:b.info.resources)if(r.Cnt>0 &&
        (r.Type==RESOURCE_GAZELLE || r.Type==RESOURCE_ELEPHANT || r.Type==RESOURCE_LION))markBody(pos(r));
    auto open=[&](int x,int y){
        if(!b.inside(x,y) || std::abs(x-ox)>8 || std::abs(y-oy)>8 ||
            (!b.walkable[x*b.height+y] && (x!=ox || y!=oy)) || bodies[x*b.height+y])return false;
        if(b.s.guardScreenActive){
            const double next=dist(Pos(x+.5,y+.5),b.s.leash);
            if((radius>=27 && next<27) || (radius<27 && next<radius-.25))return false;
        }
        return true;
    };
    struct Node{
        double peak,cost;int cell;
        bool operator<(const Node& other)const{
            return peak>other.peak || (peak==other.peak && cost>other.cost);
        }
    };
    std::priority_queue<Node> queue;
    std::vector<double> peak(b.width*b.height,1e30),cost(b.width*b.height,1e30);
    std::vector<int> first(b.width*b.height,-1);
    peak[start]=initial;cost[start]=0;queue.push(Node{initial,0,start});
    double best=1e30;int selected=-1;
    while(!queue.empty()){
        Node current=queue.top();queue.pop();
        if(current.peak>peak[current.cell]+.001 || current.cost>cost[current.cell]+.001)continue;
        int x=current.cell/b.height,y=current.cell%b.height;Pos point(x+.5,y+.5);
        const double risk=b.danger(point,4);
        if(first[current.cell]>=0 && risk<initial-.5 &&
            (!b.s.guardScreenActive || dist(point,b.s.leash)>=27)){
            double score=current.peak*160+risk*80+current.cost*.2+dist(point,wanted)*.1;
            if(score<best){best=score;selected=first[current.cell];}
        }
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){
            int nx=x+dx,ny=y+dy;
            if((!dx && !dy) || !open(nx,ny) || (dx && dy && (!open(nx,y) || !open(x,ny))) ||
                std::abs((*b.info.theMap)[x][y].height-(*b.info.theMap)[nx][ny].height)>1)continue;
            int next=nx*b.height+ny;
            const double exposure=b.danger(Pos(nx+.5,ny+.5),4);
            const double nextPeak=std::max(current.peak,exposure),nextCost=current.cost+1+exposure*.2;
            // A brief lateral detour is acceptable only when the complete route reaches lower exposure.
            if(nextPeak>initial+1.5 || nextPeak>peak[next]+.001 ||
                (std::abs(nextPeak-peak[next])<.001 && nextCost>=cost[next]))continue;
            peak[next]=nextPeak;cost[next]=nextCost;first[next]=current.cell==start?next:first[current.cell];
            queue.push(Node{nextPeak,nextCost,next});
        }
    }
    if(selected<0)return false;
    step=Pos(selected/b.height+.5,selected%b.height+.5);return true;
}
}

namespace combat {
// The caller decides whether a group without a safe shot should wait or charge.
// This helper never changes that decision, dispatches reinforcements, or moves a priest.
struct TowerFirePlan {
    bool valid=false,firingPosition=false,escaping=false,waiting=false,attack=false;
    int target=-1;
    Pos point,goal;
    int steps=0;
};
bool towerFireCovered(const Bot& b,Pos p){
    for(const auto& tower:b.info.enemy_buildings)
        if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER && towerSelects(tower,p))return true;
    return false;
}
bool towerFireShot(const Bot& b,const tagArmy& a,const tagBuilding& tower,Pos p){
    if(tower.Blood<=0 || tower.Percent<100 || tower.Type!=BUILDING_ARROWTOWER)return false;
    const double distance=reachDist(p,pos(tower));
    if(distance>bowRange(b,a,p,pos(tower))+.001)return false;
    return a.Sort!=AT_STONE_THROWER ||
        (distance>=double(DIS_MIN_STONE_THROWER) && siegeShot(b,pos(tower)));
}
TowerFirePlan planTowerFire(Bot& b,const tagArmy& a,const tagBuilding& target,Pos waitPoint){
    TowerFirePlan result;
    const bool shot=ranged(a.Sort) || a.Sort==AT_STONE_THROWER;
    if((!shot && !melee(a.Sort)) || target.Blood<=0 ||
        target.Type!=BUILDING_ARROWTOWER || dist(pos(a),pos(target))>28)return result;
    b.grid();const Pos origin=pos(a);const int ox=int(origin.x),oy=int(origin.y);
    if(!b.inside(ox,oy) || !b.land(ox,oy))return result;
    const bool initiallyCovered=towerFireCovered(b,origin);
    result.point=result.goal=origin;result.target=target.SN;
    auto shoot=[&](const tagBuilding& tower){
        result.valid=true;result.firingPosition=true;result.attack=true;result.target=tower.SN;
    };
    // Keep a valid building attack even when the engine reports it as WORKING.
    // In particular, another nearby tower must not restart a safe firing relation.
    if(shot && !initiallyCovered){
        for(const auto& tower:b.info.enemy_buildings)
            if(tower.SN==a.WorkObjectSN && towerFireShot(b,a,tower,origin)){shoot(tower);return result;}
        if(towerFireShot(b,a,target,origin)){shoot(target);return result;}
    }
    const int x0=std::max(0,ox-24),x1=std::min(b.width-1,ox+24);
    const int y0=std::max(0,oy-24),y1=std::min(b.height-1,oy+24);
    const int height=y1-y0+1,total=(x1-x0+1)*height;
    auto index=[&](int x,int y){return (x-x0)*height+y-y0;};
    auto inside=[&](int x,int y){return x>=x0 && x<=x1 && y>=y0 && y<=y1;};
    auto point=[&](int cell){return Pos(cell/height+x0+.5,cell%height+y0+.5);};
    std::vector<unsigned char> open(total,0);
    std::vector<int> hits(total,0),parent(total,-1),depth(total,0);
    std::vector<double> cost(total,1e30);
    std::vector<const tagBuilding*> towers;
    for(const auto& tower:b.info.enemy_buildings)
        if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER)towers.push_back(&tower);
    std::vector<std::vector<unsigned char>> cover(towers.size(),std::vector<unsigned char>(total,0));
    for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y){
        const int cell=index(x,y);open[cell]=b.land(x,y) && b.walkable[x*b.height+y];
        for(size_t t=0;t<towers.size();++t)if(towerSelects(*towers[t],point(cell))){cover[t][cell]=1;++hits[cell];}
    }
    auto block=[&](int sn,Pos p){
        if(sn!=a.SN && inside(int(p.x),int(p.y)))open[index(int(p.x),int(p.y))]=0;
    };
    for(const auto& human:b.humans)block(human.first,pos(*human.second));
    for(const auto& human:b.info.enemy_armies)if(human.Blood>0)block(human.SN,pos(human));
    for(const auto& human:b.info.enemy_farmers)if(human.Blood>0)block(human.SN,pos(human));
    for(const auto& order:b.orders)if(order.second.kind==Go)block(order.first,order.second.point);
    const int start=index(ox,oy);open[start]=1;
    // Once a tower has lost this unit, no later step may enter its selection circle.
    // A unit already covered by several towers may leave them one at a time.
    auto subset=[&](int from,int to){
        for(size_t t=0;t<towers.size();++t)if(cover[t][to] && !cover[t][from])return false;
        return true;
    };
    auto edge=[&](int from,int nx,int ny){
        const Pos p=point(from);const int x=int(p.x),y=int(p.y);
        if(!inside(nx,ny))return false;const int next=index(nx,ny);
        if(!open[next] || !subset(from,next) ||
            std::abs((*b.info.theMap)[x][y].height-(*b.info.theMap)[nx][ny].height)>1)return false;
        if(nx!=x && ny!=y){
            const int side1=index(nx,y),side2=index(x,ny);
            if(!open[side1] || !open[side2] || !subset(from,side1) || !subset(from,side2))return false;
        }
        return true;
    };
    struct Node{double cost;int cell;bool operator<(const Node& other)const{
        return cost!=other.cost?cost>other.cost:cell>other.cell;
    }};
    std::priority_queue<Node> queue;cost[start]=0;queue.push(Node{0,start});
    int fire=-1,escape=-1,waiting=-1;double escapeScore=1e30,waitScore=1e30;
    while(!queue.empty()){
        const Node current=queue.top();queue.pop();
        if(current.cost>cost[current.cell]+.0001)continue;
        const Pos p=point(current.cell);const int x=int(p.x),y=int(p.y);
        if(hits[current.cell]==0){
            const double score=current.cost+dist(p,waitPoint)*.015;
            if(score<escapeScore){escapeScore=score;escape=current.cell;}
            const double gatherScore=dist(p,waitPoint)*4+current.cost*.05;
            if(gatherScore<waitScore){waitScore=gatherScore;waiting=current.cell;}
            if(shot && towerFireShot(b,a,target,p)){fire=current.cell;break;}
        }
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){
            if((!dx && !dy) || !edge(current.cell,x+dx,y+dy))continue;
            const int next=index(x+dx,y+dy);
            const double value=current.cost+(dx && dy?1.41421356237:1)*(1+hits[next]*12);
            if(value>=cost[next]-.0001)continue;
            cost[next]=value;parent[next]=current.cell;depth[next]=depth[current.cell]+1;queue.push(Node{value,next});
        }
    }
    const int goal=fire>=0?fire:(initiallyCovered?escape:waiting);
    if(goal<0)return result;
    result.valid=true;result.firingPosition=fire>=0;result.escaping=initiallyCovered;
    result.goal=point(goal);result.steps=depth[goal];
    if(goal==start){
        if(fire>=0 && towerFireShot(b,a,target,origin) && !initiallyCovered){shoot(target);return result;}
        // The floor-cell center can have a different range than the unit itself.
        if(fire>=0 && dist(origin,point(start))>.05){result.point=point(start);return result;}
        result.waiting=true;return result;
    }
    int step=goal;while(parent[step]>=0 && parent[step]!=start)step=parent[step];
    result.point=point(step);
    // Preserve the in-progress adjacent landing rather than turn at every floor
    // boundary. Validate it against the same coverage, terrain and crowd rules.
    const Unit& unit=b.s.units[a.SN];
    const Pos native(a.DR0/double(BLOCKSIDELENGTH),a.UR0/double(BLOCKSIDELENGTH));
    const int nx=int(native.x),ny=int(native.y);
    if(unit.last.kind==Go && unit.last.priority==94 && a.NowState==HUMAN_STATE_WALKING &&
        b.now-unit.moved<frames(4) && dist(origin,native)>.12 && dist(origin,native)<1.6 &&
        dist(native,unit.last.point)<.1 && inside(nx,ny) &&
        (index(nx,ny)==start || edge(start,nx,ny)))result.point=native;
    return result;
}
bool applyTowerFire(Bot& b,const tagArmy& a,const TowerFirePlan& plan,int priority=94){
    if(!plan.valid)return false;
    const Pos current=pos(a);Unit& unit=b.s.units[a.SN];
    if(plan.attack){
        if(a.WorkObjectSN==plan.target &&
            (a.NowState==HUMAN_STATE_WORKING || a.NowState==HUMAN_STATE_ATTACKING))return true;
        Order attack;attack.kind=Use;attack.sn=a.SN;attack.target=plan.target;attack.priority=priority;
        b.propose(attack);return true;
    }
    if(plan.waiting){hold(b,a,priority);return true;}
    const Pos native(a.DR0/double(BLOCKSIDELENGTH),a.UR0/double(BLOCKSIDELENGTH));
    if(dist(current,plan.point)<.05 || (a.NowState==HUMAN_STATE_WALKING &&
        dist(native,plan.point)<.1 && b.now-unit.moved<frames(4)) || b.now-unit.sent<frames(.25))return true;
    // Use this exact checked neighbor: Bot::move can move a crowded landing into
    // an unchecked tower circle, defeating a safe endpoint/path calculation.
    Order go;go.kind=Go;go.sn=a.SN;go.point=plan.point;go.priority=priority;b.propose(go);return true;
}
}

namespace combat {
struct TowerRegroupPlan {
    bool active=false,released=false;
    Pos rally=Pos(-1,-1);
    int ready=0,front=0;
};
// The caller skips this helper when the priest will commit this frame. A
// negative rally means no verified landing: keep exposure disabled and permit
// only safe ranged fire, rather than falling back to a one-unit tower charge.
TowerRegroupPlan towerRegroup(Bot& b,Pos center,int release=7,int reform=2){
    TowerRegroupPlan plan;
    if(b.s.phase==Defend || b.s.committed || b.s.factorySN<0 || !b.guardsClear())return plan;
    std::vector<const tagBuilding*> local;
    for(const auto& tower:b.info.enemy_buildings)if(tower.Blood>0 && tower.Percent>=100 &&
        tower.Type==BUILDING_ARROWTOWER && dist(pos(tower),b.s.factory)<32)local.push_back(&tower);
    if(local.size()<2){b.s.towerWaveReleased=false;return plan;}
    plan.active=true;b.grid();
    if(b.s.towerRallyFactory!=b.s.factorySN){
        b.s.towerRallyFactory=b.s.factorySN;b.s.towerWaveReleased=false;b.s.towerRally=Pos(-1,-1);
    }
    const auto& approach=b.field(center);
    auto safe=[&](Pos p){
        int x=int(p.x),y=int(p.y);
        if(!b.land(x,y) || !b.walkable[x*b.height+y] || approach[x*b.height+y]<0 ||
            dist(p,b.base)<20 || dist(p,b.s.factory)>=dist(p,b.base) || dist(p,b.s.factory)>32)return false;
        // Keep the rally tied to the surviving towers, not to a demolished
        // outer tower whose old waiting point is now too far from the battle.
        double nearest=1e30;for(auto tower:local)nearest=std::min(nearest,dist(p,pos(*tower)));
        if(nearest>14)return false;
        // Three tiles beyond acquisition leave room for move()'s two-tile
        // crowded-landing adjustment without placing a regrouped bow in range.
        const int margin=int(DIS_ARROWTOWER)+3;
        for(const auto& tower:b.info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER){
            int dx=x-tower.BlockDR,dy=y-tower.BlockUR;if(dx*dx+dy*dy<=margin*margin)return false;
        }
        for(const auto& enemy:b.info.enemy_armies)if(enemy.Blood>0 && enemy.Sort!=AT_SHIP &&
            reachDist(p,pos(enemy))<enemyRange(enemy.Sort)+2)return false;
        const int radius=int(DIS_ARROWTOWER);
        for(int dx=-radius;dx<=radius;++dx)for(int dy=-radius;dy<=radius;++dy){
            if(dx*dx+dy*dy>radius*radius || !b.inside(x+dx,y+dy))continue;
            if((*b.info.theMap)[x+dx][y+dy].type==MAPPATTERN_UNKNOWN)return false;
        }
        return true;
    };
    if(!safe(b.s.towerRally)){
        Pos chosen(-1,-1);double best=1e30;
        for(int x=std::max(0,int(b.s.factory.x)-32);x<std::min(b.width,int(b.s.factory.x)+33);++x)
            for(int y=std::max(0,int(b.s.factory.y)-32);y<std::min(b.height,int(b.s.factory.y)+33);++y){
                Pos q(x+.5,y+.5);if(!safe(q))continue;
                double nearest=1e30;for(auto tower:local)nearest=std::min(nearest,dist(q,pos(*tower)));
                if(nearest>14)continue;
                const double score=approach[x*b.height+y]+dist(q,b.s.factory)*.35;
                if(score<best){best=score;chosen=q;}
            }
        b.s.towerRally=chosen;
    }
    if(b.s.towerRally.x<0){b.s.towerWaveReleased=false;return plan;}
    plan.rally=b.s.towerRally;
    for(auto soldier:b.army)if(b.s.dispatched.count(soldier->SN) &&
        soldier->Blood>=25 && soldier->Blood*2>=soldier->MaxBlood){
        const bool shot=ranged(soldier->Sort) || soldier->Sort==AT_STONE_THROWER;
        if(!shot && !melee(soldier->Sort))continue;
        const Pos p=pos(*soldier);
        bool canFight=false,atFront=false;
        for(auto tower:local){
            if(soldier->Sort==AT_STONE_THROWER && !siegeShot(b,pos(*tower)))continue;
            const double reach=shot?bowRange(b,*soldier,p,pos(*tower)):
                double(DISTANCE_ATTACK_CLOSE/BLOCKSIDELENGTH)+sizeOf(tower->Type)*.5;
            const double distance=reachDist(p,pos(*tower));
            const bool inRange=distance<=reach+.001 &&
                (soldier->Sort!=AT_STONE_THROWER || distance>=double(DIS_MIN_STONE_THROWER));
            // A visible but unreachable opposite bank is not an arrived fighter.
            const bool reachable=inRange || b.walkDistance(p,pos(*tower))<=12;
            if(!reachable)continue;canFight=true;
            if(distance<=(shot?reach+3:6))atFront=true;
        }
        if(!canFight)continue;
        const bool arrived=dist(p,plan.rally)<=6 && b.walkDistance(p,plan.rally)<=8;
        if(arrived)++plan.ready;
        if(arrived || atFront)++plan.front;
    }
    release=std::max(2,release);reform=std::max(0,std::min(reform,release-1));
    if(b.s.towerWaveReleased && plan.front<=reform)b.s.towerWaveReleased=false;
    else if(!b.s.towerWaveReleased && plan.ready>=release)b.s.towerWaveReleased=true;
    plan.released=b.s.towerWaveReleased;
    return plan;
}
}

Pos Bot::armyCenter()const{
    std::vector<const tagArmy*> main;
    for(auto a:army)if(s.dispatched.count(a->SN) &&
        (s.phase==Defend || dist(s.front,base)<20 || dist(pos(*a),s.front)<24))main.push_back(a);
    if(main.empty())return s.phase==Defend?s.assembly:s.front;
    const tagArmy* center=main.front();int most=-1;double best=1e30;
    for(auto a:main){
        int nearby=0;for(auto other:main)if(dist(pos(*a),pos(*other))<11)++nearby;
        double score=dist(pos(*a),focus);
        if((nearby>=3 && most<3) || (nearby>=3 && most>=3 && score<best) ||
            (nearby<3 && most<3 && nearby>most)){
            center=a;most=nearby;best=score;
        }
    }
    // Coordinate medians may lie in water; return an actual member of the front cluster.
    std::vector<double> xs,ys;
    for(auto a:main)if(dist(pos(*a),pos(*center))<11){xs.push_back(pos(*a).x);ys.push_back(pos(*a).y);}
    std::sort(xs.begin(),xs.end());std::sort(ys.begin(),ys.end());
    Pos middle(xs[xs.size()/2],ys[ys.size()/2]);best=1e30;
    for(auto a:main)if(dist(pos(*a),pos(*center))<11 && dist(pos(*a),middle)<best){best=dist(pos(*a),middle);center=a;}
    return pos(*center);
}
bool Bot::towerBusy(const tagBuilding& tower)const{
    auto h=humans.find(tower.Project);if(h==humans.end() || (hero && h->first==hero->SN))return false;
    int dx=tower.BlockDR-h->second->BlockDR,dy=tower.BlockUR-h->second->BlockUR;
    return h->second->Blood>=30 && dx*dx+dy*dy<=int(DIS_ARROWTOWER)*int(DIS_ARROWTOWER);
}
bool Bot::guardsClear()const{
    s.clearVeto=0;
    if(s.factorySN<0 || !hero){s.clearVeto=1;return false;}
    for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP &&
        (dist(pos(e),s.leash)<32 || e.WorkObjectSN==hero->SN ||
         reachDist(pos(e),pos(*hero))<enemyRange(e.Sort)+4)){s.clearVeto=2;return false;}
    for(const auto& e:s.enemies)if(now-e.second.seen<frames(25) && dist(e.second.point,s.leash)<32){
        bool covered=false;for(auto a:army)if(s.dispatched.count(a->SN) && dist(pos(*a),e.second.point)<2.5)covered=true;
        if(!covered){s.clearVeto=3;return false;}
    }
    // Seeing only the outer edge is insufficient evidence that inner guards have died.
    // Once escorts have actually inspected it, losing escorts to towers must not
    // erase that evidence; visible guards and their recent memory still veto it.
    if(s.guardsScouted)return true;
    int escorts=0;for(auto a:army)if(s.dispatched.count(a->SN) && dist(pos(*a),s.factory)<18)++escorts;
    if(escorts<2){s.clearVeto=4;return false;}return true;
}

void Bot::tactics(){
    if(!hero)return;
    if(s.factorySN<0){
        std::vector<const tagBuilding*> towers;
        const Pos mirror(width-base.x,height-base.y);
        for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER &&
            dist(pos(tower),base)>30 && dist(pos(tower),mirror)<dist(pos(tower),base))towers.push_back(&tower);
        int most=1;double nearest=1e30;Pos anchor;
        for(auto center:towers){int count=0;Pos sum;
            for(auto tower:towers)if(dist(pos(*tower),pos(*center))<18){++count;sum.x+=pos(*tower).x;sum.y+=pos(*tower).y;}
            if(count<2)continue;Pos average(sum.x/count,sum.y/count);double d=dist(average,mirror);
            if(count>most || (count==most && d<nearest)){most=count;nearest=d;anchor=average;}
        }
        if(most>=2)focus=anchor;
    }
    s.guardScreenActive=s.phase!=Defend && s.factorySN>=0 && !s.committed && !guardsClear();
    grid();
    const int left=GAME_LOSE_SEC-seconds;
    Pos direction(focus.x-base.x,focus.y-base.y);double length=std::max(1.0,dist(focus,base));
    direction=Pos(direction.x/length,direction.y/length);
    const tagBuilding* defenseTower=nullptr;double towerDistance=1e30;
    for(const auto& tower:info.buildings)if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER){
        double d=dist(pos(tower),rally);if(d<towerDistance){towerDistance=d;defenseTower=&tower;}
    }
    Pos defenseFacing=direction;const tagArmy* incoming=nullptr;double closest=1e30;
    for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP){
        double d=dist(pos(e),rally);if(d<closest && d<32){closest=d;incoming=&e;}
    }
    if(incoming){double d=std::max(1.0,dist(pos(*incoming),rally));defenseFacing=Pos((pos(*incoming).x-rally.x)/d,(pos(*incoming).y-rally.y)/d);}
    std::set<int> rescueTargets;const tagArmy* rescue=nullptr;double rescueScore=1e30;
    for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP && dist(pos(e),base)<29){
        auto victim=buildings.find(e.WorkObjectSN);
        bool attacking=victim!=buildings.end() && combat::important(victim->second->Type);
        const bool huntingHero=e.WorkObjectSN==hero->SN &&
            reachDist(pos(e),pos(*hero))<enemyRange(e.Sort)+6;
        if(!attacking && !huntingHero && dist(pos(e),base)>9)continue;
        rescueTargets.insert(e.SN);double score=dist(pos(e),base);
        if(huntingHero)score-=20;
        if(e.Sort==AT_STONE_THROWER)score-=10;
        if(attacking && victim->second->Type==BUILDING_CENTER)score-=15;
        if(score<rescueScore){rescueScore=score;rescue=&e;}
    }
    Pos defenseRear(rally.x-defenseFacing.x*6.5,rally.y-defenseFacing.y*6.5);
    if(s.phase==Defend && rescue){
        const tagArmy* support=nullptr;double best=1e30;
        for(auto a:army)if(ranged(a->Sort) && a->Blood*2>=a->MaxBlood && dist(pos(*a),base)<29){
            int allies=0;for(auto other:army)if(ranged(other->Sort) && dist(pos(*other),pos(*a))<8)++allies;
            if(allies<2)continue;double score=dist(pos(*a),pos(*rescue));
            if(score<best){best=score;support=a;}
        }
        if(support){Pos p=pos(*support),e=pos(*rescue);double d=std::max(1.0,dist(p,e));
            defenseRear=Pos(p.x+(p.x-e.x)*6.5/d,p.y+(p.y-e.y)*6.5/d);}
    }
    bool quietDefense=s.phase==Defend && !homeDanger && danger(pos(*hero),4)==0;
    if(rescue)quietDefense=false;
    for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP &&
        (dist(pos(e),rally)<24 || reachDist(pos(e),pos(*hero))<rangeOf(e.Sort)+12))quietDefense=false;
    const tagArmy* recovering=nullptr;
    if(quietDefense)for(auto a:army)if((ranged(a->Sort) || combat::melee(a->Sort)) &&
        a->Blood*2<a->MaxBlood && danger(pos(*a),3)==0){
        if(!recovering || a->SN==hero->WorkObjectSN)recovering=a;
        if(a->SN==hero->WorkObjectSN)break;
    }
    auto defensePoint=[&](Pos wanted){
        const auto& available=field(rally);Pos selected=rally;double best=1e30;
        for(int dx=-3;dx<=3;++dx)for(int dy=-3;dy<=3;++dy){
            int x=int(wanted.x)+dx,y=int(wanted.y)+dy;if(!inside(x,y) || !walkable[x*height+y] || available[x*height+y]<0)continue;
            Pos q(x+.5,y+.5);double score=dist(q,wanted)+danger(q,1)*2;
            if(score<best){best=score;selected=q;}
        }
        return selected;
    };
    if(!inside(int(s.assembly.x),int(s.assembly.y)) || dist(s.assembly,base)<8 || dist(s.assembly,base)>25){
        const auto& accessible=field(rally);double best=1e30;Pos wanted(base.x+direction.x*12,base.y+direction.y*12);
        s.assembly=safePoint(rally,wanted);
        for(int x=std::max(0,int(base.x)-19);x<std::min(width,int(base.x)+20);++x)
            for(int y=std::max(0,int(base.y)-19);y<std::min(height,int(base.y)+20);++y){
                Pos q(x+.5,y+.5);double radius=dist(q,base);
                if(radius<10 || radius>17 || accessible[x*height+y]<0 || occupied(q,2) || danger(q,3)>0)continue;
                double score=dist(q,wanted)+radius*.15;
                if(score<best){best=score;s.assembly=q;}
            }
    }

    // Towers become the first retaliation lock before the bowmen join the volley.
    std::set<int> towerTargets;
    for(const auto& tower:info.buildings)if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER){
        const tagArmy* target=combat::enemy(*this,tower.Project);double best=1e30;
        if(target && reachDist(pos(tower),pos(*target))>DIS_ARROWTOWER)target=nullptr;
        if(!target)for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP &&
            reachDist(pos(tower),pos(e))<=DIS_ARROWTOWER && !towerTargets.count(e.SN)){
            double score=dist(pos(tower),pos(e))+(e.Sort==AT_STONE_THROWER?-5:0);
            if(e.WorkObjectSN==hero->SN)score-=10;
            if(e.SN==hero->WorkObjectSN)score+=15;
            if(score<best){best=score;target=&e;}
        }
        if(target){towerTargets.insert(target->SN);action(tower.SN,target->SN,112);}
    }

    if(s.phase==Defend){
        int healthy=0,strong=0,available=0,healthyRanged=0,healthyHeavy=0;
        for(auto a:army)if((dist(pos(*a),base)<34 || dist(pos(*a),s.assembly)<24) &&
            walkDistance(pos(*a),s.assembly)<INT_MAX){
            ++available;if(a->Blood*2<a->MaxBlood)continue;++healthy;
            if(ranged(a->Sort))++healthyRanged;
            const bool heavy=a->Sort==AT_HOPLITE || a->Sort==AT_BROADSWORDSMAN ||
                a->Sort==AT_CAVALRY || a->Sort==AT_CHARIOT;
            if(heavy)++healthyHeavy;
            if(heavy || a->Sort==AT_STONE_THROWER || a->Sort==AT_COMPOSITE_BOWMAN ||
                a->Sort==AT_IMPROVED || a->Sort==AT_CHARIOT_ARCHER)++strong;
        }
        s.launchAvailable=available;s.launchHealthy=healthy;s.launchStrong=strong;
        const bool full=info.Human_MaxNum>=48 && info.Human_Num>=info.Human_MaxNum-1;
        if(full && s.fullSince<0)s.fullSince=now;
        if(!full)s.fullSince=-1;
        // The scripted third wave is frame-based, independent of simulation speed.
        // Its local enemies must disappear before the army and priest leave together.
        const int thirdWaveFrame=21000;
        bool raidPressure=homeDanger;
        if(now>=thirdWaveFrame){
            for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP){
                bool homeTarget=false;
                auto h=humans.find(e.WorkObjectSN);
                if(h!=humans.end() && dist(pos(*h->second),base)<34)homeTarget=true;
                auto b=buildings.find(e.WorkObjectSN);
                if(b!=buildings.end() && dist(pos(*b->second),base)<34)homeTarget=true;
                if(dist(pos(e),base)<32 || homeTarget)raidPressure=true;
            }
            if(raidPressure){s.thirdRaidSeen=true;s.thirdRaidClearSince=-1;}
            else if(s.thirdRaidSeen || now>=thirdWaveFrame+frames(120)){
                // The fallback also handles a wave with no surviving eligible attackers.
                if(s.thirdRaidClearSince<0)s.thirdRaidClearSince=now;
            }
        }
        const bool raidsCleared=now>=thirdWaveFrame && s.thirdRaidClearSince>=0 &&
            now-s.thirdRaidClearSince>=frames(3);
        const bool firepower=healthyRanged>=4 || healthyHeavy>=2;
        s.launchReason=0;
        // Leave as soon as the third wave clears. Converted units count; no
        // full-population, optional-technology or late clock threshold remains.
        if(raidsCleared && !raidPressure && firepower && available>=8 && healthy>=6 && strong>=4){
            s.launchReason=4;
            s.phase=Advance;s.front=s.assembly;s.march=s.assembly;s.marchTime=-10000;
            for(auto a:army)s.dispatched.insert(a->SN);
        }
    }
    Pos center=armyCenter();
    int mainNear=0;for(auto a:army)if(s.dispatched.count(a->SN) && dist(pos(*a),center)<12)++mainNear;
    if(s.phase!=Defend && mainNear>=3 && dist(center,base)>20)s.front=center;
    if(s.phase!=Defend){
        for(auto a:army)if(!s.dispatched.count(a->SN))s.batch.insert(a->SN);
        if(!s.batch.empty() && s.batchTime<0)s.batchTime=now;
        const int required=left<=180?2:3;
        if(int(s.batch.size())>=required || (!s.batch.empty() && now-s.batchTime>=frames(15))){
            s.dispatched.insert(s.batch.begin(),s.batch.end());s.batch.clear();s.batchTime=-1;
        }
        if(dist(center,base)<18 && dist(s.front,base)>20)center=s.front;
        double distance=dist(center,focus);
        if(distance<s.marchDistance-.7){s.marchDistance=distance;s.marchProgress=now;}
        if(now-s.marchTime>=frames(5) || dist(s.march,center)<3 || now-s.marchProgress>=frames(8)){
            const bool connected=walkDistance(center,focus)<INT_MAX;
            if(!connected && dist(s.march,center)<3 && now-s.marchProgress>=frames(6))
                s.blockedFrontiers[int(s.march.x)*height+int(s.march.y)]=now+frames(180);
            s.march=connected?focus:frontier(center,focus);
            s.marchTime=now;if(now-s.marchProgress>=frames(8))s.marchProgress=now;
        }
    }
    const Pos core=s.factorySN>=0?s.leash:focus;
    if((s.screenSince>=0 || s.screenDone) && dist(core,s.screenCore)>3){
        s.screenSince=-1;s.screenDone=false;s.screenCore=core;
    }
    bool defenders=false;
    for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP && dist(pos(e),core)<32)defenders=true;
    // The opposite spawn is useful before the factory is visible: begin the
    // ranged screen on first contact rather than marching through its guards.
    if(s.phase==Advance && !s.committed && !s.screenDone && defenders && dist(center,core)<39){
        if(s.screenSince>=0 && dist(core,s.screenCore)>3)s.screenSince=-1;
        if(s.screenSince<0){
            const auto& available=field(center);double best=1e30;bool found=false;
            for(int x=0;x<width;++x)for(int y=0;y<height;++y){
                Pos q(x+.5,y+.5);double radius=dist(q,core);
                if(radius<27.5 || radius>29 || available[x*height+y]<0 || danger(q,1)>0)continue;
                double score=dist(q,center)+dist(q,base)*.1;
                if(score<best){best=score;s.screen=q;found=true;}
            }
            if(found){s.screenSince=now;s.screenFire=now;s.screenCore=core;}
        }
        int firing=0;
        for(auto a:army)if(s.dispatched.count(a->SN) && ranged(a->Sort) &&
            a->NowState==HUMAN_STATE_ATTACKING){
            const auto* e=combat::enemy(*this,a->WorkObjectSN);
            if(e && reachDist(pos(*a),pos(*e))<=combat::bowRange(*this,*a,pos(*a),pos(*e)))++firing;
        }
        if(firing>=std::min(3,std::max(1,mainNear/4)))s.screenFire=now;
    }
    // Losing sight of the defenders must not freeze the opposite-spawn advance.
    if(s.screenSince>=0 && now-s.screenFire>frames(18)){s.screenDone=true;s.screenSince=-1;}
    if(s.screenSince>=0 && !defenders && s.factorySN>=0){s.screenDone=true;s.screenSince=-1;}
    const bool screen=s.screenSince>=0 && !s.screenDone && !homeDanger;
    const bool clear=guardsClear();
    // Three towers permit a healthy direct rush; four use the route exposure
    // or verified shield budget below. Five still require a confirmed removal.
    const bool finishAllowed=remainingEnemyTowers()<=4;
    s.guardScreenActive=s.phase!=Defend && (s.factorySN>=0 || screen) && !s.committed && !clear;
    if(clear){s.guardsScouted=true;if(s.clearSince<0)s.clearSince=now;}else s.clearSince=-1;

    // Build a reachable final corridor, then divert only towers which can cover it.
    std::map<int,Pos> decoys;
    bool locksReady=true,reservesReady=true,canReach=false;
    bool nearEntry=false,commitKnown=false,directReady=false,forceReady=false;
    std::vector<const tagBuilding*> relevant;
    Pos priestStage=pos(*hero);bool stageReady=false,stageBlocked=false;
    if(s.phase!=Defend && (clear || s.committed)){
        std::vector<Pos> corridor;canReach=combat::finalCorridor(*this,corridor);
        std::vector<Pos> plain;
        const bool plainReach=combat::finalCorridor(*this,plain,false);
        forceReady=clear && plainReach && combat::forceConversionReady(*this);
        directReady=forceReady || (finishAllowed && clear && plainReach && combat::directConversionSafe(*this,plain));
        nearEntry=canReach && plainReach && combat::routeLength(corridor)<=4 && combat::routeLength(plain)<=4;
        commitKnown=nearEntry;
        // All potential tower origins covering the short native final approach must
        // already be explored before an irreversible Action is allowed.
        if(commitKnown)for(Pos q:plain){int cx=int(q.x),cy=int(q.y),radius=int(DIS_ARROWTOWER);
            for(int dx=-radius;dx<=radius;++dx)for(int dy=-radius;dy<=radius;++dy){
                int x=cx+dx,y=cy+dy;if(dx*dx+dy*dy>radius*radius || !inside(x,y))continue;
                if((*info.theMap)[x][y].type==MAPPATTERN_UNKNOWN)commitKnown=false;
            }
        }
        const auto& approach=nearEntry?plain:corridor;
        for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER){
            // Keep actual locks stationary, but establish new locks only for the
            // next short approach. Remote shields otherwise die before we arrive.
            const tagHuman* target=combat::towerTarget(*this,tower);
            if(target)decoys[target->SN]=pos(*target);
            bool covers=false;double traveled=0;
            for(int i=0;i<int(approach.size());++i){
                if(i>0)traveled+=dist(approach[i-1],approach[i]);
                if(!nearEntry && traveled>5)break;
                if(combat::towerSelects(tower,approach[i]))covers=true;
            }
            if(covers)relevant.push_back(&tower);
        }
        const bool sufficient=nearEntry && commitKnown && combat::shieldCapacity(*this,relevant,plain);
        for(auto tower:relevant){
            const tagHuman* target=combat::towerTarget(*this,*tower);
            if(target)decoys[target->SN]=pos(*target);
            if(!combat::stationaryTowerShield(*this,*tower,left<=240?12:30))locksReady=false;
        }
        for(auto tower:relevant){
            const tagHuman* target=combat::towerTarget(*this,*tower);
            const int backups=directReady?0:(left<=240?(sufficient?0:1):2);
            const int primary=!target || target->Blood<(left<=240?12:30)?1:0;
            for(int slot=0;slot<backups+primary;++slot){
            const tagArmy* chosen=nullptr;double scoreBest=1e30;
            for(auto a:army)if(a->Sort!=AT_STONE_THROWER && a->Blood>=25 && s.dispatched.count(a->SN) &&
                !decoys.count(a->SN) && dist(pos(*a),pos(*tower))<23){
                const double lifetime=a->Blood/combat::towerDps(*this,*a);
                double score=dist(pos(*a),pos(*tower))*1.2-lifetime*.7;
                if(score<scoreBest){scoreBest=score;chosen=a;}
            }
            if(!chosen){reservesReady=false;continue;}
            Pos stand=pos(*chosen);scoreBest=1e30;
            const auto& available=field(pos(*chosen));
            const bool newPrimary=primary>0 && slot==0;
            for(int dx=-6;dx<=6;++dx)for(int dy=-6;dy<=6;++dy){
                int x=tower->BlockDR+dx,y=tower->BlockUR+dy;
                int radius2=dx*dx+dy*dy;
                if(!inside(x,y) || !walkable[x*height+y] || available[x*height+y]<0 ||
                    radius2<4 || radius2>(newPrimary?36:25))continue;
                Pos q(x+.5,y+.5);if(!combat::towerSelects(*tower,q))continue;
                double score=available[x*height+y]*.2+dist(q,pos(*hero))*.7+
                    std::abs(std::sqrt(double(radius2))-(newPrimary?5.5:5))*3;
                for(const auto& other:info.enemy_buildings)if(other.Blood>0 && other.Type==BUILDING_ARROWTOWER &&
                    other.SN!=tower->SN && combat::towerSelects(other,q))score+=40;
                for(const auto& d:decoys)if(dist(q,d.second)<1)score+=5;
                if(score<scoreBest){scoreBest=score;stand=q;}
            }
            decoys[chosen->SN]=stand;
            if(scoreBest==1e30)reservesReady=false;
            int dx=tower->BlockDR-chosen->BlockDR,dy=tower->BlockUR-chosen->BlockUR;
            if(dx*dx+dy*dy>25)reservesReady=false;
            }
        }
        if(left<=240)reservesReady=sufficient;
        // Approach before the final order while every tower able to select the priest
        // still has a healthy, stationary target. Stop outside touching distance.
        if(canReach && clear && !s.committed)for(int i=1;i<int(corridor.size());++i){
            Pos q=corridor[i];if(reachDist(q,s.factory)<sizeOf(BUILDING_SIEGE)*.5+2)break;
            if(int(q.x)==int(pos(*hero).x) && int(q.y)==int(pos(*hero).y))continue;
            bool safe=true;
            for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER && combat::towerSelects(tower,q)){
                if(!combat::stationaryTowerShield(*this,tower))safe=false;
            }
            if(!safe)break;
            auto bodyAt=[&](int x,int y){
                for(const auto& h:humans)if(h.first!=hero->SN && int(pos(*h.second).x)==x && int(pos(*h.second).y)==y)return true;
                for(const auto& h:info.enemy_armies)if(h.Blood>0 && int(pos(h).x)==x && int(pos(h).y)==y)return true;
                for(const auto& h:info.enemy_farmers)if(h.Blood>0 && int(pos(h).x)==x && int(pos(h).y)==y)return true;
                return false;
            };
            int x=int(pos(*hero).x),y=int(pos(*hero).y),nx=int(q.x),ny=int(q.y);
            if(!bodyAt(nx,ny) && !(nx!=x && ny!=y && (bodyAt(nx,y) || bodyAt(x,ny)))){
                priestStage=q;stageReady=dist(q,pos(*hero))>.4;
            }
            break;
        }
        stageBlocked=canReach && clear && !s.committed && !stageReady;
    }

    // A shield only has to stand under fire when that exposure can finish the
    // objective now. Otherwise conserve the army and use safe firing positions.
    const bool finishingNow=s.phase!=Defend && clear && finishAllowed && (forceReady || now-s.clearSince>=frames(.5)) &&
        (directReady || (canReach && nearEntry && commitKnown && locksReady && reservesReady));
    const bool towerEndgame=s.phase!=Defend && clear && s.factorySN>=0 && !s.committed && !finishingNow;
    const bool protectTowerShots=s.phase!=Defend && s.factorySN>=0 && (clear || s.committed);
    combat::TowerRegroupPlan towerWave;
    s.towerReady=0;s.towerFront=0;s.towerSafe=0;
    if(towerEndgame){
        towerWave=combat::towerRegroup(*this,center,7,2);
        s.towerReady=towerWave.ready;s.towerFront=towerWave.front;
        decoys.clear();
    }

    s.finish=int(clear)+int(canReach)*2+int(locksReady)*4+int(reservesReady)*8+int(relevant.size())*16;
    // Outer towers must not recruit the whole army before the objective is found.
    std::set<int> outerTowerShooters;
    if(s.phase!=Defend && s.factorySN<0 && !homeDanger)for(int slot=0;slot<3;++slot){
        const tagArmy* chosen=nullptr;double best=1e30;
        for(auto a:army)if(ranged(a->Sort) && a->Blood>=25 && s.dispatched.count(a->SN) &&
            !outerTowerShooters.count(a->SN))for(const auto& tower:info.enemy_buildings)
            if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER && dist(pos(tower),center)<21){
                const double reach=combat::bowRange(*this,*a,pos(*a),pos(tower));
                const double distance=reachDist(pos(*a),pos(tower));
                if(distance>reach+3)continue;
                double score=std::max(0.0,distance-reach)*3+dist(pos(*a),pos(tower))*.1;
                if(score<best){best=score;chosen=a;}
            }
        if(!chosen)break;
        outerTowerShooters.insert(chosen->SN);
    }
    for(auto a:army){
        Pos p=pos(*a);
        const bool rangedTowerShot=ranged(a->Sort) || a->Sort==AT_STONE_THROWER;
        if((towerEndgame || (protectTowerShots && rangedTowerShot)) && s.dispatched.count(a->SN) &&
            !(homeDanger && dist(p,base)<34)){
            const Pos waiting=towerWave.rally.x>=0?towerWave.rally:p;
            std::vector<const tagBuilding*> localTowers;
            for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Percent>=100 &&
                tower.Type==BUILDING_ARROWTOWER && dist(p,pos(tower))<=28)localTowers.push_back(&tower);
            std::sort(localTowers.begin(),localTowers.end(),[&](const tagBuilding* lhs,const tagBuilding* rhs){
                return dist(p,pos(*lhs))-(lhs->SN==a->WorkObjectSN?100:0)<
                    dist(p,pos(*rhs))-(rhs->SN==a->WorkObjectSN?100:0);
            });
            combat::TowerFirePlan planned,fallback;double scoreBest=1e30;
            for(size_t i=0;i<std::min(size_t(3),localTowers.size());++i){
                auto candidate=combat::planTowerFire(*this,*a,*localTowers[i],waiting);
                if(!fallback.valid && candidate.valid)fallback=candidate;
                if(!candidate.valid || !candidate.firingPosition)continue;
                double score=candidate.steps+(candidate.attack?-1000:0);
                if(candidate.target==a->WorkObjectSN)score-=2;
                if(score<scoreBest){scoreBest=score;planned=candidate;}
                if(candidate.attack && candidate.target==a->WorkObjectSN)break;
            }
            if(!planned.valid)planned=fallback;
            const bool gathering=towerWave.active && !towerWave.released;
            if(planned.valid && (rangedTowerShot || planned.firingPosition || gathering ||
                (planned.escaping && a->Blood*2<a->MaxBlood))){
                if(planned.attack)++s.towerSafe;
                combat::applyTowerFire(*this,*a,planned,94);continue;
            }
            if(gathering){
                // Far reinforcements approach the front rally. Near any tower,
                // only the checked path above is allowed to enter a new cell.
                if(localTowers.empty() && towerWave.rally.x>=0 && dist(p,waiting)>5)
                    move(a->SN,waypoint(p,waiting,false,5),94,.8);
                else combat::hold(*this,*a,94);
                continue;
            }
            // A released wave or priest commitment must not turn a ranged unit's
            // missing safe shot into an unchecked charge through tower coverage.
            if(rangedTowerShot && !localTowers.empty()){
                combat::hold(*this,*a,94);continue;
            }
        }

        auto d=decoys.find(a->SN);
        if(d!=decoys.end()){
            bool holding=false;for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER){
                const tagHuman* target=combat::towerTarget(*this,tower);
                if(target && target->SN==a->SN)holding=true;
            }
            bool stationed=holding || dist(p,d->second)<=.6;
            const Unit& u=s.units[a->SN];
            // A crowded landing may be shifted by move(); accept that completed
            // local landing only while it still screens the same tower.
            if(!stationed && u.last.kind==Go && u.last.priority==93 &&
                dist(p,u.last.point)<=.6 && dist(p,d->second)<=2.9){
                for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER &&
                    combat::towerSelects(tower,p) && combat::towerSelects(tower,d->second))stationed=true;
            }
            // An established stationary backup volley remains useful when the
            // preferred landing shifts slightly with the advancing priest.
            if(!stationed && u.last.kind==Use && u.last.priority==93 && u.last.target==a->WorkObjectSN)
                for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.SN==a->WorkObjectSN &&
                    tower.Type==BUILDING_ARROWTOWER && combat::towerSelects(tower,p) &&
                    combat::towerSelects(tower,d->second))stationed=true;
            const tagBuilding* shotTarget=nullptr;double shotScore=1e30;
            const bool shot=ranged(a->Sort) || a->Sort==AT_STONE_THROWER;
            // Entering the assigned tower's circle is sufficient when this
            // ranged screen can already fire safely; do not walk deeper just
            // to match a preferred landing which changes with the priest.
            if(!stationed && shot)for(const auto& tower:info.enemy_buildings)
                if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER &&
                    combat::towerSelects(tower,p) && combat::towerSelects(tower,d->second)){
                    if(a->Sort==AT_STONE_THROWER && (!combat::siegeShot(*this,pos(tower)) ||
                        reachDist(p,pos(tower))<double(DIS_MIN_STONE_THROWER)))continue;
                    if(reachDist(p,pos(tower))<=combat::bowRange(*this,*a,p,pos(tower))+.001){stationed=true;break;}
                }
            if(stationed && (shot || combat::melee(a->Sort)))
                for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER){
                    if(a->Sort==AT_STONE_THROWER && (!combat::siegeShot(*this,pos(tower)) ||
                        reachDist(p,pos(tower))<double(DIS_MIN_STONE_THROWER)))continue;
                    const double reach=shot?combat::bowRange(*this,*a,p,pos(tower)):
                        double(DISTANCE_ATTACK_CLOSE/BLOCKSIDELENGTH)+sizeOf(tower.Type)*.5;
                    if(reachDist(p,pos(tower))>reach+.001)continue;
                    double score=dist(p,pos(tower));
                    if(tower.SN==a->WorkObjectSN)score-=1000;
                    else if(tower.Project==a->SN)score-=100;
                    if(score<shotScore){shotScore=score;shotTarget=&tower;}
                }
            if(shotTarget){
                // Building attacks are reported as WORKING by Core::infoShare.
                // Preserve the relation so screening troops fire without moving.
                if(a->WorkObjectSN!=shotTarget->SN || a->NowState==HUMAN_STATE_IDLE)
                    action(a->SN,shotTarget->SN,93);
            }else if(stationed)combat::hold(*this,*a,93);
            else move(a->SN,waypoint(p,d->second),93,.5);
            continue;
        }
        if(recovering && a->SN==recovering->SN){
            Pos hp=pos(*hero),place=p;double best=1e30;
            for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)if(dx || dy){
                Pos q=clamp(Pos(hp.x+dx*.65,hp.y+dy*.65));
                if(!walkable[int(q.x)*height+int(q.y)] || danger(q,3)>0 || walkDistance(p,q)==INT_MAX)continue;
                double score=walkDistance(p,q)+dist(q,defenseRear)*.1;
                if(score<best){best=score;place=q;}
            }
            if(reachDist(p,hp)<=.85)combat::hold(*this,*a,62);
            else if(best<1e30)move(a->SN,dist(p,place)<2?place:waypoint(p,place),62,.15);
            continue;
        }
        const bool localDefense=homeDanger && dist(p,base)<34;
        const bool defending=s.phase==Defend || localDefense;
        const bool waiting=s.phase!=Defend && !s.dispatched.count(a->SN);
        if(waiting && !localDefense){move(a->SN,waypoint(p,s.assembly),40,1.5);continue;}
        // Rear reinforcements must not pull a surviving front-line archer away
        // from a tower it can already hit. A distant stale relation is not enough.
        const tagBuilding* nearbyTower=nullptr;double towerScore=1e30;
        if(!defending && (s.factorySN>=0 || outerTowerShooters.count(a->SN)))
            for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER){
                const bool shot=ranged(a->Sort) || a->Sort==AT_STONE_THROWER;
                const double reach=shot?combat::bowRange(*this,*a,p,pos(tower)):
                    double(DISTANCE_ATTACK_CLOSE/BLOCKSIDELENGTH)+sizeOf(tower.Type)*.5;
                if(reachDist(p,pos(tower))>reach+.001 ||
                    (a->Sort==AT_STONE_THROWER && (reachDist(p,pos(tower))<double(DIS_MIN_STONE_THROWER) ||
                     !combat::siegeShot(*this,pos(tower)))))continue;
                double score=dist(p,pos(tower))-(a->WorkObjectSN==tower.SN?1000:0);
                if(score<towerScore){towerScore=score;nearbyTower=&tower;}
            }
        // Released reinforcements follow the fighting force through known terrain.
        const bool joinFront=s.phase!=Defend && !localDefense && dist(center,base)>20 &&
            dist(p,center)>13 && dist(p,focus)>dist(center,focus)+3;
        if(joinFront && !nearbyTower && danger(p,1)==0){
            Pos destination=walkDistance(p,center)<INT_MAX?center:frontier(p,center);
            move(a->SN,waypoint(p,destination),80,.6);continue;
        }
        const tagArmy* target=nullptr;const tagArmy* contact=nullptr;double best=1e30,nearest=1e30;
        const bool outside=screen && ranged(a->Sort) && !localDefense;
        for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP){
            double distance=reachDist(p,pos(e));
            if(localDefense && dist(pos(e),base)>29)continue;
            if(combat::melee(e.Sort) && distance<nearest){nearest=distance;contact=&e;}
            double reach=combat::bowRange(*this,*a,p,pos(e));
            const bool shooting=a->WorkObjectSN==e.SN && a->NowState==HUMAN_STATE_ATTACKING && distance<=reach+.02;
            const bool saving=defending && rescueTargets.count(e.SN)>0;
            const bool mobilizing=defending && ranged(a->Sort) &&
                (saving || dist(pos(e),base)<29 || dist(pos(e),rally)<24);
            if(defending){
                if(ranged(a->Sort) && distance>reach-.1 && !mobilizing && !shooting)continue;
                bool towerLocked=towerTargets.count(e.SN)>0;
                auto attacked=buildings.find(e.WorkObjectSN);
                if(attacked!=buildings.end() && attacked->second->Type==BUILDING_ARROWTOWER)towerLocked=true;
                bool urgent=distance<=rangeOf(e.Sort)+2 || reachDist(pos(e),pos(*hero))<=rangeOf(e.Sort)+4;
                auto victim=humans.find(e.WorkObjectSN);
                if(victim!=humans.end() && reachDist(pos(e),pos(*victim->second))<=rangeOf(e.Sort)+1)urgent=true;
                if(ranged(a->Sort) && defenseTower && !towerLocked && !urgent && !mobilizing && !shooting)continue;
                if(combat::melee(a->Sort) && distance>4 &&
                    !(e.Sort==AT_STONE_THROWER && dist(pos(e),rally)<16) && !urgent && !saving)continue;
            }
            if(outside && distance>reach-.2 && !shooting)continue;
            if(a->Sort==AT_STONE_THROWER && !combat::siegeShot(*this,pos(e)))continue;
            if(!saving && !mobilizing && !localDefense && distance>14 && dist(p,center)>16)continue;
            if(!saving && !mobilizing && !localDefense && distance>14 && (s.phase==Defend || dist(pos(e),center)>17))continue;
            double score=distance;
            if(e.WorkObjectSN==hero->SN)score-=10;
            if(e.Sort==AT_STONE_THROWER)score-=combat::melee(a->Sort)?10:4;
            if(e.Sort==AT_CAVALRY || e.Sort==AT_CHARIOT_ARCHER)score-=3;
            if(defending && rescueTargets.count(e.SN))score-=15;
            if(e.SN==a->WorkObjectSN && distance<=reach)score-=5;
            if(score<best){best=score;target=&e;}
        }
        if(ranged(a->Sort) && contact && nearest<1.8){
            Pos away(p.x+(p.x-pos(*contact).x)*3,p.y+(p.y-pos(*contact).y)*3);
            Pos escape=safePoint(p,away);Pos step=waypoint(p,escape,false,3);
            if(reachDist(step,pos(*contact))>nearest+.5 && (!outside || dist(step,core)>=27))
                move(a->SN,step,98,.3);
            else if(target && a->WorkObjectSN!=target->SN)action(a->SN,target->SN,82);
            continue;
        }
        if(target){
            if(a->WorkObjectSN==target->SN && a->NowState==HUMAN_STATE_ATTACKING &&
                reachDist(p,pos(*target))<=combat::bowRange(*this,*a,p,pos(*target))+.02)continue;
            if(ranged(a->Sort) && reachDist(p,pos(*target))>combat::bowRange(*this,*a,p,pos(*target))-.1){
                const auto& accessible=field(p);Pos firing=p;double best=1e30;
                int reach=int(std::ceil(combat::bowRange(*this,*a,p,pos(*target))));
                for(int x=int(pos(*target).x)-reach;x<=int(pos(*target).x)+reach;++x)
                    for(int y=int(pos(*target).y)-reach;y<=int(pos(*target).y)+reach;++y){
                        if(!inside(x,y) || !walkable[x*height+y] || accessible[x*height+y]<0)continue;
                        Pos q(x+.5,y+.5);double shotRange=combat::bowRange(*this,*a,q,pos(*target));
                        if((defending && dist(q,base)>29) || (outside && dist(q,core)<27) ||
                            reachDist(q,pos(*target))>shotRange-.4 || reachDist(q,pos(*target))<shotRange-1.5)continue;
                        bool body=false;for(const auto& h:humans)if(h.first!=a->SN && int(pos(*h.second).x)==x && int(pos(*h.second).y)==y)body=true;
                        if(body)continue;
                        double score=accessible[x*height+y]+danger(q,1)*(defending?2:12)+
                            dist(q,defending?base:center)*.1+(a->SN+x+y)%7*.03;
                        for(const auto& o:orders)if(o.second.kind==Go && dist(o.second.point,q)<1.2)score+=8;
                        if(score<best){best=score;firing=q;}
                    }
                if(a->WorkObjectSN>=0)combat::hold(*this,*a,88);
                else if(best<1e30)move(a->SN,waypoint(p,firing),82,.5);
                continue;
            }
            if(a->WorkObjectSN!=target->SN || a->NowState==HUMAN_STATE_IDLE)action(a->SN,target->SN,82);
            continue;
        }
        if(outside){
            Pos firing=p;double scoreBest=1e30;
            for(int dx=-5;dx<=5;++dx)for(int dy=-5;dy<=5;++dy){
                int x=int(p.x)+dx,y=int(p.y)+dy;if(!inside(x,y) || !walkable[x*height+y])continue;
                Pos q(x+.5,y+.5);if(dist(q,core)<27 || danger(q,1)>0)continue;
                for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP &&
                    reachDist(q,pos(e))<combat::bowRange(*this,*a,q,pos(e))-.4){
                    double score=dist(p,q)+danger(q,1)*5;
                    if(score<scoreBest){scoreBest=score;firing=q;}
                }
            }
            if(scoreBest<1e30){Pos step=waypoint(p,firing,false,3);if(dist(step,core)>=27)move(a->SN,step,84,.5);}
            else if(dist(p,core)>=27 && dist(p,core)<33)combat::hold(*this,*a);
            else {Pos step=waypoint(p,s.screen);if(dist(step,core)>=27 || dist(step,core)>dist(p,core))move(a->SN,step,84,.6);}
            continue;
        }
        if(defending){
            // A defensive Action outside actual range would chase past the tower and take its retaliation lock.
            if(ranged(a->Sort) && a->WorkObjectSN>=0)combat::hold(*this,*a,88);
            double spread=(a->SN%5-2)*.8,depth=combat::melee(a->Sort)?2.5:-(1.8+(a->SN%2)*.8);
            Pos wanted(rally.x+defenseFacing.x*depth-defenseFacing.y*spread,
                       rally.y+defenseFacing.y*depth+defenseFacing.x*spread);
            move(a->SN,waypoint(p,defensePoint(wanted)),55,.8);continue;
        }
        const tagBuilding* towerTarget=nullptr;best=1e30;
        if(s.phase!=Defend && !localDefense && (s.factorySN>=0 || outerTowerShooters.count(a->SN)))for(const auto& tower:info.enemy_buildings)
            if(tower.Blood>0 && tower.Percent>=100 && tower.Type==BUILDING_ARROWTOWER && (dist(pos(tower),center)<21 || &tower==nearbyTower) &&
                (a->Sort!=AT_STONE_THROWER || combat::siegeShot(*this,pos(tower)))){
                double score=dist(p,pos(tower))-(&tower==nearbyTower?1000:0);if(score<best){best=score;towerTarget=&tower;}
            }
        if(towerTarget){
            const bool shot=ranged(a->Sort) || a->Sort==AT_STONE_THROWER;
            const double reach=shot?combat::bowRange(*this,*a,p,pos(*towerTarget)):
                double(DISTANCE_ATTACK_CLOSE/BLOCKSIDELENGTH)+sizeOf(towerTarget->Type)*.5;
            // A valid in-range building relation must not be restarted merely
            // because it is reported as WORKING or sits at the range boundary.
            if(a->WorkObjectSN==towerTarget->SN && a->NowState!=HUMAN_STATE_IDLE &&
                reachDist(p,pos(*towerTarget))<=reach+.001 &&
                (a->Sort!=AT_STONE_THROWER || reachDist(p,pos(*towerTarget))>=double(DIS_MIN_STONE_THROWER)))continue;
            if(reachDist(p,pos(*towerTarget))>(shot?reach-.1:reach+.01) ||
                (a->Sort==AT_STONE_THROWER && reachDist(p,pos(*towerTarget))<double(DIS_MIN_STONE_THROWER))){
                const auto& accessible=field(p);Pos approach=p;double best=1e30;
                const int radius=int(std::ceil(reach+1));
                for(int x=towerTarget->BlockDR-radius;x<=towerTarget->BlockDR+radius+sizeOf(towerTarget->Type);++x)
                    for(int y=towerTarget->BlockUR-radius;y<=towerTarget->BlockUR+radius+sizeOf(towerTarget->Type);++y){
                        if(!inside(x,y) || accessible[x*height+y]<0 || !walkable[x*height+y])continue;
                        Pos q(x+.5,y+.5);double d=reachDist(q,pos(*towerTarget));
                        if(d>(shot?reach-.4:reach+.01) || d<(shot?reach-1.2:sizeOf(towerTarget->Type)*.5+.05))continue;
                        double score=accessible[x*height+y]+danger(q,0)*5;
                        if(score<best){best=score;approach=q;}
                    }
                if(best<1e30)move(a->SN,waypoint(p,approach),69,.4);
            }else action(a->SN,towerTarget->SN,68);
            continue;
        }
        Pos goal=s.phase==Defend?(localDefense?rally:s.assembly):s.march;
        if(joinFront)goal=center;
        if(screen && combat::melee(a->Sort)){
            double radius=std::max(1.0,dist(s.screen,core));
            goal=clamp(Pos(core.x+(s.screen.x-core.x)*19/radius,core.y+(s.screen.y-core.y)*19/radius));
            if(walkDistance(p,goal)==INT_MAX)goal=frontier(p,goal);
        }
        move(a->SN,waypoint(p,goal),55,1);
    }

    // Soldier orders above may withdraw a formerly locked bow. Do not let the
    // priest step into the circle on the strength of that now-outgoing shield.
    if(towerEndgame && stageReady){
        for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER &&
            combat::towerSelects(tower,priestStage) && !combat::stationaryTowerShield(*this,tower)){
            stageReady=false;stageBlocked=true;break;
        }
    }
    // Once committed, no casualty, tower lock loss, heal or conversion can replace this order.
    if(s.committed && s.factorySN>=0){
        s.phase=Converting;
        if(hero->WorkObjectSN!=s.factorySN)action(hero->SN,s.factorySN,120);
        return;
    }
    if(s.phase!=Defend && clear && finishAllowed && (forceReady || now-s.clearSince>=frames(.5)) &&
        (directReady || (canReach && nearEntry && commitKnown && locksReady && reservesReady))){
        s.committed=true;s.phase=Converting;action(hero->SN,s.factorySN,120);return;
    }
    if(clear && !s.committed){
        const Unit& u=s.units[hero->SN];const Pos current=pos(*hero),destination=u.last.point;
        const double remaining=dist(current,destination);
        const Pos nativeDestination(hero->DR0/BLOCKSIDELENGTH,hero->UR0/BLOCKSIDELENGTH);
        if(u.last.kind==Go && u.last.priority==102 && hero->NowState==HUMAN_STATE_WALKING &&
            remaining>.2 && remaining<=2 && now-u.moved<frames(2) && dist(nativeDestination,destination)<.2){
            auto open=[&](int x,int y){
                if(!inside(x,y) || !walkable[x*height+y])return false;
                for(const auto& h:humans)if(h.first!=hero->SN && int(pos(*h.second).x)==x && int(pos(*h.second).y)==y)return false;
                for(const auto& h:info.enemy_armies)if(h.Blood>0 && int(pos(h).x)==x && int(pos(h).y)==y)return false;
                for(const auto& h:info.enemy_farmers)if(h.Blood>0 && int(pos(h).x)==x && int(pos(h).y)==y)return false;
                for(const auto& r:info.resources)if(r.Cnt>0 &&
                    (r.Type==RESOURCE_GAZELLE || r.Type==RESOURCE_ELEPHANT || r.Type==RESOURCE_LION) &&
                    int(pos(r).x)==x && int(pos(r).y)==y)return false;
                return true;
            };
            bool safe=true;int previousX=int(current.x),previousY=int(current.y);
            const int samples=std::max(1,int(std::ceil(remaining/.25)));
            for(int i=0;i<=samples && safe;++i){
                const double part=double(i)/samples;
                Pos q(current.x+(destination.x-current.x)*part,current.y+(destination.y-current.y)*part);
                int x=int(q.x),y=int(q.y);
                if(!open(x,y) || std::abs((*info.theMap)[x][y].height-(*info.theMap)[previousX][previousY].height)>1 ||
                    (x!=previousX && y!=previousY && (!open(x,previousY) || !open(previousX,y)))){safe=false;break;}
                for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER && combat::towerSelects(tower,q)){
                if(!combat::stationaryTowerShield(*this,tower)){safe=false;break;}
                }
                previousX=x;previousY=y;
            }
            // Crossing a cell edge is earlier than arriving at its center. Finish
            // this verified local step before a changing corridor picks another one.
            if(safe)return;
        }
    }
    if(clear && stageReady){move(hero->SN,priestStage,102,.2);return;}
    if(clear && canReach && stageBlocked){
        bool safe=true;
        for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER &&
            combat::towerSelects(tower,pos(*hero))){
                if(!combat::stationaryTowerShield(*this,tower)){safe=false;break;}
        }
        // Wait at the last safe landing while the next tower acquires a shield.
        // The general rear position can move as the army shifts and undo this approach.
        if(safe){combat::hold(*this,*hero,103);return;}
    }
    Pos hp=pos(*hero),support=s.phase==Defend?rally:center;
    Pos facing(core.x-support.x,core.y-support.y);double radius=std::max(1.0,dist(core,support));
    facing=Pos(facing.x/radius,facing.y/radius);
    Pos rear=s.phase==Defend?defenseRear:
        Pos(support.x-facing.x*8,support.y-facing.y*8);
    if(s.guardScreenActive && dist(rear,s.leash)<28){
        double r=std::max(.1,dist(rear,s.leash));rear=Pos(s.leash.x+(rear.x-s.leash.x)*28/r,s.leash.y+(rear.y-s.leash.y)*28/r);
    }
    auto heroMove=[&](Pos wanted,int priority){
        Pos goal=safePoint(hp,wanted,true),step=waypoint(hp,goal,true,5);
        const bool endangered=danger(hp,4)>0 || hero->Blood<45;
        const bool safetyBlocked=dist(step,hp)<.4;
        Pos routed=hp;
        // Once the safe field is cut off, a complete escape route takes precedence
        // over a locally improving step which may lead into another forest pocket.
        bool routedEscape=endangered && safetyBlocked && combat::escapeStep(*this,hp,wanted,routed);
        if(routedEscape)step=routed;
        else if(safetyBlocked){
            Pos escape=safePoint(hp,wanted,true),candidate=waypoint(hp,escape,false,1);
            const bool outside=!s.guardScreenActive || dist(candidate,s.leash)>=27;
            const bool recovering=s.guardScreenActive && dist(hp,s.leash)<27 &&
                dist(candidate,s.leash)>dist(hp,s.leash)+.4 && danger(candidate,4)<=danger(hp,4)+.05;
            const bool safeProgress=outside && danger(candidate,4)==0 && dist(candidate,escape)<dist(hp,escape)-.1;
            if((outside && danger(candidate,4)+.2<danger(hp,4)) || safeProgress || recovering)step=candidate;
        }
        auto bodyAt=[&](int x,int y){
            for(const auto& h:humans)if(h.first!=hero->SN && int(pos(*h.second).x)==x && int(pos(*h.second).y)==y)return h.first;
            return -1;
        };
        auto open=[&](int x,int y){return inside(x,y) && walkable[x*height+y] && bodyAt(x,y)<0;};
        if(!routedEscape && ((endangered && (dist(step,hp)<.5 || bodyAt(int(step.x),int(step.y))>=0 ||
            danger(step,4)>=danger(hp,4))) || bodyAt(int(step.x),int(step.y))>=0)){
            int x=int(hp.x),y=int(hp.y);Pos escape=hp;double best=1e30;
            for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int nx=x+dx,ny=y+dy;
                if((!dx && !dy) || !open(nx,ny) || (dx && dy && (!open(nx,y) || !open(x,ny))))continue;
                Pos p(nx+.5,ny+.5);if(s.guardScreenActive && dist(p,s.leash)<27 && dist(p,s.leash)<=dist(hp,s.leash))continue;
                double score=danger(p,4)*100+dist(p,wanted);
                if(score<best){best=score;escape=p;}
            }
            if(best<1e30 && (danger(escape,4)<danger(hp,4) || danger(escape,4)==0))step=escape;
            else if(endangered){
                if(!safetyBlocked && combat::escapeStep(*this,hp,wanted,routed))step=routed;
                else{
                    // If our own idle unit seals the escape cell, clear one cell first.
                    for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){int nx=x+dx,ny=y+dy;
                        if(std::abs(dx)+std::abs(dy)!=1 || !inside(nx,ny) || !walkable[nx*height+ny])continue;
                        int blocker=bodyAt(nx,ny);if(blocker<0 || pending(blocker) || now-s.units[blocker].progress<frames(2))continue;
                        const tagHuman* ally=humans.at(blocker);if(ally->NowState==HUMAN_STATE_ATTACKING && hero->Blood>=45)continue;
                        Pos next=pos(*ally);double scoreBest=1e30;
                        for(int a=-1;a<=1;++a)for(int b=-1;b<=1;++b){int xx=nx+a,yy=ny+b;
                            if((!a && !b) || !open(xx,yy) || (xx==x && yy==y) ||
                                (a && b && (!open(xx,ny) || !open(nx,yy))))continue;
                            Pos p(xx+.5,yy+.5);double score=danger(p,3)*10+dist(p,wanted);
                            if(score<scoreBest){scoreBest=score;next=p;}
                        }
                        if(scoreBest<1e30 && move(blocker,next,119,.2))return;
                    }
                }
            }
        }
        if(dist(step,hp)>.5)move(hero->SN,step,priority,.5);else combat::hold(*this,*hero,priority);
    };
    bool exposed=false;double dangerMargin=1e30;int directAttackers=0;
    for(const auto& e:info.enemy_armies)if(e.Blood>0 && e.Sort!=AT_SHIP){
        double margin=reachDist(hp,pos(e))-enemyRange(e.Sort);dangerMargin=std::min(dangerMargin,margin);
        if(e.WorkObjectSN==hero->SN && margin<=1)++directAttackers;
        if(margin<2 || (e.WorkObjectSN==hero->SN && margin<5))exposed=true;
    }
    bool towerExposure=false;
    for(const auto& tower:info.enemy_buildings)if(tower.Blood>0 && tower.Type==BUILDING_ARROWTOWER &&
        (tower.Project==hero->SN || (!combat::stationaryTowerShield(*this,tower) && reachDist(hp,pos(tower))<double(DIS_ARROWTOWER)+2)))towerExposure=true;
    if(towerExposure)exposed=true;
    if(s.guardScreenActive && dist(hp,s.leash)<27)exposed=true;
    if(s.phase!=Defend && !clear && (hp.x-support.x)*facing.x+(hp.y-support.y)*facing.y>-4)exposed=true;
    const tagArmy* converting=combat::enemy(*this,hero->WorkObjectSN);
    if(converting && !towerExposure && hero->ConvertCooldown==0 && (hero->NowState==HUMAN_STATE_ATTACKING || hero->NowState==HUMAN_STATE_WORKING) && hero->Blood>60 && directAttackers<=1 &&
        reachDist(hp,pos(*converting))<=DIS_PRIEST && (!s.guardScreenActive || dist(hp,core)>=27))return;
    // A pursuing enemy used to trigger retreat before a new conversion could
    // begin. During home defense, counter it from the current position first.
    int conversionCover=0;
    for(auto a:army)if(a->Blood>=25 && dist(pos(*a),hp)<=8 &&
        (ranged(a->Sort) || combat::melee(a->Sort)))++conversionCover;
    const bool homeCounter=(s.phase==Defend || homeDanger) && dist(hp,base)<32 &&
        !towerExposure && (!s.guardScreenActive || dist(hp,core)>=27) &&
        (directAttackers<=1 || (directAttackers==2 && conversionCover>=2));
    if(homeCounter && hero->Blood>20 && hero->ConvertCooldown==0){
        // Damage alone must not reset the native 2-6 second conversion timer.
        // Once it is underway, hold longer than the threshold used to start it.
        if(converting &&
            (hero->NowState==HUMAN_STATE_ATTACKING || hero->NowState==HUMAN_STATE_WORKING) &&
            reachDist(hp,pos(*converting))<=DIS_PRIEST)return;
        const Unit& history=s.units[hero->SN];
        const tagArmy* waiting=history.last.kind==Use?combat::enemy(*this,history.last.target):nullptr;
        if(waiting && now-history.sent<frames(2) && reachDist(hp,pos(*waiting))<=double(DIS_PRIEST)-.25)return;
    }
    if(homeCounter && hero->Blood>=30 && hero->ConvertCooldown==0){
        const tagArmy* target=nullptr;double best=1e30;
        for(const auto& e:info.enemy_armies)if(e.Blood>0 && (e.Blood>25 || e.WorkObjectSN==hero->SN) && e.Sort!=AT_SHIP &&
            dist(pos(e),base)<32 && reachDist(hp,pos(e))<=double(DIS_PRIEST)-.25){
            double score=reachDist(hp,pos(e))-e.Blood*.015+(towerTargets.count(e.SN)?8:0);
            if(e.WorkObjectSN==hero->SN)score-=20;
            if(e.Sort==AT_STONE_THROWER)score-=12;
            if(e.Sort==AT_CAVALRY || e.Sort==AT_CHARIOT || e.Sort==AT_HOPLITE)score-=5;
            if(score<best){best=score;target=&e;}
        }
        if(target){action(hero->SN,target->SN,110);return;}
    }
    if(exposed){heroMove(rear,118);return;}
    if(converting && reachDist(hp,pos(*converting))<=DIS_PRIEST &&
        (hero->NowState==HUMAN_STATE_ATTACKING || now-s.units[hero->SN].sent<frames(8)))return;
    if(hero->ConvertCooldown==0){
        const tagArmy* target=nullptr;double best=1e30;
        for(const auto& e:info.enemy_armies)if(e.Blood>25 && e.Sort!=AT_SHIP &&
            reachDist(hp,pos(e))<double(DIS_PRIEST)-.5 &&
            (!s.guardScreenActive || dist(hp,s.leash)>=27)){
            double score=reachDist(hp,pos(e))+(towerTargets.count(e.SN)?8:0)-e.Blood*.015;
            if(e.Sort==AT_STONE_THROWER)score-=12;
            if(e.Sort==AT_CAVALRY || e.Sort==AT_CHARIOT || e.Sort==AT_HOPLITE)score-=5;
            if(score<best){best=score;target=&e;}
        }
        if(target){action(hero->SN,target->SN,108);return;}
    }
    const tagHuman* wounded=nullptr;double most=0;
    for(const auto& h:humans)if(h.first!=hero->SN && h.second->Blood<h.second->MaxBlood &&
        reachDist(hp,pos(*h.second))<=.95 && dangerMargin>4){
        if(quietDefense && (!recovering || h.first!=recovering->SN))continue;
        double need=1-double(h.second->Blood)/std::max(1,h.second->MaxBlood);
        if(need>most){most=need;wounded=h.second;}
    }
    if(wounded){action(hero->SN,wounded->SN,75);return;}
    heroMove(rear,76);
}

} // namespace

void UsrAI::processData() {
    const tagInfo info = getInfo();
    static State state;
    if (info.GameFrame < state.frame) state = State();
    if (!info.theMap || info.theMap->empty() || (*info.theMap)[0].empty() || info.GameFrame == state.frame) return;
    state.frame = info.GameFrame;
    const auto started = std::chrono::steady_clock::now();
    Bot bot(info, state);
    if (!state.announced) {
        std::cout << "AI build=h-rewrite-20261008-v54 submission=explicit-batch" << std::endl;
        state.announced = true;
    }
    if (state.baseSN < 0 || !bot.hero) return;
    if (info.GameFrame - state.tactics >= frames(.25)) {
        bot.tactics(); state.tactics = info.GameFrame;
    }
    if (info.GameFrame - state.economy >= frames(1)) {
        bot.production(); bot.construction(); bot.economy(); state.economy = info.GameFrame;
    }
    // Use the four public helpers and their actual IDs. One chosen command per actor.
    for (const auto& entry : bot.orders) {
        const Order& command = entry.second; int id = -1;
        switch (command.kind) {
        case Go: id = HumanMove(command.sn, command.point.x * BLOCKSIDELENGTH, command.point.y * BLOCKSIDELENGTH); break;
        case Use: id = HumanAction(command.sn, command.target); break;
        case Build: id = HumanBuild(command.sn, command.option, int(command.point.x), int(command.point.y)); break;
        case Project: id = BuildingAction(command.sn, command.option); break;
        }
        if (id < 0) continue;
        Pending pending; pending.order = command; pending.frame = info.GameFrame;
        auto human = bot.humans.find(command.sn);
        if (human != bot.humans.end()) pending.origin = pos(*human->second);
        for (auto farmer : bot.workers) if (farmer->SN == command.sn) pending.cargo = farmer->Resource;
        state.pending[id] = pending;
        Unit& unit = state.units[command.sn];
        const bool restart = command.kind == Use && bot.resourceRole(command.target) >= 0 &&
            (unit.last.kind != Use || unit.last.target != command.target || unit.blocked == command.target);
        unit.last = command; unit.sent = info.GameFrame;
        if (restart) {
            unit.progress = info.GameFrame; unit.harvest = info.GameFrame;
            if (unit.blocked == command.target) unit.blocked = -1;
        }
        if (command.research) {
            Tech& tech = state.tech[command.option]; tech.building = command.sn; tech.start = info.GameFrame;
            tech.seen = false; tech.accepted = false;
        }
        ++state.issued;
    }
    // Direct server executors need this handoff too; AI::run's subsequent empty commit is safe.
    CommitInstruction();
    bool heroThreat = bot.homeDanger || bot.hero->Blood < 75;
    for (const auto& e : info.enemy_armies) if (e.Blood > 0 && e.Sort != AT_SHIP &&
        (e.WorkObjectSN == bot.hero->SN || reachDist(pos(e),pos(*bot.hero)) <= 12)) heroThreat = true;
    if (info.GameFrame - state.report < frames(heroThreat ? 5 : 30)) return;
    state.report = info.GameFrame;
    int jobs[3] = {0,0,0}, idle = 0, builders = 0, firing = 0, staged = 0, healthy = 0;
    for (auto f : bot.workers) {
        int role = bot.resourceRole(f->WorkObjectSN); if (role >= 0) ++jobs[role];
        if (f->NowState == HUMAN_STATE_IDLE) ++idle;
        auto b = bot.buildings.find(f->WorkObjectSN);
        if (b != bot.buildings.end() && b->second->Percent < 100) ++builders;
    }
    for (auto a : bot.army) {
        if (a->NowState == HUMAN_STATE_ATTACKING) ++firing;
        if (state.phase != Defend && !state.dispatched.count(a->SN)) ++staged;
        if (a->Blood*2 >= a->MaxBlood) ++healthy;
    }
    Pos center = bot.armyCenter(), hp = pos(*bot.hero);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << "AI t=" << bot.seconds << " phase=" << int(state.phase)
        << " workers=" << bot.workers.size() << " army=" << bot.army.size()
        << " res=" << info.Wood << '/' << info.Meat << '/' << info.Gold
        << " hero=" << bot.hero->Blood << " target=" << state.factorySN << " errors=" << state.errors
        << " jobs=" << jobs[0] << '/' << jobs[1] << '/' << jobs[2] << " idle=" << idle << " builders=" << builders
        << " firing=" << firing << " healthy=" << healthy << " staged=" << staged << " batch=" << state.batch.size()
        << " screen=" << (state.screenSince >= 0) << " armyPos=" << int(center.x) << ',' << int(center.y)
        << " goal=" << int(state.march.x) << ',' << int(state.march.y)
        << " age=" << info.civilizationStage << " farms=" << bot.count(BUILDING_FARM,true)
        << " ranges=" << bot.count(BUILDING_RANGE,true) << " stocks=" << bot.count(BUILDING_STOCK,true)
        << " armor=" << bot.level(BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER)
        << " woodTech=" << bot.level(BUILDING_MARKET_WOOD_UPGRADE)
        << " goldTech=" << bot.level(BUILDING_MARKET_GOLD_UPGRADE)
        << " composite=" << bot.level(BUILDING_RANGE_UPGRADE_COMPOSITE_BOW)
        << " heroPos=" << int(hp.x) << ',' << int(hp.y) << " heroWork=" << bot.hero->WorkObjectSN
        << " heroState=" << bot.hero->NowState << " cooldown=" << bot.hero->ConvertCooldown
        << " guardsClear=" << bot.guardsClear() << " committed=" << state.committed << " finish=" << state.finish
        << " stalledFarms=" << state.stalledFarms.size() << " pop=" << info.Human_Num << '/' << info.Human_MaxNum
        << " pending=" << state.pending.size() << " unknown=" << state.unknown << " issued=" << state.issued << " planMs=" << ms;
    std::cout<<" launch="<<state.launchAvailable<<'/'<<state.launchHealthy<<'/'<<state.launchStrong<<':'<<state.launchReason
        <<" fullSince="<<(state.fullSince<0?-1:state.fullSince*TimePerFrame/1000)<<" homeDanger="<<bot.homeDanger<<" clearVeto="<<state.clearVeto;
    std::cout<<" thirdRaid="<<state.thirdRaidSeen<<':'<<(state.thirdRaidClearSince<0?-1:(bot.now-state.thirdRaidClearSince)*TimePerFrame/1000);
    std::cout<<" towerWave="<<state.towerWaveReleased<<':'<<state.towerReady<<'/'<<state.towerFront
        <<" towerSafe="<<state.towerSafe<<" towerRally="<<int(state.towerRally.x)<<','<<int(state.towerRally.y);
    std::cout<<" enemyTowers="<<bot.remainingEnemyTowers()<<" towersRemoved="<<state.removedEnemyTowers.size();
    std::cout<<" scout="<<state.resourceScout<<" investment="<<state.buildWish<<':'<<state.buildWood<<':'<<state.buildSite<<':'<<state.buildWorker;
    int mines=0;for(const auto& r:info.resources)if(r.Type==RESOURCE_GOLD && r.Cnt>100 && dist(pos(r),bot.base)<32)++mines;
    std::cout<<" mines="<<mines;
    int foodJobs[2]={0,0};int woodStalled=0,hauling[3]={0,0,0},farmDropMax=0,worstFarmDrop=0;
    for(auto f:bot.workers){
        const Unit& u=state.units[f->SN];
        if(u.role==1 && u.job>=0 && !bot.constructing(*f) && (f->NowState!=HUMAN_STATE_IDLE || u.depot>=0)){
            if(u.source==2)++foodJobs[0];else if(u.source==4)++foodJobs[1];
        }
        if(u.source==0 && info.GameFrame-u.progress>=frames(8))++woodStalled;
        if(f->Resource>0 && u.depot>=0 && u.role>=0 && u.role<3)++hauling[u.role];

    }
    for(const auto& farm:info.buildings)if(farm.Blood>0 && farm.Type==BUILDING_FARM && farm.Percent>=100 && farm.Cnt>0){
        int best=INT_MAX;
        for(const auto& depot:info.buildings)if(depot.Blood>0 && depot.Percent>=100 &&
            (depot.Type==BUILDING_GRANARY || depot.Type==BUILDING_CENTER))best=std::min(best,bot.walkDistance(pos(farm),pos(depot)));
        if(best!=INT_MAX)farmDropMax=std::max(farmDropMax,best);
        int worst=farmDropDistance(bot,Pos(farm.BlockDR,farm.BlockUR),sizeOf(farm.Type));
        if(worst!=INT_MAX)worstFarmDrop=std::max(worstFarmDrop,worst);
    }
    const FoodSupply supply=foodSupply(bot);
    std::cout<<" woodStalled="<<woodStalled<<" hauling="<<hauling[0]<<'/'<<hauling[1]<<'/'<<hauling[2]
        <<" farmDropMax="<<farmDropMax<<" worstFarmDrop="<<worstFarmDrop
        <<" foodJobs="<<foodJobs[0]<<'/'<<foodJobs[1]<<" foodSupply="<<supply.readyFarms<<'/'<<supply.natural<<'/'<<supply.productive<<" granaries="<<bot.count(BUILDING_GRANARY,true);
    if(state.resourceScout>=0 && bot.humans.count(state.resourceScout)){
        const Pos p=pos(*bot.humans.at(state.resourceScout));
        std::cout<<" scoutPos="<<int(p.x)<<','<<int(p.y)<<" scoutGoal="<<int(state.scoutGoal.x)<<','<<int(state.scoutGoal.y);
    }
    if(info.Gold<=40 && bot.seconds>=600)for(const auto& b:info.buildings)if(b.Type==BUILDING_STOCK || b.Percent<100)
        std::cout<<" depotOrBuild="<<b.SN<<':'<<b.Type<<':'<<b.BlockDR<<','<<b.BlockUR<<':'<<b.Percent;
    for (const auto& b : info.buildings) if (b.Blood > 0 && (b.Type == BUILDING_ARROWTOWER || b.Type == BUILDING_CENTER)) {
        std::cout << (b.Type == BUILDING_ARROWTOWER ? " ownTower=" : " ownCenter=")
            << b.SN << ':' << b.Blood << '/' << b.MaxBlood << ':' << b.Project << ':' << b.BlockDR << ',' << b.BlockUR;
    }
    const auto command = bot.orders.find(bot.hero->SN);
    auto printHeroOrder = [&](const char* label,const Order& order) {
        std::cout << label << (order.kind == Go ? "Go:" : (order.kind == Use ? "Use:" : "Other:"));
        if (order.kind == Go) std::cout << int(order.point.x) << ',' << int(order.point.y);
        else std::cout << order.target;
    };
    if (command != bot.orders.end()) printHeroOrder(" heroCmd=",command->second);
    else std::cout << " heroCmd=none";
    const Unit& heroUnit = state.units[bot.hero->SN];
    printHeroOrder(" heroLast=",heroUnit.last);
    std::cout << ':' << (info.GameFrame-heroUnit.sent)*TimePerFrame/1000;
    for (const auto& e : info.enemy_armies) if (e.Blood > 0 && e.Sort != AT_SHIP &&
        (e.WorkObjectSN == bot.hero->SN || reachDist(pos(e),hp) <= 12)) {
        std::cout << " enemyNearHero=" << e.SN << ':' << e.Sort << ':' << e.Blood << ':' << e.WorkObjectSN
            << ':' << e.BlockDR << ',' << e.BlockUR;
    }
    for (auto f : bot.workers) if (f->NowState == HUMAN_STATE_IDLE && info.GameFrame - state.units[f->SN].progress >= frames(3)) {
        const Unit& u = state.units[f->SN];
        std::cout << " idleUnit=" << f->SN << ':' << f->Resource << ':' << f->WorkObjectSN << ':' << u.job << ':' << u.depot << ':' << f->ResourceSort << ':' << f->BlockDR << ',' << f->BlockUR
            << ':' << int(u.last.kind) << ':' << u.last.target << ':' << u.deliveryDistance;
    }
    if(state.phase!=Defend && !state.committed){
        for(const auto& tower:info.enemy_buildings)if(tower.Type==BUILDING_ARROWTOWER && tower.Blood>0 && dist(pos(tower),center)<24)
            std::cout<<" enemyTower="<<tower.SN<<':'<<tower.Blood<<':'<<tower.Project<<':'<<tower.BlockDR<<','<<tower.BlockUR;
        for(const auto& e:info.enemy_armies)if(e.Blood>0 && dist(pos(e),center)<18)
            std::cout<<" frontEnemy="<<e.SN<<':'<<e.Sort<<':'<<e.Blood<<':'<<e.BlockDR<<','<<e.BlockUR;
        for(auto a:bot.army)if(state.dispatched.count(a->SN) && a->NowState!=HUMAN_STATE_ATTACKING &&
            info.GameFrame-state.units[a->SN].progress>=frames(5))
            std::cout<<" stalledArmy="<<a->SN<<':'<<a->Sort<<':'<<a->NowState<<':'<<a->WorkObjectSN
                <<':'<<a->BlockDR<<','<<a->BlockUR<<':'<<int(a->DR0/BLOCKSIDELENGTH)<<','<<int(a->UR0/BLOCKSIDELENGTH);
    }
    std::cout << std::endl;
}
