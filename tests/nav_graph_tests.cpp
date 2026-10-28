// Test44: the navigation graph reader and the walking distances used to pick
// zombie spawn points (include/nav_graph.hpp), on raw nodes and links laid
// out as the x64 build stores them.
#include "nav_graph.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>

using namespace mod::nav;
namespace {
struct RawGraph {
    std::vector<std::uint8_t> nodes;
    std::map<std::uint64_t,std::vector<std::uint8_t>> links;   // fake pointer -> bytes
    std::uint64_t next=0x1000;
};
template<class T> void Put(std::uint8_t* p,std::size_t o,T v) {std::memcpy(p+o,&v,sizeof(v));}
struct LinkSpec {std::uint16_t to;float distance;std::uint8_t disconnect=0;};
// links[0..active) are the connected ones, as the engine keeps them.
void AddNode(RawGraph& g,std::int32_t type,Point at,const std::vector<LinkSpec>& links,int active=-1) {
    const auto n=g.nodes.size()/kNodeBytes;g.nodes.resize((n+1)*kNodeBytes,0);
    auto* p=g.nodes.data()+n*kNodeBytes;
    Put(p,kNodeType,type);
    for(int k=0;k<3;++k) Put(p,kNodeOrigin+static_cast<std::size_t>(k)*4,at[static_cast<std::size_t>(k)]);
    Put<std::uint16_t>(p,kNodeTotalLinks,static_cast<std::uint16_t>(links.size()));
    Put<std::int16_t>(p,kNodeLinkCount,static_cast<std::int16_t>(active<0?static_cast<int>(links.size()):active));
    if(links.empty()) return;
    std::vector<std::uint8_t> raw(links.size()*kLinkBytes,0);
    for(std::size_t k=0;k<links.size();++k) {
        Put(raw.data()+k*kLinkBytes,kLinkDistance,links[k].distance);
        Put(raw.data()+k*kLinkBytes,kLinkNode,links[k].to);
        Put(raw.data()+k*kLinkBytes,kLinkDisconnect,links[k].disconnect);
    }
    Put<std::uint64_t>(p,kNodeLinks,g.next);g.links[g.next]=raw;g.next+=0x1000;
}
bool Build(RawGraph& g,Graph& out) {
    return Decode(g.nodes.data(),g.nodes.size()/kNodeBytes,[&](std::size_t,std::uint64_t pointer,std::uint16_t total) -> const std::uint8_t* {
        const auto found=g.links.find(pointer);
        return found!=g.links.end() && found->second.size()>=total*kLinkBytes?found->second.data():nullptr;
    },out);
}
}

int main() {
    // A corridor 0-1-2-3 (100 units apart), node 4 on a roof reachable only
    // by a one-way drop 4->3, node 5 behind a closed door (link 2<->5
    // disconnected: moved past wLinkCount), node 6 with no active links.
    RawGraph raw;
    AddNode(raw,1,{0,0,0},{{1,100}});
    AddNode(raw,1,{100,0,0},{{0,100},{2,100}});
    AddNode(raw,1,{200,0,0},{{1,100},{3,100},{5,100,1}},2);
    AddNode(raw,1,{300,0,0},{{2,100}});
    AddNode(raw,16,{300,0,300},{{3,320}});                    // negotiation begin: drop to 3
    AddNode(raw,1,{200,100,0},{{2,100,1}},0);
    AddNode(raw,1,{0,500,0},{});
    Graph g;
    assert(Build(raw,g));
    assert(g.nodes.size()==7 && g.usableNodes==5);              // nodes 5, 6: no active links
    assert(!g.nodes[6].usable && g.nodes[5].usable==false);     // node 5: wLinkCount 0
    assert(g.connectedLinks==7 && g.disconnectedLinks==1);    // 2->5 past wLinkCount
    // Distances to the player at node 0.
    const auto d=DistancesTo(g,0);
    assert(d[0]==0.f && d[1]==100.f && d[2]==200.f && d[3]==300.f);
    assert(d[4]==620.f);                                        // down the drop, then the corridor
    assert(!std::isfinite(d[5]) && !std::isfinite(d[6]));
    // Player on the roof: nothing can climb up the one-way drop.
    const auto roof=DistancesTo(g,4);
    assert(roof[4]==0.f && !std::isfinite(roof[0]) && !std::isfinite(roof[3]));
    // Anchors: the nearest usable node beside the point, not the one above it.
    const auto near=NearestNodes(g,{300,10,20},128.f,64.f,4);
    assert(!near.empty() && near[0]==3);
    assert(NearestNodes(g,{300,0,280},128.f,64.f,4).front()==4);
    assert(NearestNodes(g,{0,500,0},64.f,64.f,4).empty());      // only an unusable node there
    assert(NearestNodes(g,{5000,0,0},128.f,64.f,4).empty());
    assert(StandNode(g.nodes[0]) && !StandNode(g.nodes[4]) && !StandNode(g.nodes[5]));   // 4: traversal, 5: unusable
    const float pd=PathDistance(g,d,3,{300,30,0});
    assert(std::fabs(pd-330.f)<0.01f);
    assert(!std::isfinite(PathDistance(g,d,-1,{0,0,0})));
    // Links pointing outside the graph or to themselves are ignored; a bad
    // stored distance falls back to the straight line.
    RawGraph odd;
    AddNode(odd,1,{0,0,0},{{0,10},{7,10},{1,std::numeric_limits<float>::quiet_NaN()}});
    AddNode(odd,1,{30,40,0},{{0,50}});
    Graph o;assert(Build(odd,o));
    assert(o.connectedLinks==2 && std::fabs(o.links[o.nodes[0].first].distance-50.f)<0.01f);
    // Unreadable links: the node keeps no links.
    RawGraph lost;AddNode(lost,1,{0,0,0},{{1,10}});AddNode(lost,1,{10,0,0},{{0,10}});
    lost.links.clear();
    Graph l;assert(Build(lost,l) && l.connectedLinks==0);
    // Bounds.
    Graph none;assert(!Decode(nullptr,0,[](std::size_t,std::uint64_t,std::uint16_t){return static_cast<const std::uint8_t*>(nullptr);},none));
    std::vector<std::uint8_t> big((kMaxNodes+1)*kNodeBytes,0);
    assert(!Decode(big.data(),kMaxNodes+1,[](std::size_t,std::uint64_t,std::uint16_t){return static_cast<const std::uint8_t*>(nullptr);},none));
    std::puts("nav_graph_tests passed");
    return 0;
}
