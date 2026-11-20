#include "tcp-diagnostics.h"

#include "ns3/node.h"
#include "ns3/object-factory.h"
#include "ns3/rtt-estimator.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-l4-protocol.h"
#include "ns3/tcp-recovery-ops.h"
#include "ns3/tcp-tx-buffer.h"
#include "ns3/type-id.h"

#include <stdexcept>

namespace rng
{

NS_OBJECT_ENSURE_REGISTERED(TcpDiagnosticsSocket);
NS_OBJECT_ENSURE_REGISTERED(TcpDiagnosticsSocketFactory);

void
TcpDiagnostics::SynRetransmission(bool synAck)
{
    if (!m_stopped)
    {
        ++(synAck ? m_counts.synAckRetransmissions : m_counts.synRetransmissions);
    }
}

void
TcpDiagnostics::DataRetransmission(DataCause cause)
{
    if (m_stopped)
    {
        return;
    }
    ++m_counts.dataRetransmissions;
    switch (cause)
    {
    case DataCause::RTO_HANDLER:
        ++m_counts.dataRtoRetransmissions;
        break;
    case DataCause::FAST_RECOVERY:
        ++m_counts.dataFastRecoveryRetransmissions;
        break;
    case DataCause::OTHER:
        ++m_counts.dataOtherRetransmissions;
        break;
    }
}

void
TcpDiagnostics::DataRtoTimeout()
{
    if (!m_stopped)
    {
        ++m_counts.dataRtoTimeouts;
    }
}

void
TcpDiagnostics::Stop()
{
    m_stopped = true;
}

const TcpDiagnosticCounts&
TcpDiagnostics::Counts() const
{
    return m_counts;
}

ns3::TypeId
TcpDiagnosticsSocket::GetTypeId()
{
    static ns3::TypeId tid = ns3::TypeId("rng::TcpDiagnosticsSocket")
                                .SetParent<ns3::TcpSocketBase>()
                                .AddConstructor<TcpDiagnosticsSocket>();
    return tid;
}

TcpDiagnosticsSocket::TcpDiagnosticsSocket()
    : m_diagnostics(std::make_shared<TcpDiagnostics>())
{
    // The base copy constructor copies Tx callbacks. A static callback inspects
    // the traced socket, never a captured listener pointer, so it also observes
    // the child's very first SYN-ACK safely.
    m_txTrace.ConnectWithoutContext(ns3::MakeCallback(&TcpDiagnosticsSocket::OnTx));
    m_retransmissionTrace.ConnectWithoutContext(
        ns3::MakeCallback(&TcpDiagnosticsSocket::OnRetransmission));
}

TcpDiagnosticsSocket::TcpDiagnosticsSocket(const TcpDiagnosticsSocket& socket)
    : ns3::TcpSocketBase(socket),
      m_diagnostics(socket.m_diagnostics)
{
    // In the pinned version, unlike Tx, Retransmission is not copied by the
    // TcpSocketBase copy constructor. Register exactly once on the new child.
    m_retransmissionTrace.ConnectWithoutContext(
        ns3::MakeCallback(&TcpDiagnosticsSocket::OnRetransmission));
}

std::shared_ptr<TcpDiagnostics>
TcpDiagnosticsSocket::GetDiagnostics() const
{
    return m_diagnostics;
}

ns3::Ptr<ns3::TcpSocketBase>
TcpDiagnosticsSocket::Fork()
{
    return ns3::CopyObject<TcpDiagnosticsSocket>(this);
}

void
TcpDiagnosticsSocket::ReTxTimeout()
{
    // These are precisely the pinned base handler's non-data early returns.
    // Count the data expiry even when the retry budget is exhausted and the
    // handler closes the connection without transmitting another packet.
    const bool dataTimeout = m_state != CLOSED && m_state != TIME_WAIT && m_state != SYN_SENT &&
                             m_txBuffer->Size() != 0;
    if (dataTimeout)
    {
        m_diagnostics->DataRtoTimeout();
    }
    const bool previous = m_inDataRto;
    m_inDataRto = dataTimeout;
    ns3::TcpSocketBase::ReTxTimeout();
    m_inDataRto = previous;
}

void
TcpDiagnosticsSocket::OnTx(ns3::Ptr<const ns3::Packet>,
                           const ns3::TcpHeader& header,
                           ns3::Ptr<const ns3::TcpSocketBase> socket)
{
    if (!(header.GetFlags() & ns3::TcpHeader::SYN))
    {
        return;
    }
    const auto observed = ns3::DynamicCast<const TcpDiagnosticsSocket>(socket);
    const bool synAck = header.GetFlags() & ns3::TcpHeader::ACK;
    bool& sent = synAck ? observed->m_sentSynAck : observed->m_sentSyn;
    if (sent)
    {
        observed->m_diagnostics->SynRetransmission(synAck);
    }
    sent = true;
}

void
TcpDiagnosticsSocket::OnRetransmission(ns3::Ptr<const ns3::Packet> packet,
                                       const ns3::TcpHeader&,
                                       const ns3::Address&,
                                       const ns3::Address&,
                                       ns3::Ptr<const ns3::TcpSocketBase> socket)
{
    if (packet->GetSize() == 0)
    {
        return;
    }
    const auto observed = ns3::DynamicCast<const TcpDiagnosticsSocket>(socket);
    auto cause = TcpDiagnostics::DataCause::OTHER;
    if (observed->m_inDataRto)
    {
        cause = TcpDiagnostics::DataCause::RTO_HANDLER;
    }
    else if (observed->m_tcb->m_congState == ns3::TcpSocketState::CA_RECOVERY)
    {
        cause = TcpDiagnostics::DataCause::FAST_RECOVERY;
    }
    observed->m_diagnostics->DataRetransmission(cause);
}

ns3::TypeId
TcpDiagnosticsSocketFactory::GetTypeId()
{
    static ns3::TypeId tid = ns3::TypeId("rng::TcpDiagnosticsSocketFactory")
                                .SetParent<ns3::SocketFactory>()
                                .AddConstructor<TcpDiagnosticsSocketFactory>();
    return tid;
}

void
TcpDiagnosticsSocketFactory::Install(ns3::Ptr<ns3::Node> node)
{
    if (!node->GetObject<TcpDiagnosticsSocketFactory>())
    {
        node->AggregateObject(ns3::CreateObject<TcpDiagnosticsSocketFactory>());
    }
}

ns3::Ptr<ns3::Socket>
TcpDiagnosticsSocketFactory::CreateSocket()
{
    const auto node = GetObject<ns3::Node>();
    const auto tcp = node->GetObject<ns3::TcpL4Protocol>();
    if (!tcp)
    {
        throw std::runtime_error("TCP diagnostics require an installed InternetStackHelper");
    }
    // Match TcpL4Protocol::CreateSocket using the same configured algorithms.
    const auto factoryFor = [&tcp](const char* attribute) {
        ns3::TypeIdValue type;
        tcp->GetAttribute(attribute, type);
        ns3::ObjectFactory factory;
        factory.SetTypeId(type.Get());
        return factory;
    };
    auto socket = ns3::CreateObject<TcpDiagnosticsSocket>();
    socket->SetNode(node);
    socket->SetTcp(tcp);
    socket->SetRtt(factoryFor("RttEstimatorType").Create<ns3::RttEstimator>());
    socket->SetCongestionControlAlgorithm(factoryFor("SocketType").Create<ns3::TcpCongestionOps>());
    socket->SetRecoveryAlgorithm(factoryFor("RecoveryType").Create<ns3::TcpRecoveryOps>());
    tcp->AddSocket(socket);
    return socket;
}

std::shared_ptr<TcpDiagnostics>
GetTcpDiagnostics(ns3::Ptr<ns3::Socket> socket)
{
    const auto observed = ns3::DynamicCast<TcpDiagnosticsSocket>(socket);
    if (!observed)
    {
        throw std::runtime_error("TCP diagnostics socket factory was not selected");
    }
    return observed->GetDiagnostics();
}

} // namespace rng
