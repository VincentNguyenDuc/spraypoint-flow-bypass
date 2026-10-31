#include "max-flow.h"

#include <algorithm>
#include <limits>
#include <queue>

namespace rng
{

Dinic::Dinic(uint32_t n)
    : m_graph(n),
      m_level(n),
      m_next(n)
{
}

void
Dinic::AddEdge(uint32_t from, uint32_t to)
{
    Edge forward{to, static_cast<uint32_t>(m_graph[to].size()), 1};
    Edge reverse{from, static_cast<uint32_t>(m_graph[from].size()), 0};
    m_graph[from].push_back(forward);
    m_graph[to].push_back(reverse);
}

uint32_t
Dinic::MaxFlow(uint32_t source, uint32_t sink)
{
    uint32_t flow = 0;
    while (BuildLevels(source, sink))
    {
        std::fill(m_next.begin(), m_next.end(), 0);
        while (uint32_t pushed = Send(source, sink, std::numeric_limits<uint32_t>::max()))
        {
            flow += pushed;
        }
    }
    return flow;
}

bool
Dinic::BuildLevels(uint32_t source, uint32_t sink)
{
    std::fill(m_level.begin(), m_level.end(), -1);
    std::queue<uint32_t> todo;
    m_level[source] = 0;
    todo.push(source);
    while (!todo.empty())
    {
        auto u = todo.front();
        todo.pop();
        for (const auto& edge : m_graph[u])
        {
            if (edge.capacity && m_level[edge.to] < 0)
            {
                m_level[edge.to] = m_level[u] + 1;
                todo.push(edge.to);
            }
        }
    }
    return m_level[sink] >= 0;
}

uint32_t
Dinic::Send(uint32_t u, uint32_t sink, uint32_t available)
{
    if (u == sink)
    {
        return available;
    }
    for (auto& i = m_next[u]; i < m_graph[u].size(); ++i)
    {
        auto& edge = m_graph[u][i];
        if (!edge.capacity || m_level[edge.to] != m_level[u] + 1)
        {
            continue;
        }
        auto pushed = Send(edge.to, sink, std::min(available, edge.capacity));
        if (pushed)
        {
            edge.capacity -= pushed;
            m_graph[edge.to][edge.reverse].capacity += pushed;
            return pushed;
        }
    }
    return 0;
}

} // namespace rng
