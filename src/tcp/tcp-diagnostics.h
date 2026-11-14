#ifndef RNG_TCP_DIAGNOSTICS_H
#define RNG_TCP_DIAGNOSTICS_H

#include "ns3/socket-factory.h"
#include "ns3/tcp-socket-base.h"

#include <cstdint>
#include <memory>

namespace rng
{

struct TcpDiagnosticCounts
{
    uint64_t synRetransmissions{0};
    uint64_t synAckRetransmissions{0};
    uint64_t dataRetransmissions{0};
    uint64_t dataRtoTimeouts{0};
    uint64_t dataRtoRetransmissions{0};
    uint64_t dataFastRecoveryRetransmissions{0};
    uint64_t dataOtherRetransmissions{0};
};

// Counts attempts, not unique lost packets or bytes. Stop at application receipt
// completion; shared ownership keeps the final counters alive after app release.
// A listener and its accepted sockets share one object (one listener per flow).
class TcpDiagnostics
{
  public:
    enum class DataCause
    {
        RTO_HANDLER,
        FAST_RECOVERY,
        OTHER
    };

    void SynRetransmission(bool synAck);
    void DataRetransmission(DataCause cause);
    void DataRtoTimeout();
    void Stop();
    const TcpDiagnosticCounts& Counts() const;

  private:
    TcpDiagnosticCounts m_counts;
    bool m_stopped{false};
};

// Adds observation to the pinned TcpSocketBase implementation without changing
// its retransmission timers, algorithms, packet contents, or callback ordering.
class TcpDiagnosticsSocket : public ns3::TcpSocketBase
{
  public:
    static ns3::TypeId GetTypeId();
    TcpDiagnosticsSocket();
    TcpDiagnosticsSocket(const TcpDiagnosticsSocket& socket);

    std::shared_ptr<TcpDiagnostics> GetDiagnostics() const;

  protected:
    ns3::Ptr<ns3::TcpSocketBase> Fork() override;
    void ReTxTimeout() override;

  private:
    static void OnTx(ns3::Ptr<const ns3::Packet> packet,
                     const ns3::TcpHeader& header,
                     ns3::Ptr<const ns3::TcpSocketBase> socket);
    static void OnRetransmission(ns3::Ptr<const ns3::Packet> packet,
                                 const ns3::TcpHeader& header,
                                 const ns3::Address& local,
                                 const ns3::Address& peer,
                                 ns3::Ptr<const ns3::TcpSocketBase> socket);

    std::shared_ptr<TcpDiagnostics> m_diagnostics;
    // Mutable because ns-3 supplies a const socket in its observational traces.
    mutable bool m_sentSyn{false};
    mutable bool m_sentSynAck{false};
    bool m_inDataRto{false};
};

// Install on each host after InternetStackHelper. Select this protocol in both
// BulkSendHelper and PacketSinkHelper so observation starts before Connect/Listen.
class TcpDiagnosticsSocketFactory : public ns3::SocketFactory
{
  public:
    static ns3::TypeId GetTypeId();
    static void Install(ns3::Ptr<ns3::Node> node);
    ns3::Ptr<ns3::Socket> CreateSocket() override;
};

// Throws if a caller accidentally uses the ordinary TCP socket factory.
std::shared_ptr<TcpDiagnostics> GetTcpDiagnostics(ns3::Ptr<ns3::Socket> socket);

} // namespace rng

#endif // RNG_TCP_DIAGNOSTICS_H
