/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/**
 * @file alert-traffic-app.h
 * @brief Stochastic UDP generator for time-sensitive EV alert messages.
 *
 * Inputs: endpoint, packet size, active window, lognormal inter-arrival parameters,
 * deterministic-first-packet flag, and RNG stream.
 * Outputs: UDP alert packets plus a Tx trace carrying timestamp and payload size.
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

// ALERT traffic represents a bounded abnormal-condition reporting window.
// The window start/stop are scenario-controlled. Packet spacing can be fixed
// for an application-level report process or lognormal for a fitted burst model.
class AlertTrafficApp : public Application
{
public:
  static TypeId GetTypeId ()
  {
    static TypeId tid =
      TypeId ("ns3::AlertTrafficApp")
        .SetParent<Application> ()
        .SetGroupName ("Applications")
        .AddConstructor<AlertTrafficApp> ()
        .AddAttribute ("RemoteAddress",
                       "Remote IPv4 address.",
                       Ipv4AddressValue (),
                       MakeIpv4AddressAccessor (&AlertTrafficApp::m_remoteAddress),
                       MakeIpv4AddressChecker ())
        .AddAttribute ("RemotePort",
                       "Remote UDP port.",
                       UintegerValue (0),
                       MakeUintegerAccessor (&AlertTrafficApp::m_remotePort),
                       MakeUintegerChecker<uint16_t> ())
        .AddAttribute ("StartTimeS",
                       "Configured alert-window start time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&AlertTrafficApp::m_startS),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("StopTimeS",
                       "Configured alert-window stop time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&AlertTrafficApp::m_stopS),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("PayloadBytes",
                       "Application payload bytes per ALERT packet.",
                       UintegerValue (0),
                       MakeUintegerAccessor (&AlertTrafficApp::m_payloadBytes),
                       MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("UseFixedIat",
                       "Use FixedIatS instead of the lognormal active IAT model.",
                       BooleanValue (false),
                       MakeBooleanAccessor (&AlertTrafficApp::m_useFixedIat),
                       MakeBooleanChecker ())
        .AddAttribute ("FixedIatS",
                       "Fixed ALERT active inter-arrival time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&AlertTrafficApp::m_fixedIatS),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("IatLognormalMu",
                       "Log-space Mu for ALERT active inter-arrival time.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&AlertTrafficApp::m_iatMu),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("IatLognormalSigma",
                       "Log-space Sigma for ALERT active inter-arrival time.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&AlertTrafficApp::m_iatSigma),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("MinIatS",
                       "Minimum active inter-arrival time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&AlertTrafficApp::m_minIatS),
                       MakeDoubleChecker<double> ())
        .AddTraceSource ("Tx",
                         "Per-packet transmit trace: time, payload bytes.",
                         MakeTraceSourceAccessor (&AlertTrafficApp::m_txTrace),
                         "ns3::TracedCallback::TimeUint32")
        .AddTraceSource ("IatDraw",
                         "Drawn ALERT active inter-arrival time in seconds.",
                         MakeTraceSourceAccessor (&AlertTrafficApp::m_iatTrace),
                         "ns3::TracedCallback::Double");
    return tid;
  }

  AlertTrafficApp () = default;
  ~AlertTrafficApp () override = default;

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
    // Validate before allocating RNGs or scheduling traffic so bad YAML fails
    // early and visibly.
    ValidateConfigured ();

    if (!m_socket)
      {
        // The app emits UDP payloads toward the remote maintenance endpoint.
        m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
        m_socket->Connect (InetSocketAddress (m_remoteAddress, m_remotePort));
      }

    if (!m_useFixedIat)
      {
        // ns-3 LogNormalRandomVariable uses log-space Mu and Sigma. The YAML
        // stores Mu = ln(SciPy scale), Sigma = SciPy shape.
        m_iatRv = CreateObject<LogNormalRandomVariable> ();
        m_iatRv->SetAttribute ("Mu", DoubleValue (m_iatMu));
        m_iatRv->SetAttribute ("Sigma", DoubleValue (m_iatSigma));
        if (m_iatStream >= 0)
          {
            // Keep ALERT generation independent from radio/channel random streams.
            m_iatRv->SetStream (m_iatStream);
          }
      }

    const double nowS = Simulator::Now ().GetSeconds ();
    const double delayS = std::max (0.0, m_startS - nowS);
    m_sendEvent = Simulator::Schedule (Seconds (delayS), &AlertTrafficApp::SendOne, this);
  }

  void StopApplication () override
  {
    // Stop future sends and release the socket.
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
    // Only transmit inside the configured alert reporting window.
    const double nowS = Simulator::Now ().GetSeconds ();
    if (nowS < m_startS || nowS >= m_stopS)
      {
        return;
      }

    // The payload is the ALERT application message. It is not a CAN frame.
    Ptr<Packet> packet = Create<Packet> (m_payloadBytes);
    m_socket->Send (packet);
    m_txTrace (Simulator::Now (), m_payloadBytes);

    // Select the next packet gap from the configured ALERT timing model.
    double iatS = m_useFixedIat ? m_fixedIatS : std::max (m_minIatS, m_iatRv->GetValue ());
    iatS = std::max (iatS, 1e-12);
    m_iatTrace (iatS);

    const double nextS = nowS + iatS;
    if (nextS < m_stopS - 1e-12)
      {
        m_sendEvent = Simulator::Schedule (Seconds (iatS), &AlertTrafficApp::SendOne, this);
      }
  }

  void ValidateConfigured () const
  {
    // These checks catch missing installer/YAML wiring before transmission.
    NS_ABORT_MSG_IF (m_remotePort == 0, "AlertTrafficApp RemotePort was not configured from YAML.");
    NS_ABORT_MSG_IF (m_payloadBytes == 0, "AlertTrafficApp PayloadBytes was not configured from YAML.");
    if (m_useFixedIat)
      {
        NS_ABORT_MSG_IF (m_fixedIatS <= 0.0, "AlertTrafficApp FixedIatS must be positive.");
      }
    else
      {
        NS_ABORT_MSG_IF (m_iatSigma <= 0.0, "AlertTrafficApp IatLognormalSigma must be positive.");
        NS_ABORT_MSG_IF (m_minIatS < 0.0, "AlertTrafficApp MinIatS cannot be negative.");
      }
    NS_ABORT_MSG_IF (m_stopS <= m_startS, "AlertTrafficApp StopTimeS must be greater than StartTimeS.");
  }

  Ptr<Socket> m_socket;                 // UDP socket used by this app.
  EventId m_sendEvent;                  // Next scheduled alert packet.
  Ipv4Address m_remoteAddress;          // Receiver address.
  uint16_t m_remotePort = 0;            // Receiver UDP port.
  double m_startS = 0.0;                // Alert-window start from YAML.
  double m_stopS = 0.0;                 // Alert-window stop from YAML.
  uint32_t m_payloadBytes = 0;          // Application payload bytes per packet.
  bool m_useFixedIat = false;           // True for application-level fixed reports.
  double m_fixedIatS = 0.0;             // Fixed IAT for ALERT report updates.
  double m_iatMu = 0.0;                 // Lognormal Mu for active IAT.
  double m_iatSigma = 0.0;              // Lognormal Sigma for active IAT.
  double m_minIatS = 0.0;               // Numerical/scheduling IAT floor.
  int64_t m_iatStream = -1;             // Optional fixed RNG stream from scenario YAML.
  Ptr<LogNormalRandomVariable> m_iatRv; // Active-burst IAT random variable.
  TracedCallback<Time, uint32_t> m_txTrace; // Emits (time, payload bytes).
  TracedCallback<double> m_iatTrace;    // Emits drawn IAT values.
};

} // namespace ns3
