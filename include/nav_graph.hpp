#pragma once
// Test44: MW2's AI navigation graph (path nodes and their links), read from
// the level, for choosing zombie spawn points from which a zombie can walk to
// the player. Test43 on Gulag showers spawned every zombie where no path
// reached the player (other wings of the prison); Favela put some on roofs.
//
// Layout (exact x64 build, see the Test44 section of zombie_bindings.hpp):
// node (0xa8 bytes): s32 type (+0, 0 = bad node), vec3 origin (+0x14), u16
// totalLinkCount (+0x38), pathlink* links (+0x40), s16 wLinkCount (+0x6c);
// link (0xc bytes): float distance (+0), u16 node (+4), u8 disconnectCount
// (+6). The engine's A* walks only links[0 .. wLinkCount): disconnecting a
// link (a closed door, disconnectpaths) moves it past wLinkCount.
//
// A zombie walks along links; it can reach the player from node n when a
// chain of connected links leads from n to the player's node, so distances
// are found by Dijkstra over the reversed links, starting at the player.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace mod::nav {
inline constexpr std::size_t kNodeBytes=0xa8,kNodeType=0x0,kNodeOrigin=0x14,kNodeTotalLinks=0x38,kNodeLinks=0x40,kNodeLinkCount=0x6c;
inline constexpr std::size_t kLinkBytes=0xc,kLinkDistance=0x0,kLinkNode=0x4,kLinkDisconnect=0x6;
inline constexpr std::uint32_t kMaxNodes=0x2000;          // Path_LoadNode's limit
inline constexpr std::uint16_t kMaxLinksPerNode=256;      // sanity bound on totalLinkCount
inline constexpr float kUnreachable=std::numeric_limits<float>::infinity();
using Point=std::array<float,3>;

struct Link {std::uint32_t node=0;float distance=0.f;};
struct Node {Point origin{};std::int32_t type=0;bool usable=false;std::uint32_t first=0,count=0;};
struct Graph {
    std::vector<Node> nodes;
    std::vector<Link> links;                  // forward: node -> link.node
    std::vector<std::uint32_t> inFirst;      // reversed adjacency (size nodes+1)
    std::vector<Link> inLinks;               // link.node = the node the link comes from
    std::size_t usableNodes=0,connectedLinks=0,disconnectedLinks=0;
    bool empty() const {return nodes.empty();}
};

template<class T> T Field(const std::uint8_t* p,std::size_t offset) {T v{};std::memcpy(&v,p+offset,sizeof(v));return v;}
inline float Distance(const Point& a,const Point& b) {
    const float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return std::sqrt(x*x+y*y+z*z);
}

// Decodes `count` raw nodes; linksOf(node index, links pointer, total) must
// return a pointer to total*kLinkBytes raw link bytes, or nullptr when they
// cannot be read (the node then has no links).
template<class LinksOf> bool Decode(const std::uint8_t* raw,std::size_t count,LinksOf linksOf,Graph& out) {
    out=Graph{};
    if(!raw || !count || count>kMaxNodes) return false;
    out.nodes.resize(count);
    for(std::size_t n=0;n<count;++n) {
        const auto* p=raw+n*kNodeBytes;
        auto& node=out.nodes[n];
        for(int k=0;k<3;++k) node.origin[static_cast<std::size_t>(k)]=Field<float>(p,kNodeOrigin+static_cast<std::size_t>(k)*4);
        const bool finite=std::isfinite(node.origin[0]) && std::isfinite(node.origin[1]) && std::isfinite(node.origin[2]);
        // As Path_NodesInRadius: a node counts while it has active links.
        node.type=Field<std::int32_t>(p,kNodeType);
        node.usable=finite && node.type!=0 && Field<std::int16_t>(p,kNodeLinkCount)>0;
        if(node.usable) ++out.usableNodes;
    }
    for(std::size_t n=0;n<count;++n) {
        const auto* p=raw+n*kNodeBytes;
        auto& node=out.nodes[n];
        node.first=static_cast<std::uint32_t>(out.links.size());
        const auto total=Field<std::uint16_t>(p,kNodeTotalLinks);
        const auto active=Field<std::int16_t>(p,kNodeLinkCount);
        const auto pointer=Field<std::uint64_t>(p,kNodeLinks);
        if(!node.usable || !total || total>kMaxLinksPerNode || !pointer) continue;
        const std::uint8_t* links=linksOf(n,pointer,total);
        if(!links) continue;
        out.disconnectedLinks+=total-std::min<std::uint16_t>(total,static_cast<std::uint16_t>(active));
        for(std::uint16_t k=0;k<total && k<active;++k) {
            const auto* l=links+static_cast<std::size_t>(k)*kLinkBytes;
            const auto to=Field<std::uint16_t>(l,kLinkNode);
            if(Field<std::uint8_t>(l,kLinkDisconnect)) {++out.disconnectedLinks;continue;}
            if(to>=count || to==n || !out.nodes[to].usable) continue;
            float d=Field<float>(l,kLinkDistance);
            if(!std::isfinite(d) || d<0.f || d>65536.f) d=Distance(node.origin,out.nodes[to].origin);
            out.links.push_back({to,d});++out.connectedLinks;
        }
        node.count=static_cast<std::uint32_t>(out.links.size())-node.first;
    }
    // Reversed adjacency (counting sort by target).
    out.inFirst.assign(count+1,0);
    for(const auto& l:out.links) ++out.inFirst[l.node+1];
    for(std::size_t n=0;n<count;++n) out.inFirst[n+1]+=out.inFirst[n];
    out.inLinks.resize(out.links.size());
    auto cursor=out.inFirst;
    for(std::size_t n=0;n<count;++n) {
        const auto& node=out.nodes[n];
        for(std::uint32_t k=node.first;k<node.first+node.count;++k) {
            const auto& l=out.links[k];
            out.inLinks[cursor[l.node]++]={static_cast<std::uint32_t>(n),l.distance};
        }
    }
    return true;
}

// Walking distance from every node to `target` along connected links
// (kUnreachable when no chain of links leads there).
inline std::vector<float> DistancesTo(const Graph& g,int target) {
    std::vector<float> d(g.nodes.size(),kUnreachable);
    if(target<0 || static_cast<std::size_t>(target)>=g.nodes.size() || !g.nodes[static_cast<std::size_t>(target)].usable) return d;
    using Item=std::pair<float,std::uint32_t>;
    std::priority_queue<Item,std::vector<Item>,std::greater<Item>> open;
    d[static_cast<std::size_t>(target)]=0.f;open.push({0.f,static_cast<std::uint32_t>(target)});
    while(!open.empty()) {
        const auto [cost,n]=open.top();open.pop();
        if(cost>d[n]) continue;
        for(std::uint32_t k=g.inFirst[n];k<g.inFirst[n+1];++k) {
            const auto& from=g.inLinks[k];
            const float next=cost+from.distance;
            if(next<d[from.node]) {d[from.node]=next;open.push({next,from.node});}
        }
    }
    return d;
}

// Usable nodes near `p`, nearest first: horizontally within maxHorizontal,
// vertically within maxVertical (vertical distance counts double in the
// ordering, so a node on the floor below loses to one beside the point).
inline std::vector<int> NearestNodes(const Graph& g,const Point& p,float maxHorizontal,float maxVertical,std::size_t limit) {
    std::vector<std::pair<float,int>> found;
    for(std::size_t n=0;n<g.nodes.size();++n) {
        const auto& node=g.nodes[n];
        if(!node.usable) continue;
        const float x=node.origin[0]-p[0],y=node.origin[1]-p[1],z=std::fabs(node.origin[2]-p[2]);
        const float h=x*x+y*y;
        if(h>maxHorizontal*maxHorizontal || z>maxVertical) continue;
        found.push_back({h+4.f*z*z,static_cast<int>(n)});
    }
    std::sort(found.begin(),found.end());
    std::vector<int> out;
    for(std::size_t k=0;k<found.size() && out.size()<limit;++k) out.push_back(found[k].second);
    return out;
}

// Node types a zombie may rise at when a level has too few reachable
// spawners: path, cover, ambush, exposed and concealment nodes (1..12), not
// doors, scripted, traversal (negotiation) or turret nodes.
inline constexpr std::int32_t kFirstStandNodeType=1,kLastStandNodeType=12;
inline bool StandNode(const Node& n) {return n.usable && n.type>=kFirstStandNodeType && n.type<=kLastStandNodeType;}

// A spawn point's walking distance to the player: through its anchor node.
inline float PathDistance(const Graph& g,const std::vector<float>& toPlayer,int anchor,const Point& p) {
    if(anchor<0 || static_cast<std::size_t>(anchor)>=toPlayer.size()) return kUnreachable;
    const float d=toPlayer[static_cast<std::size_t>(anchor)];
    return std::isfinite(d)?d+Distance(p,g.nodes[static_cast<std::size_t>(anchor)].origin):kUnreachable;
}
} // namespace mod::nav
