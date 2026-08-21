/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/**
 * @file fault-traffic-app.h
 * @brief Stochastic burst generator for fragmented EV fault-refresh payloads.
 *
 * Inputs: endpoint, fragment size/count, burst timing, active window, inter-arrival
 * distribution, deterministic-first-burst flag, and RNG stream.
 * Outputs: UDP fault fragments plus a Tx trace carrying timestamp and payload size.
 */
#pragma once

#include "ns3/application.h"
#include "ns3/boolean.h"
#include "ns3/callback.h"
#include "ns3/double.h"
#include "ns3/fatal-error.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cstdint>

namespace ns3 {

// FAULT traffic represents a bounded detailed fault-reporting window. Each
// packet is one application-level diagnostic chunk; two chunks form one full
// detailed refresh in the default YAML. Timing can be fixed for the reference
// model or lognormal for a bounded stochastic robustness model.
class FaultTrafficApp : public Application
{
public:
  static TypeId GetTypeId ()
  {
    static TypeId tid =
      TypeId ("ns3::FaultTrafficApp")
        .SetParent<Application> ()
        .SetGroupName ("Applications")
        .AddConstructor<FaultTrafficApp> ()
        .AddAttribute ("RemoteAddress",
                       "Remote IPv4 address.",
                       Ipv4AddressValue (),
                       MakeIpv4AddressAccessor (&FaultTrafficApp::m_remoteAddress),
                       MakeIpv4AddressChecker ())
        .AddAttribute ("RemotePort",
                       "Remote UDP port.",
                       UintegerValue (0),
                       MakeUintegerAccessor (&FaultTrafficApp::m_remotePort),
                       MakeUintegerChecker<uint16_t> ())
        .AddAttribute ("StartTimeS",
                       "Configured fault-window start time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&FaultTrafficApp::m_startS),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("StopTimeS",
                       "Configured fault-window stop time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&FaultTrafficApp::m_stopS),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("PayloadBytes",
                       "Application payload bytes per FAULT packet.",
                       UintegerValue (0),
                       MakeUintegerAccessor (&FaultTrafficApp::m_payloadBytes),
                       MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("UseFixedIat",
                       "Use PeriodS instead of the lognormal active IAT model.",
                       BooleanValue (true),
                       MakeBooleanAccessor (&FaultTrafficApp::m_useFixedIat),
                       MakeBooleanChecker ())
        .AddAttribute ("PeriodS",
                       "Fixed FAULT active inter-arrival time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&FaultTrafficApp::m_periodS),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("IatLognormalMu",
                       "Log-space Mu for FAULT active inter-arrival time.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&FaultTrafficApp::m_iatMu),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("IatLognormalSigma",
                       "Log-space Sigma for FAULT active inter-arrival time.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&FaultTrafficApp::m_iatSigma),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("MinIatS",
                       "Minimum active inter-arrival time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&FaultTrafficApp::m_minIatS),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("PacketsPerFullRefresh",
                       "Number of packets representing one complete detailed FAULT refresh.",
                       UintegerValue (0),
                       MakeUintegerAccessor (&FaultTrafficApp::m_packetsPerFullRefresh),
                       MakeUintegerChecker<uint32_t> ())
        .AddTraceSource ("Tx",
                         "Per-packet transmit trace: time, payload bytes.",
                         MakeTraceSourceAccessor (&FaultTrafficApp::m_txTrace),
                         "ns3::TracedCallback::TimeUint32")
        .AddTraceSource ("IatDraw",
                         "Drawn FAULT active inter-arrival time in seconds.",
                         MakeTraceSourceAccessor (&FaultTrafficApp::m_iatTrace),
                         "ns3::TracedCallback::Double");
    return tid;
  }

  FaultTrafficApp () = default;
  ~FaultTrafficApp () override = default;

  void SetIatStream (int64_t stream)
  {
    m_iatStream = stream;
    if (m_iatRv)
      {
        m_iatRv->SetStream (stream);
      }
  }

private:
  void StartApplication () override
  {
    // The helper must configure all attributes from YAML before this runs.
    ValidateConfigured ();

    if (!m_socket)
      {
        // UDP carries the application payload through the lower ns-3 stack.
        m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
        m_socket->Connect (InetSocketAddress (m_remoteAddress, m_remotePort));
      }

    if (!m_useFixedIat)
      {
        m_iatRv = CreateObject<LogNormalRandomVariable> ();
        m_iatRv->SetAttribute ("Mu", DoubleValue (m_iatMu));
        m_iatRv->SetAttribute ("Sigma", DoubleValue (m_iatSigma));
        if (m_iatStream >= 0)
          {
            m_iatRv->SetStream (m_iatStream);
          }
      }

    // Delay to the configured FAULT window start.
    const double nowS = Simulator::Now ().GetSeconds ();
    const double delayS = std::max (0.0, m_startS - nowS);
    m_sendEvent = Simulator::Schedule (Seconds (delayS), &FaultTrafficApp::SendOne, this);
  }

  void StopApplication () override
  {
    // Clean shutdown so the app does not keep scheduling packets after stop.
    if (m_sendEvent.IsPending ())
      {
        Simulator::Cancel (m_sendEvent);
      }
    if (m_socket)
      {
        m_socket->Close ();
        m_socket = nullptr;
      }
  }

  void SendOne ()
  {
    // Do not send outside the configured reporting window.
    const double nowS = Simulator::Now ().GetSeconds ();
    if (nowS < m_startS || nowS >= m_stopS)
      {
        return;
      }

    // Create one detailed FAULT chunk as application payload bytes.
    Ptr<Packet> packet = Create<Packet> (m_payloadBytes);
    m_socket->Send (packet);
    m_txTrace (Simulator::Now (), m_payloadBytes);
    ++m_packetIndex;

    // Select the next detailed-report chunk gap from the configured timing model.
    double iatS = m_useFixedIat ? m_periodS : std::max (m_minIatS, m_iatRv->GetValue ());
    iatS = std::max (iatS, 1e-12);
    m_iatTrace (iatS);

    const double nextS = nowS + iatS;
    if (nextS < m_stopS - 1e-12)
      {
        m_sendEvent = Simulator::Schedule (Seconds (iatS), &FaultTrafficApp::SendOne, this);
      }
  }

  void ValidateConfigured () const
  {
    // Fail fast if any required YAML-derived value is missing.
    NS_ABORT_MSG_IF (m_remotePort == 0, "FaultTrafficApp RemotePort was not configured from YAML.");
    NS_ABORT_MSG_IF (m_payloadBytes == 0, "FaultTrafficApp PayloadBytes was not configured from YAML.");
    if (m_useFixedIat)
      {
        NS_ABORT_MSG_IF (m_periodS <= 0.0, "FaultTrafficApp PeriodS must be positive.");
      }
    else
      {
        NS_ABORT_MSG_IF (m_iatSigma <= 0.0, "FaultTrafficApp IatLognormalSigma must be positive.");
        NS_ABORT_MSG_IF (m_minIatS < 0.0, "FaultTrafficApp MinIatS cannot be negative.");
      }
    NS_ABORT_MSG_IF (m_packetsPerFullRefresh == 0, "FaultTrafficApp PacketsPerFullRefresh must be positive.");
    NS_ABORT_MSG_IF (m_stopS <= m_startS, "FaultTrafficApp StopTimeS must be greater than StartTimeS.");
  }

  Ptr<Socket> m_socket;                 // UDP socket used by this app.
  EventId m_sendEvent;                  // Next scheduled fault packet.
  Ipv4Address m_remoteAddress;          // Receiver address.
  uint16_t m_remotePort = 0;            // Receiver UDP port.
  double m_startS = 0.0;                // Fault-window start from YAML.
  double m_stopS = 0.0;                 // Fault-window stop from YAML.
  uint32_t m_payloadBytes = 0;          // Application payload bytes per packet.
  bool m_useFixedIat = true;            // True for the deterministic reference model.
  double m_periodS = 0.0;               // Fixed high-rate IAT in seconds.
  double m_iatMu = 0.0;                 // Lognormal Mu for stochastic active IAT.
  double m_iatSigma = 0.0;              // Lognormal Sigma for stochastic active IAT.
  double m_minIatS = 0.0;               // Numerical/scheduling IAT floor.
  uint32_t m_packetsPerFullRefresh = 0; // Chunks per detailed refresh.
  uint64_t m_packetIndex = 0;           // Internal sequence counter.
  int64_t m_iatStream = -1;             // Optional fixed RNG stream from scenario YAML.
  Ptr<LogNormalRandomVariable> m_iatRv; // Stochastic active-IAT random variable.
  TracedCallback<Time, uint32_t> m_txTrace; // Emits (time, payload bytes).
  TracedCallback<double> m_iatTrace;    // Emits drawn IAT values.
};

} // namespace ns3
