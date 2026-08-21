/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/**
 * @file maintenance-traffic-installer.h
 * @brief Install DIAG, ALERT, and FAULT clients and UDP sinks on ns-3 nodes.
 *
 * Inputs: typed traffic configuration, client/server nodes, destination address,
 * timing offset, and RNG stream base.
 * Outputs: application containers and typed handles for trace connections.
 */
#pragma once

#include "../apps/alert-traffic-app.h"
#include "../apps/diag-traffic-app.h"
#include "../apps/fault-traffic-app.h"
#include "maintenance-yaml-config.h"

#include "ns3/application-container.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/ipv4-address.h"
#include "ns3/node.h"
#include "ns3/ptr.h"

#include <cstdint>

namespace evnr_maintenance {

// Returned handles let callers connect traces after installation. The same
// installer can be used by the point-to-point smoke test and later by the
// full 5G-LENA scenario.
struct MaintenanceTrafficInstallResult
{
  ns3::ApplicationContainer serverApps;
  ns3::ApplicationContainer clientApps;
  ns3::Ptr<ns3::DiagTrafficApp> diagApp;
  ns3::Ptr<ns3::AlertTrafficApp> alertApp;
  ns3::Ptr<ns3::FaultTrafficApp> faultApp;
};

class MaintenanceTrafficInstaller
{
public:
  explicit MaintenanceTrafficInstaller (const MaintenanceTrafficConfig& config)
    : m_config (config)
  {
  }

  void SetClientNode (ns3::Ptr<ns3::Node> node)
  {
    m_clientNode = node;
  }

  void SetServerNode (ns3::Ptr<ns3::Node> node)
  {
    m_serverNode = node;
  }

  void SetRemoteAddress (const ns3::Ipv4Address& address)
  {
    m_remoteAddress = address;
  }

  void SetTimeOffsetS (double offsetS)
  {
    NS_ABORT_MSG_IF (offsetS < 0.0, "MaintenanceTrafficInstaller time offset cannot be negative.");
    m_timeOffsetS = offsetS;
  }

  void SetRandomStreamBase (int64_t streamBase)
  {
    m_randomStreamBase = streamBase;
  }

  MaintenanceTrafficInstallResult Install () const
  {
    // Fail early if the caller forgot to provide the topology endpoints.
    NS_ABORT_MSG_IF (!m_clientNode, "MaintenanceTrafficInstaller client node is not set.");
    NS_ABORT_MSG_IF (!m_serverNode, "MaintenanceTrafficInstaller server node is not set.");
    NS_ABORT_MSG_IF (m_remoteAddress == ns3::Ipv4Address (), "MaintenanceTrafficInstaller remote address is not set.");

    MaintenanceTrafficInstallResult result;
    InstallServers (result);
    InstallClients (result);
    return result;
  }

private:
  void InstallServers (MaintenanceTrafficInstallResult& result) const
  {
    // UdpServerHelper creates one receiver per enabled class. In the full NR
    // scenario, these same ports can be used for TFT/QoS/slice classification.
    if (m_config.diag.enabled)
      {
        ns3::UdpServerHelper server (m_config.diag.udpPort);
        ns3::ApplicationContainer apps = server.Install (m_serverNode);
        apps.Start (ns3::Seconds (0.0));
        apps.Stop (ns3::Seconds (m_timeOffsetS + m_config.durationS + 1.0));
        result.serverApps.Add (apps);
      }
    if (m_config.alert.enabled)
      {
        ns3::UdpServerHelper server (m_config.alert.udpPort);
        ns3::ApplicationContainer apps = server.Install (m_serverNode);
        apps.Start (ns3::Seconds (0.0));
        apps.Stop (ns3::Seconds (m_timeOffsetS + m_config.durationS + 1.0));
        result.serverApps.Add (apps);
      }
    if (m_config.fault.enabled)
      {
        ns3::UdpServerHelper server (m_config.fault.udpPort);
        ns3::ApplicationContainer apps = server.Install (m_serverNode);
        apps.Start (ns3::Seconds (0.0));
        apps.Stop (ns3::Seconds (m_timeOffsetS + m_config.durationS + 1.0));
        result.serverApps.Add (apps);
      }
  }

  void InstallClients (MaintenanceTrafficInstallResult& result) const
  {
    // Each app is created with neutral constructor defaults, then every
    // scientific traffic parameter is overridden from the validated YAML config.
    if (m_config.diag.enabled)
      {
        ns3::Ptr<ns3::DiagTrafficApp> app = ns3::CreateObject<ns3::DiagTrafficApp> ();
        app->SetAttribute ("RemoteAddress", ns3::Ipv4AddressValue (m_remoteAddress));
        app->SetAttribute ("RemotePort", ns3::UintegerValue (m_config.diag.udpPort));
        app->SetAttribute ("StartTimeS", ns3::DoubleValue (m_timeOffsetS + m_config.diag.startS));
        app->SetAttribute ("StopTimeS", ns3::DoubleValue (m_timeOffsetS + m_config.diag.stopS));
        app->SetAttribute ("PayloadBytes", ns3::UintegerValue (m_config.diag.payloadB));
        app->SetAttribute ("PeriodS", ns3::DoubleValue (m_config.diag.iat.periodS));
        m_clientNode->AddApplication (app);
        app->SetStartTime (ns3::Seconds (m_timeOffsetS + m_config.diag.startS));
        app->SetStopTime (ns3::Seconds (m_timeOffsetS + m_config.diag.stopS));
        result.clientApps.Add (app);
        result.diagApp = app;
      }

    if (m_config.alert.enabled)
      {
        ns3::Ptr<ns3::AlertTrafficApp> app = ns3::CreateObject<ns3::AlertTrafficApp> ();
        app->SetAttribute ("RemoteAddress", ns3::Ipv4AddressValue (m_remoteAddress));
        app->SetAttribute ("RemotePort", ns3::UintegerValue (m_config.alert.udpPort));
        app->SetAttribute ("StartTimeS", ns3::DoubleValue (m_timeOffsetS + m_config.alert.startS));
        app->SetAttribute ("StopTimeS", ns3::DoubleValue (m_timeOffsetS + m_config.alert.stopS));
        app->SetAttribute ("PayloadBytes", ns3::UintegerValue (m_config.alert.payloadB));
        if (m_config.alert.iat.model == "fixed")
          {
            app->SetAttribute ("UseFixedIat", ns3::BooleanValue (true));
            app->SetAttribute ("FixedIatS", ns3::DoubleValue (m_config.alert.iat.periodS));
          }
        else
          {
            app->SetAttribute ("UseFixedIat", ns3::BooleanValue (false));
            app->SetAttribute ("IatLognormalMu", ns3::DoubleValue (m_config.alert.iat.mu));
            app->SetAttribute ("IatLognormalSigma", ns3::DoubleValue (m_config.alert.iat.sigma));
            app->SetAttribute ("MinIatS", ns3::DoubleValue (m_config.alert.iat.minIatS));
            if (m_randomStreamBase >= 0)
              {
                // Random ALERT generation, when used, is isolated from radio/channel streams.
                app->SetIatStream (m_randomStreamBase);
              }
          }
        m_clientNode->AddApplication (app);
        app->SetStartTime (ns3::Seconds (m_timeOffsetS + m_config.alert.startS));
        app->SetStopTime (ns3::Seconds (m_timeOffsetS + m_config.alert.stopS));
        result.clientApps.Add (app);
        result.alertApp = app;
      }

    if (m_config.fault.enabled)
      {
        ns3::Ptr<ns3::FaultTrafficApp> app = ns3::CreateObject<ns3::FaultTrafficApp> ();
        app->SetAttribute ("RemoteAddress", ns3::Ipv4AddressValue (m_remoteAddress));
        app->SetAttribute ("RemotePort", ns3::UintegerValue (m_config.fault.udpPort));
        app->SetAttribute ("StartTimeS", ns3::DoubleValue (m_timeOffsetS + m_config.fault.startS));
        app->SetAttribute ("StopTimeS", ns3::DoubleValue (m_timeOffsetS + m_config.fault.stopS));
        app->SetAttribute ("PayloadBytes", ns3::UintegerValue (m_config.fault.payloadB));
        if (m_config.fault.iat.model == "fixed")
          {
            app->SetAttribute ("UseFixedIat", ns3::BooleanValue (true));
            app->SetAttribute ("PeriodS", ns3::DoubleValue (m_config.fault.iat.periodS));
          }
        else
          {
            app->SetAttribute ("UseFixedIat", ns3::BooleanValue (false));
            app->SetAttribute ("IatLognormalMu", ns3::DoubleValue (m_config.fault.iat.mu));
            app->SetAttribute ("IatLognormalSigma", ns3::DoubleValue (m_config.fault.iat.sigma));
            app->SetAttribute ("MinIatS", ns3::DoubleValue (m_config.fault.iat.minIatS));
            if (m_randomStreamBase >= 0)
              {
                // Use a distinct stream from ALERT so stochastic flows are reproducible but independent.
                app->SetIatStream (m_randomStreamBase + 1);
              }
          }
        app->SetAttribute ("PacketsPerFullRefresh", ns3::UintegerValue (m_config.fault.packetsPerFullRefresh));
        m_clientNode->AddApplication (app);
        app->SetStartTime (ns3::Seconds (m_timeOffsetS + m_config.fault.startS));
        app->SetStopTime (ns3::Seconds (m_timeOffsetS + m_config.fault.stopS));
        result.clientApps.Add (app);
        result.faultApp = app;
      }
  }

  MaintenanceTrafficConfig m_config;
  ns3::Ptr<ns3::Node> m_clientNode;
  ns3::Ptr<ns3::Node> m_serverNode;
  ns3::Ipv4Address m_remoteAddress;
  double m_timeOffsetS = 0.0;
  int64_t m_randomStreamBase = -1;
};

} // namespace evnr_maintenance
