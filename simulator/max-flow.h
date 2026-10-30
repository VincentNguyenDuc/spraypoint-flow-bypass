#ifndef RNG_MAX_FLOW_H
#define RNG_MAX_FLOW_H

#include <cstdint>
#include <vector>

namespace rng
{

// Unit-capacity Dinic max flow, used to count edge-disjoint paths.
class Dinic
{
  public:
    explicit Dinic(uint32_t n);

    void AddEdge(uint32_t from, uint32_t to);
    uint32_t MaxFlow(uint32_t source, uint32_t sink);

  private:
    struct Edge
    {
        uint32_t to;
        uint32_t reverse;
        uint32_t capacity;
    };

    bool BuildLevels(uint32_t source, uint32_t sink);
    uint32_t Send(uint32_t u, uint32_t sink, uint32_t available);

    std::vector<std::vector<Edge>> m_graph;
    std::vector<int> m_level;
    std::vector<uint32_t> m_next;
};

} // namespace rng

#endif // RNG_MAX_FLOW_H
