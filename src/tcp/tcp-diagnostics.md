# TCP retransmission diagnostics

These counters observe the project's pinned ns-3 revision
`c3b0d18fc6d6e61b0cc792eb45d8fe7a1db78d08`. `TcpDiagnosticsSocket`
subclasses `TcpSocketBase` and delegates the TCP implementation to it. No pinned
sources or installed libraries are modified. The subclass neither reschedules
timers nor changes TCP algorithms, packets, or congestion control decisions.

The public ns-3 `Retransmission` trace fires for retransmitted data from
`SendDataPacket`; it does not cover SYN or SYN-ACK. The public `RTO` trace reports
changes to a timeout value, not timer expirations. A `CA_LOSS` transition is also
insufficient: repeated RTOs can occur while TCP remains in that state.

## Counter definitions

| Suggested CSV column | Source and meaning |
| --- | --- |
| `data_retransmissions` | Existing compatible field: sender payload retransmission attempts reported by ns-3's `Retransmission` trace. Counts segments, including repeated retransmission of the same sequence range. |
| `syn_retransmissions` | Sender `Tx` events carrying SYN without ACK after that socket's first SYN. |
| `syn_ack_retransmissions` | Receiver `Tx` events carrying SYN+ACK after that accepted socket's first SYN-ACK. Includes responses to duplicate SYNs. |
| `data_rto_timeouts` | Entries into the actual virtual `ReTxTimeout` handler with a nonempty transmit buffer, excluding CLOSED, TIME_WAIT, and SYN_SENT, matching the pinned handler's data branch. Includes a terminal expiry that exhausts the retry budget without transmitting. |
| `data_rto_retransmissions` | Sender payload retransmissions emitted synchronously inside that data timeout handler. |
| `data_fast_recovery_retransmissions` | Sender payload retransmissions emitted outside the timeout handler while the actual socket congestion state is `CA_RECOVERY`. |
| `data_other_retransmissions` | Remaining sender payload retransmissions. In particular, a later ACK-driven send during `CA_LOSS` is not attributed to the RTO handler. |
| `handshake_timeouts` | Unavailable; write an empty CSV value, never a guessed value or zero. |

The three data retransmission categories partition `data_retransmissions`.
`data_rto_timeouts` counts events rather than packets, so it need not equal
`data_rto_retransmissions`. Fast recovery here describes the state at the actual
send, not a proof that a packet was lost: reordering can also trigger recovery.
No count identifies unique losses, retransmitted bytes, or FCT above a threshold.

Handshake timers invoke `SendEmptyPacket` directly, bypassing `ReTxTimeout`.
That function also sends SYN-ACKs in response to duplicate SYNs. There is no
separate public timer-expiry trace; this helper does not alter timer scheduling
to obtain one. Thus handshake retry counts are exact transmission counts, but
handshake timeout counts remain unknown. The SYN-ACK loss regression illustrates
the distinction: one dropped SYN-ACK causes one SYN retry and two SYN-ACK retries.

## Experiment integration and lifetime

`TcpExperiment` now uses the following wiring; frozen batches continue to use
their original binaries and column set.

1. Add `tcp/tcp-diagnostics.cc` to the `tcp` library and add
   `test/tcp-diagnostics` to the test subdirectories.
2. After installing each host's internet stack, call
   `TcpDiagnosticsSocketFactory::Install(host)`. Select
   `"rng::TcpDiagnosticsSocketFactory"` in both `BulkSendHelper` and
   `PacketSinkHelper`. The factory reads `RttEstimatorType`, `SocketType`, and
   `RecoveryType` from that node's `TcpL4Protocol`, constructs those objects, and
   uses its public `AddSocket` registration path.
3. Store two `std::shared_ptr<TcpDiagnostics>` values in each flow record.
   Once applications have started, obtain them with
   `GetTcpDiagnostics(source->GetSocket())` and
   `GetTcpDiagnostics(sink->GetListeningSocket())`. The existing listener-marking
   callback at start + 1 ns can capture these pointers for **every** flow, with
   small-flow ToS marking conditional as before. These pointers only retrieve
   counters: instrumentation already started in the socket constructors, before
   the source's initial SYN and before any listener fork.
4. At the first sink receipt completing the requested flow bytes, call `Stop()`
   on both diagnostics. Read `Counts()` for output. A defensive completion path
   can retrieve missing pointers directly from the still-active apps if needed.
   The counters survive application disposal and socket destruction; trace
   callbacks capture no application, experiment, or flow pointer. Existing app
   release logic therefore remains valid. A flow that has not started has no
   diagnostic objects; report appended fields as unavailable rather than
   treating missing observation as measured zero.
5. Preserve the existing CSV columns and append the additional named columns.
   Keep `data_retransmissions` tied to the sender's stopped diagnostic count.
   If retaining the older BulkSend trace callback, prevent post-completion
   updates and avoid double counting.

The accepted socket inherits the listener's shared counters in `Fork`. Its
per-socket "already sent SYN-ACK" flag starts false, so an initial SYN-ACK is not
mistaken for a retry. The base copy constructor copies the static `Tx` callback;
that callback uses the socket argument, not a captured pointer to the listener.
The subclass installs the data retransmission callback separately because the
pinned base copy constructor does not copy that trace. This design assumes the
experiment's existing one-listener-per-flow arrangement. A listener shared by
multiple flows would require per-connection attribution.

These counters stop at sink completion, matching FCT. They intentionally exclude
any later loss of the final data ACK and retransmissions after receiver
completion. Incomplete flows retain observations through simulation stop.

## Lightweight validation

The test directory can build against the already installed pinned libraries
without running the repository's ns-3 bootstrap or changing the shared build:

```sh
cmake -S test/tcp-diagnostics -B /tmp/cse561-tcp-diagnostics-build
cmake --build /tmp/cse561-tcp-diagnostics-build --parallel 1
ctest --test-dir /tmp/cse561-tcp-diagnostics-build --output-on-failure
```

The mock sequence checks counter partitions, terminal timeouts without sends,
and the completion cutoff. Nine bounded two-node simulations check a no-loss
baseline with both factories, lost SYN, lost SYN-ACK, failed handshake, one and
two data RTOs, exhausted data retries, and fast recovery. Applications are
created dynamically, counters attach before SYN, and application disposal uses
the experiment's release pattern. Socket registration/removal and retained
counters are checked. The no-loss comparison requires identical complete wire
bytes and arrival timestamps in both directions, identical completion time,
and the configured congestion/recovery implementation types.

Connection reuse and ECN policy remain research decisions; this instrumentation
does not change either.
