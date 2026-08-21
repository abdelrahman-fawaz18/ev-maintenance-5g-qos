/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/**
 * @file diag-traffic-app.h
 * @brief Periodic UDP generator for routine EV diagnostic telemetry.
 *
 * Inputs: remote address/port, packet size, period, active window, and RNG stream.
 * Outputs: UDP packets plus a Tx trace carrying timestamp and payload size.
 */
#pragma once

#include "ns3/application.h"
#include "ns3/callback.h"
#include "ns3/fatal-error.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/ipv4-address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/udp-socket-factory.h"

#include <algorithm>
#include <cstdint>

namespace ns3 {

// DIAG traffic represents continuous routine EV maintenance reports.
// This implementation is intentionally simple: fixed-size UDP application
// payloads are sent at a fixed period configured from YAML.
class DiagTrafficApp : public Application
{
public:
  static TypeId GetTypeId ()
  {
    static TypeId tid =
      TypeId ("ns3::DiagTrafficApp")
        .SetParent<Application> ()
        .SetGroupName ("Applications")
        .AddConstructor<DiagTrafficApp> ()
        .AddAttribute ("RemoteAddress",
                       "Remote IPv4 address.",
                       Ipv4AddressValue (),
                       MakeIpv4AddressAccessor (&DiagTrafficApp::m_remoteAddress),
                       MakeIpv4AddressChecker ())
        .AddAttribute ("RemotePort",
                       "Remote UDP port.",
                       UintegerValue (0),
                       MakeUintegerAccessor (&DiagTrafficApp::m_remotePort),
                       MakeUintegerChecker<uint16_t> ())
        .AddAttribute ("StartTimeS",
                       "Configured application start time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&DiagTrafficApp::m_startS),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("StopTimeS",
                       "Configured application stop time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&DiagTrafficApp::m_stopS),
                       MakeDoubleChecker<double> ())
        .AddAttribute ("PayloadBytes",
                       "Application payload bytes per DIAG packet.",
                       UintegerValue (0),
                       MakeUintegerAccessor (&DiagTrafficApp::m_payloadBytes),
                       MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("PeriodS",
                       "Fixed DIAG inter-arrival time in seconds.",
                       DoubleValue (0.0),
                       MakeDoubleAccessor (&DiagTrafficApp::m_periodS),
                       MakeDoubleChecker<double> ())
        .AddTraceSource ("Tx",
                         "Per-packet transmit trace: time, payload bytes.",
                         MakeTraceSourceAccessor (&DiagTrafficApp::m_txTrace),
                         "ns3::TracedCallback::TimeUint32");
    return tid;
  }

  DiagTrafficApp () = default;
  ~DiagTrafficApp () override = default;

private:
  void StartApplication () override
  {
    // TypeId defaults are neutral. The installer must set the real parameters
    // from YAML before ns-3 calls StartApplication().
    ValidateConfigured ();

    if (!m_socket)
      {
        // The app generates UDP application traffic. UDP/IP and lower layers
        // add their own headers below this application packet.
        m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
        m_socket->Connect (InetSocketAddress (m_remoteAddress, m_remotePort));
      }

    // Schedule the first packet at the configured start time. If ns-3 starts
    // the app exactly at start_s, this delay is zero.
    const double nowS = Simulator::Now ().GetSeconds ();
    const double delayS = std::max (0.0, m_startS - nowS);
    m_sendEvent = Simulator::Schedule (Seconds (delayS), &DiagTrafficApp::SendOne, this);
  }

  void StopApplication () override
  {
    // Cancel any future send and close the UDP socket when the app stops.
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
    // Guard against edge cases where the scheduler calls us just outside the
    // intended active window.
    const double nowS = Simulator::Now ().GetSeconds ();
    if (nowS < m_startS || nowS >= m_stopS)
      {
        return;
      }

    // Create<Packet>(N) means N bytes of application payload.
    Ptr<Packet> packet = Create<Packet> (m_payloadBytes);
    m_socket->Send (packet);
    m_txTrace (Simulator::Now (), m_payloadBytes);

    // Fixed-IAT DIAG reschedules itself until the configured stop time.
    const double nextS = nowS + m_periodS;
    if (nextS < m_stopS - 1e-12)
      {
        m_sendEvent = Simulator::Schedule (Seconds (m_periodS), &DiagTrafficApp::SendOne, this);
      }
  }

  void ValidateConfigured () const
  {
    // These checks catch missing YAML wiring before the first packet is sent.
    NS_ABORT_MSG_IF (m_remotePort == 0, "DiagTrafficApp RemotePort was not configured from YAML.");
    NS_ABORT_MSG_IF (m_payloadBytes == 0, "DiagTrafficApp PayloadBytes was not configured from YAML.");
    NS_ABORT_MSG_IF (m_periodS <= 0.0, "DiagTrafficApp PeriodS must be positive.");
    NS_ABORT_MSG_IF (m_stopS <= m_startS, "DiagTrafficApp StopTimeS must be greater than StartTimeS.");
  }

  Ptr<Socket> m_socket;                 // UDP socket used by this app.
  EventId m_sendEvent;                  // Next scheduled self-send event.
  Ipv4Address m_remoteAddress;          // Receiver address.
  uint16_t m_remotePort = 0;            // Receiver UDP port.
  double m_startS = 0.0;                // Active-window start from YAML.
  double m_stopS = 0.0;                 // Active-window stop from YAML.
  uint32_t m_payloadBytes = 0;          // Application payload bytes per packet.
  double m_periodS = 0.0;               // Fixed DIAG IAT in seconds.
  TracedCallback<Time, uint32_t> m_txTrace; // Emits (time, payload bytes).
};

} // namespace ns3
