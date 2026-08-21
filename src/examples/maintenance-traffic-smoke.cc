/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/**
 * @file maintenance-traffic-smoke.cc
 * @brief Validate the three custom traffic generators over a lightweight IP link.
 *
 * Inputs: a traffic YAML file and optional output-directory command-line arguments.
 * Outputs: transmitted-packet traces and class-level counters used for fast validation.
 */

#include "../helpers/maintenance-traffic-installer.h"

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using namespace ns3;

namespace {

// Simple counters used only by the smoke test. The production NR scenario can
// connect to the same Tx traces and write richer KPI files.
uint64_t g_diagTxPackets = 0;
uint64_t g_diagTxBytes = 0;
uint64_t g_alertTxPackets = 0;
uint64_t g_alertTxBytes = 0;
uint64_t g_faultTxPackets = 0;
uint64_t g_faultTxBytes = 0;
std::ofstream g_packetTrace;

void
WritePacketTraceRow (const char* trafficClass, Time txTime, uint32_t sizeB)
{
  if (g_packetTrace.is_open ())
    {
      g_packetTrace << std::fixed << std::setprecision (9)
                    << txTime.GetSeconds () << ","
                    << trafficClass << ","
                    << sizeB << "\n";
    }
}

void
OnDiagTx (Time txTime, uint32_t sizeB)
{
  // Trace callback connected to DiagTrafficApp::Tx.
  ++g_diagTxPackets;
  g_diagTxBytes += sizeB;
  WritePacketTraceRow ("DIAG", txTime, sizeB);
}

void
OnAlertTx (Time txTime, uint32_t sizeB)
{
  // Trace callback connected to AlertTrafficApp::Tx.
  ++g_alertTxPackets;
  g_alertTxBytes += sizeB;
  WritePacketTraceRow ("ALERT", txTime, sizeB);
}

void
OnFaultTx (Time txTime, uint32_t sizeB)
{
  // Trace callback connected to FaultTrafficApp::Tx.
  ++g_faultTxPackets;
  g_faultTxBytes += sizeB;
  WritePacketTraceRow ("FAULT", txTime, sizeB);
}

} // namespace

int
main (int argc, char* argv[])
{
  // The default path assumes this example is run from the ns-3 root after the
  // package folder has been copied or symlinked there.
  std::string cfgPath = "ns3_traffic_generation_rewrite/config/evnr-maintenance-traffic.yaml";

  CommandLine cmd;
  cmd.AddValue ("cfg", "Path to maintenance traffic YAML.", cfgPath);
  cmd.Parse (argc, argv);

  // Parse and validate YAML before creating any topology or scheduling events.
  const auto config = evnr_maintenance::LoadMaintenanceTrafficConfig (cfgPath);
  RngSeedManager::SetSeed (config.rngSeed);
  RngSeedManager::SetRun (config.rngRun);

  if (config.txTraceEnable)
    {
      g_packetTrace.open (config.packetTraceCsv.c_str ());
      NS_ABORT_MSG_IF (!g_packetTrace.is_open (),
                       "Could not open packet trace CSV: " << config.packetTraceCsv);
      g_packetTrace << "time_s,traffic_class,payload_B\n";
    }

  // The smoke topology is intentionally not a 5G-LENA topology. It only checks
  // that the three applications generate the expected UDP packet processes.
  NodeContainer nodes;
  nodes.Create (2);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue (config.smokeP2pDataRate));
  p2p.SetChannelAttribute ("Delay", StringValue (config.smokeP2pDelay));
  NetDeviceContainer devices = p2p.Install (nodes);

  // Install a minimal IP stack so UDP sockets can send packets.
  InternetStackHelper internet;
  internet.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase (config.smokeNetworkBase.c_str (), config.smokeNetworkMask.c_str ());
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  // The helper installs one UDP server per enabled class on node 1 and the
  // traffic generators on node 0.
  evnr_maintenance::MaintenanceTrafficInstaller installer (config);
  installer.SetClientNode (nodes.Get (0));
  installer.SetServerNode (nodes.Get (1));
  installer.SetRemoteAddress (interfaces.GetAddress (1));
  auto installed = installer.Install ();

  if ((config.txTraceEnable || config.printSummary) && installed.diagApp)
    {
      installed.diagApp->TraceConnectWithoutContext ("Tx", MakeCallback (&OnDiagTx));
    }
  if ((config.txTraceEnable || config.printSummary) && installed.alertApp)
    {
      installed.alertApp->TraceConnectWithoutContext ("Tx", MakeCallback (&OnAlertTx));
    }
  if ((config.txTraceEnable || config.printSummary) && installed.faultApp)
    {
      installed.faultApp->TraceConnectWithoutContext ("Tx", MakeCallback (&OnFaultTx));
    }

  // Run one extra second so packets sent very near the end can drain through
  // the point-to-point link and UDP servers before Simulator::Destroy().
  Simulator::Stop (Seconds (config.durationS + 1.0));
  Simulator::Run ();
  Simulator::Destroy ();

  if (g_packetTrace.is_open ())
    {
      g_packetTrace.close ();
    }

  if (config.printSummary)
    {
      std::cout << "DIAG tx_packets=" << g_diagTxPackets
                << " tx_bytes=" << g_diagTxBytes << std::endl;
      std::cout << "ALERT tx_packets=" << g_alertTxPackets
                << " tx_bytes=" << g_alertTxBytes << std::endl;
      std::cout << "FAULT tx_packets=" << g_faultTxPackets
                << " tx_bytes=" << g_faultTxBytes << std::endl;

      std::cout << "Derived median-load estimates:" << std::endl;
      std::cout << "  DIAG bps="
                << evnr_maintenance::OfferedBitrateBpsFromMedianIat (config.diag)
                << std::endl;
      std::cout << "  ALERT bps="
                << evnr_maintenance::OfferedBitrateBpsFromMedianIat (config.alert)
                << std::endl;
      std::cout << "  FAULT bps="
                << evnr_maintenance::OfferedBitrateBpsFromMedianIat (config.fault)
                << std::endl;
      if (config.txTraceEnable)
        {
          std::cout << "Packet trace CSV=" << config.packetTraceCsv << std::endl;
        }
    }

  return 0;
}
