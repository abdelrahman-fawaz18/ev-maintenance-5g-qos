/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/**
 * @file maintenance-nr-single-run.cc
 * @brief Execute one configured 5G NR EV-maintenance policy experiment.
 *
 * Inputs: scenario and traffic YAML files plus command-line seed/output overrides.
 * Outputs: packet trace, flow summary, delay histogram, FlowMonitor XML, run metadata,
 * and a run manifest in the selected output directory.
 */

#include "../helpers/maintenance-nr-output.h"
#include "../helpers/maintenance-nr-scenario-config.h"
#include "../helpers/maintenance-traffic-installer.h"

#include "ns3/antenna-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ideal-beamforming-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/point-to-point-module.h"

#include <cstdint>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace ns3;

namespace {

struct AppTxCounter
{
  uint64_t packets = 0;
  uint64_t bytes = 0;
};

std::map<std::string, AppTxCounter> g_appCounters;
std::ofstream g_appTrace;
double g_appTraceTimeOriginS = 0.0;

struct QosRuntimeInfo
{
  int qfi = 1;
  uint8_t rulePrecedence = 255;
  double latencyDeadlineMs = 0.0;
  double pdrTarget = 0.0;
  std::string fiveQiName = "DEFAULT_QOS_FLOW";
};

struct FlowMetricRow
{
  uint32_t flowId = 0;
  std::string trafficClass;
  std::string srcAddr;
  uint16_t srcPort = 0;
  std::string dstAddr;
  uint16_t dstPort = 0;
  uint64_t txPackets = 0;
  uint64_t rxPackets = 0;
  uint64_t lostPackets = 0;
  uint64_t txBytes = 0;
  uint64_t rxBytes = 0;
  uint64_t deadlineViolations = 0;
  double delaySumMs = 0.0;
  double delayHistSumMs = 0.0;
  double delayHistSumSqMs = 0.0;
  uint64_t delayHistSamples = 0;
  double meanDelayMs = 0.0;
  double meanDelayCiLowerMs = 0.0;
  double meanDelayCiUpperMs = 0.0;
  double delayP05Ms = 0.0;
  double delayP95Ms = 0.0;
  double deadlinePenalizedMeanMs = 0.0;
  double throughputMbps = 0.0;
};

void
WriteAppTx (const std::string& trafficClass, Time txTime, uint32_t payloadB)
{
  auto& counter = g_appCounters[trafficClass];
  ++counter.packets;
  counter.bytes += payloadB;
  if (g_appTrace.is_open ())
    {
      const double traceTimeS = std::max (0.0, txTime.GetSeconds () - g_appTraceTimeOriginS);
      g_appTrace << std::fixed << std::setprecision (9)
                 << traceTimeS << ","
                 << trafficClass << ","
                 << payloadB << "\n";
    }
}

void OnDiagTx (Time t, uint32_t b) { WriteAppTx ("DIAG", t, b); }
void OnAlertTx (Time t, uint32_t b) { WriteAppTx ("ALERT", t, b); }
void OnFaultTx (Time t, uint32_t b) { WriteAppTx ("FAULT", t, b); }

Ptr<NrQosRule>
CreateUplinkPortQosRule (uint16_t remotePort, uint8_t precedence)
{
  Ptr<NrQosRule> rule = Create<NrQosRule> ();
  // The installed 5G-LENA v4.2 classifies UE-originated traffic with QoS rules.
  // For uplink UDP, the remote port is the server-side destination port, so
  // each maintenance application receives a separate QoS flow.
  NrQosRule::PacketFilter filter {};
  filter.direction = NrQosRule::UPLINK;
  filter.remotePortStart = remotePort;
  filter.remotePortEnd = remotePort;
  rule->SetPrecedence (precedence);
  rule->Add (filter);
  return rule;
}

NrQosFlow
CreateQosFlow (const evnr_maintenance::NrQosClassConfig& qos)
{
  const bool hasGbrRate = qos.gbrDlBps > 0 || qos.mbrDlBps > 0 || qos.gbrUlBps > 0 ||
                          qos.mbrUlBps > 0;
  NrQosFlow flow (qos.fiveQi);
  if (hasGbrRate)
    {
      NrGbrQosInformation gbr;
      gbr.gbrDl = qos.gbrDlBps;
      gbr.gbrUl = qos.gbrUlBps;
      gbr.mbrDl = qos.mbrDlBps;
      gbr.mbrUl = qos.mbrUlBps;
      flow = NrQosFlow (qos.fiveQi, gbr);
    }
  flow.arp.priorityLevel = qos.arpPriority;
  flow.arp.preemptionCapability = qos.preemptionCapability;
  flow.arp.preemptionVulnerability = qos.preemptionVulnerability;
  return flow;
}


double
HistogramQuantileMs (const Histogram& histogram, uint64_t samples, double quantile)
{
  if (samples == 0)
    {
      return 0.0;
    }
  const uint64_t target = std::max<uint64_t> (1, static_cast<uint64_t> (std::ceil (quantile * samples)));
  uint64_t cumulative = 0;
  for (uint32_t i = 0; i < histogram.GetNBins (); ++i)
    {
      cumulative += histogram.GetBinCount (i);
      if (cumulative >= target)
        {
          return 1000.0 * 0.5 * (histogram.GetBinStart (i) + histogram.GetBinEnd (i));
        }
    }
  return 1000.0 * histogram.GetBinEnd (histogram.GetNBins () == 0 ? 0 : histogram.GetNBins () - 1);
}

uint64_t
CountDeadlineViolationsFromHistogram (const Histogram& histogram, double deadlineMs)
{
  uint64_t count = 0;
  const double deadlineS = deadlineMs / 1000.0;
  for (uint32_t i = 0; i < histogram.GetNBins (); ++i)
    {
      // A bin is counted when any part of it exceeds the configured deadline.
      // The FlowMonitor bin width is configured small enough for this to be a
      // conservative but useful deadline-miss estimate.
      if (histogram.GetBinEnd (i) > deadlineS)
        {
          count += histogram.GetBinCount (i);
        }
    }
  return count;
}

void
AccumulateHistogramMomentsMs (const Histogram& histogram,
                              double& sumMs,
                              double& sumSqMs,
                              uint64_t& samples)
{
  for (uint32_t i = 0; i < histogram.GetNBins (); ++i)
    {
      const uint64_t count = histogram.GetBinCount (i);
      if (count == 0)
        {
          continue;
        }
      const double midpointMs = 1000.0 * 0.5 * (histogram.GetBinStart (i) + histogram.GetBinEnd (i));
      sumMs += midpointMs * count;
      sumSqMs += midpointMs * midpointMs * count;
      samples += count;
    }
}

void
FinalizeDelayConfidenceInterval (FlowMetricRow& row)
{
  if (row.rxPackets == 0)
    {
      row.meanDelayCiLowerMs = 0.0;
      row.meanDelayCiUpperMs = 0.0;
      return;
    }
  if (row.delayHistSamples < 2)
    {
      row.meanDelayCiLowerMs = row.meanDelayMs;
      row.meanDelayCiUpperMs = row.meanDelayMs;
      return;
    }
  const double n = static_cast<double> (row.delayHistSamples);
  const double mean = row.delayHistSumMs / n;
  const double variance = std::max (0.0, (row.delayHistSumSqMs - n * mean * mean) / (n - 1.0));
  const double standardError = std::sqrt (variance / n);
  // Per-flow diagnostic interval. Paper-level uncertainty is computed across
  // independent seeds by the bootstrap analysis script.
  const double z95 = 1.959963984540054;
  row.meanDelayCiLowerMs = row.meanDelayMs - z95 * standardError;
  row.meanDelayCiUpperMs = row.meanDelayMs + z95 * standardError;
}

void
WriteFlowMetricCsvRow (std::ofstream& out,
                       const FlowMetricRow& row,
                       const QosRuntimeInfo& qos)
{
  const double pdr = row.txPackets == 0 ? 0.0 : static_cast<double> (row.rxPackets) / row.txPackets;
  const double deadlineViolationRate = row.txPackets == 0
                                           ? 0.0
                                           : static_cast<double> (row.deadlineViolations) / row.txPackets;
  out << row.flowId << ","
      << row.trafficClass << ","
      << qos.qfi << ","
      << qos.fiveQiName << ","
      << +qos.rulePrecedence << ","
      << qos.latencyDeadlineMs << ","
      << qos.pdrTarget << ","
      << row.srcAddr << ","
      << row.srcPort << ","
      << row.dstAddr << ","
      << row.dstPort << ","
      << row.txPackets << ","
      << row.rxPackets << ","
      << row.lostPackets << ","
      << row.txBytes << ","
      << row.rxBytes << ","
      << pdr << ","
      << row.deadlineViolations << ","
      << deadlineViolationRate << ","
      << row.meanDelayMs << ","
      << row.meanDelayCiLowerMs << ","
      << row.meanDelayCiUpperMs << ","
      << row.delayP05Ms << ","
      << row.delayP95Ms << ","
      << row.deadlinePenalizedMeanMs << ","
      << row.throughputMbps << "\n";
}

bool
IsEvTrafficClass (const std::string& trafficClass)
{
  return trafficClass == "DIAG" || trafficClass == "ALERT" || trafficClass == "FAULT";
}

std::string
ClassForPort (const evnr_maintenance::MaintenanceTrafficConfig& traffic,
              const evnr_maintenance::MaintenanceNrScenarioConfig& nr,
              uint16_t port)
{
  if (port == traffic.diag.udpPort)
    {
      return "DIAG";
    }
  if (port == traffic.alert.udpPort)
    {
      return "ALERT";
    }
  if (port == traffic.fault.udpPort)
    {
      return "FAULT";
    }
  if (nr.background.enabled && port == nr.background.udpPort)
    {
      return "BACKGROUND";
    }
  if (nr.evBackground.enabled && port == nr.evBackground.udpPort)
    {
      return "EV_BACKGROUND";
    }
  return "OTHER";
}

double
BackgroundPerUeRateMbps (const evnr_maintenance::NrBackgroundTrafficConfig& background)
{
  if (!background.enabled || background.ueCount == 0)
    {
      return 0.0;
    }
  if (background.offeredLoadMbps == 0.0)
    {
      return 0.0;
    }
  if (background.loadMode == "per_ue")
    {
      return background.offeredLoadMbps;
    }
  return background.offeredLoadMbps / static_cast<double> (background.ueCount);
}

void
WriteHistogramRows (std::ofstream& out,
                    uint32_t flowId,
                    const std::string& trafficClass,
                    const Histogram& histogram,
                    uint64_t samples)
{
  uint64_t cumulative = 0;
  for (uint32_t i = 0; i < histogram.GetNBins (); ++i)
    {
      const uint64_t count = histogram.GetBinCount (i);
      if (count == 0)
        {
          continue;
        }
      cumulative += count;
      const double cdf = samples == 0 ? 0.0 : static_cast<double> (cumulative) / samples;
      out << flowId << ","
          << trafficClass << ","
          << 1000.0 * histogram.GetBinStart (i) << ","
          << 1000.0 * histogram.GetBinEnd (i) << ","
          << count << ","
          << cumulative << ","
          << cdf << "\n";
    }
}

void
WriteRunMetadata (const std::string& path,
                  const evnr_maintenance::MaintenanceNrScenarioConfig& nr,
                  const evnr_maintenance::MaintenanceTrafficConfig& traffic,
                  const std::string& runDir,
                  const std::string& schedulerTypeName,
                  const std::map<std::string, QosRuntimeInfo>& qosRuntime)
{
  std::ofstream out (path.c_str ());
  NS_ABORT_MSG_IF (!out.is_open (), "Could not open run metadata file: " << path);
  out << "run_dir=" << runDir << "\n";
  out << "traffic_config=" << nr.run.trafficConfigPath << "\n";
  out << "scenario_mode=" << nr.scenario.mode << "\n";
  out << "scheduler=" << schedulerTypeName << "\n";
  out << "scheduler_access=" << nr.schedulerAccess << "\n";
  out << "scheduler_policy=" << nr.schedulerType << "\n";
  out << "seed=" << traffic.rngSeed << "\n";
  out << "run=" << traffic.rngRun << "\n";
  out << "duration_s=" << traffic.durationS << "\n";
  out << "app_start_offset_s=" << nr.run.appStartOffsetS << "\n";
  out << "traffic_rng_stream_base=" << nr.run.trafficRngStreamBase << "\n";
  out << "drain_s=" << nr.run.drainS << "\n";
  out << "background_enabled=" << nr.background.enabled << "\n";
  out << "background_ue_count=" << nr.background.ueCount << "\n";
  out << "background_load_mode=" << nr.background.loadMode << "\n";
  out << "background_offered_load_mbps=" << nr.background.offeredLoadMbps << "\n";
  out << "background_per_ue_load_mbps=" << BackgroundPerUeRateMbps (nr.background) << "\n";
  out << "background_packet_size_B=" << nr.background.packetSizeB << "\n";
  out << "background_udp_port=" << nr.background.udpPort << "\n";
  out << "background_start_s=" << nr.background.startS << "\n";
  out << "background_stop_s=" << nr.background.stopS << "\n";
  out << "ev_background_enabled=" << nr.evBackground.enabled << "\n";
  out << "ev_background_offered_load_mbps=" << nr.evBackground.offeredLoadMbps << "\n";
  out << "ev_background_packet_size_B=" << nr.evBackground.packetSizeB << "\n";
  out << "ev_background_udp_port=" << nr.evBackground.udpPort << "\n";
  out << "ev_background_start_s=" << nr.evBackground.startS << "\n";
  out << "ev_background_stop_s=" << nr.evBackground.stopS << "\n";
  out << "diag_port=" << traffic.diag.udpPort << "\n";
  out << "alert_port=" << traffic.alert.udpPort << "\n";
  out << "fault_port=" << traffic.fault.udpPort << "\n";
  out << "diag_dedicated_qos_flow_enabled=" << nr.qos.diag.dedicatedQosFlowEnabled << "\n";
  out << "alert_dedicated_qos_flow_enabled=" << nr.qos.alert.dedicatedQosFlowEnabled << "\n";
  out << "fault_dedicated_qos_flow_enabled=" << nr.qos.fault.dedicatedQosFlowEnabled << "\n";
  for (const auto& item : qosRuntime)
    {
      out << evnr_maintenance::ToLowerCopy (item.first) << "_qfi=" << item.second.qfi << "\n";
      out << evnr_maintenance::ToLowerCopy (item.first) << "_five_qi=" << item.second.fiveQiName << "\n";
      out << evnr_maintenance::ToLowerCopy (item.first) << "_rule_precedence=" << +item.second.rulePrecedence << "\n";
      out << evnr_maintenance::ToLowerCopy (item.first) << "_latency_deadline_ms=" << item.second.latencyDeadlineMs << "\n";
      out << evnr_maintenance::ToLowerCopy (item.first) << "_pdr_target=" << item.second.pdrTarget << "\n";
    }
}

void
WriteRunManifest (const std::string& path,
                  const evnr_maintenance::MaintenanceNrScenarioConfig& nr,
                  const evnr_maintenance::MaintenanceTrafficConfig& traffic,
                  const std::string& runDir,
                  const std::string& schedulerTypeName)
{
  std::ofstream out (path.c_str ());
  NS_ABORT_MSG_IF (!out.is_open (), "Could not open run manifest CSV: " << path);
  out << "key,value\n";
  auto write = [&] (const std::string& key, const std::string& value)
  {
    out << key << "," << value << "\n";
  };
  auto writeNumber = [&] (const std::string& key, double value)
  {
    out << key << "," << std::setprecision (12) << value << "\n";
  };

  write ("run_dir", runDir);
  write ("scenario_config", nr.run.name);
  write ("traffic_config_path", nr.run.trafficConfigPath);
  write ("scenario_mode", nr.scenario.mode);
  write ("scheduler", schedulerTypeName);
  write ("scheduler_access", nr.schedulerAccess);
  write ("scheduler_policy", nr.schedulerType);
  writeNumber ("seed", traffic.rngSeed);
  writeNumber ("run", traffic.rngRun);
  writeNumber ("duration_s", traffic.durationS);
  writeNumber ("app_start_offset_s", nr.run.appStartOffsetS);
  writeNumber ("drain_s", nr.run.drainS);
  write ("background_enabled", nr.background.enabled ? "true" : "false");
  writeNumber ("background_ue_count", nr.background.ueCount);
  write ("background_load_mode", nr.background.loadMode);
  writeNumber ("background_offered_load_mbps", nr.background.offeredLoadMbps);
  writeNumber ("background_per_ue_load_mbps", BackgroundPerUeRateMbps (nr.background));
  writeNumber ("background_packet_size_B", nr.background.packetSizeB);
  writeNumber ("background_udp_port", nr.background.udpPort);
  writeNumber ("background_start_s", nr.background.startS);
  writeNumber ("background_stop_s", nr.background.stopS);
  write ("ev_background_enabled", nr.evBackground.enabled ? "true" : "false");
  writeNumber ("ev_background_offered_load_mbps", nr.evBackground.offeredLoadMbps);
  writeNumber ("ev_background_packet_size_B", nr.evBackground.packetSizeB);
  writeNumber ("ev_background_udp_port", nr.evBackground.udpPort);
  writeNumber ("ev_background_start_s", nr.evBackground.startS);
  writeNumber ("ev_background_stop_s", nr.evBackground.stopS);
  write ("app_tx_trace_csv", nr.logging.appTxTraceCsv);
  write ("flow_summary_csv", nr.logging.flowSummaryCsv);
  write ("delay_histogram_csv", nr.logging.delayHistogramCsv);
  write ("flow_monitor_xml", nr.logging.flowMonitorXml);
}

std::string
AddressToString (Ipv4Address address)
{
  std::ostringstream os;
  os << address;
  return os.str ();
}

void
WriteFlowSummary (const std::string& path,
                  const std::string& delayHistogramPath,
                  Ptr<FlowMonitor> monitor,
                  FlowMonitorHelper& helper,
                  const evnr_maintenance::MaintenanceTrafficConfig& traffic,
                  const evnr_maintenance::MaintenanceNrScenarioConfig& nr,
                  const std::map<std::string, QosRuntimeInfo>& qosRuntime)
{
  std::ofstream out (path.c_str ());
  NS_ABORT_MSG_IF (!out.is_open (), "Could not open flow summary CSV: " << path);
  out << "flow_id,traffic_class,qfi,five_qi,rule_precedence,latency_deadline_ms,pdr_target,"
         "src_addr,src_port,dst_addr,dst_port,tx_packets,rx_packets,lost_packets,tx_bytes,"
         "rx_bytes,pdr,deadline_violations,deadline_violation_rate,mean_delay_ms,"
         "mean_delay_ci_lower_ms,mean_delay_ci_upper_ms,delay_p05_ms,delay_p95_ms,"
         "deadline_penalized_mean_ms,throughput_mbps\n";

  std::ofstream histogramOut (delayHistogramPath.c_str ());
  NS_ABORT_MSG_IF (!histogramOut.is_open (), "Could not open delay histogram CSV: " << delayHistogramPath);
  histogramOut << "flow_id,traffic_class,bin_start_ms,bin_end_ms,count,cumulative_count,cdf\n";

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (helper.GetClassifier ());
  const auto stats = monitor->GetFlowStats ();
  std::map<std::string, FlowMetricRow> totals;
  FlowMetricRow evTotal;
  evTotal.trafficClass = "TOTAL";
  evTotal.srcAddr = "-";
  evTotal.dstAddr = "-";

  for (const auto& item : stats)
    {
      const auto tuple = classifier->FindFlow (item.first);
      const auto& st = item.second;
      const std::string cls = ClassForPort (traffic, nr, tuple.destinationPort);
      if (cls == "OTHER")
        {
          continue;
        }
      const auto qosIt = qosRuntime.find (cls);
      NS_ABORT_MSG_IF (qosIt == qosRuntime.end (), "Missing QoS runtime info for " << cls);
      const auto& qos = qosIt->second;
      const double activeS =
          std::max (1e-12, st.timeLastRxPacket.GetSeconds () - st.timeFirstTxPacket.GetSeconds ());

      FlowMetricRow row;
      row.flowId = item.first;
      row.trafficClass = cls;
      row.srcAddr = AddressToString (tuple.sourceAddress);
      row.srcPort = tuple.sourcePort;
      row.dstAddr = AddressToString (tuple.destinationAddress);
      row.dstPort = tuple.destinationPort;
      row.txPackets = st.txPackets;
      row.rxPackets = st.rxPackets;
      // FlowMonitor may not classify every packet still queued at simulation
      // stop as lost. Use the complete accounting identity so reliability and
      // loss-penalized auxiliary metrics include every undelivered packet.
      row.lostPackets = st.txPackets >= st.rxPackets ? st.txPackets - st.rxPackets : st.lostPackets;
      row.txBytes = st.txBytes;
      row.rxBytes = st.rxBytes;
      row.delaySumMs = 1000.0 * st.delaySum.GetSeconds ();
      row.meanDelayMs = st.rxPackets == 0 ? 0.0 : row.delaySumMs / st.rxPackets;
      row.delayP05Ms = HistogramQuantileMs (st.delayHistogram, st.rxPackets, 0.05);
      row.delayP95Ms = HistogramQuantileMs (st.delayHistogram, st.rxPackets, 0.95);
      row.deadlineViolations = qos.latencyDeadlineMs <= 0.0
                                   ? 0
                                   : CountDeadlineViolationsFromHistogram (st.delayHistogram, qos.latencyDeadlineMs) + st.lostPackets;
      row.deadlinePenalizedMeanMs =
          st.txPackets == 0 ? 0.0 : (row.delaySumMs + st.lostPackets * qos.latencyDeadlineMs) / st.txPackets;
      row.throughputMbps = (st.rxBytes * 8.0) / activeS / 1e6;
      AccumulateHistogramMomentsMs (st.delayHistogram,
                                    row.delayHistSumMs,
                                    row.delayHistSumSqMs,
                                    row.delayHistSamples);
      FinalizeDelayConfidenceInterval (row);
      WriteFlowMetricCsvRow (out, row, qos);
      WriteHistogramRows (histogramOut, row.flowId, row.trafficClass, st.delayHistogram, st.rxPackets);

      auto& total = totals[cls];
      if (total.trafficClass.empty ())
        {
          total.trafficClass = cls + "_TOTAL";
          total.srcAddr = "-";
          total.dstAddr = "-";
        }
      total.txPackets += row.txPackets;
      total.rxPackets += row.rxPackets;
      total.lostPackets += row.lostPackets;
      total.txBytes += row.txBytes;
      total.rxBytes += row.rxBytes;
      total.deadlineViolations += row.deadlineViolations;
      total.delaySumMs += row.delaySumMs;
      total.delayHistSumMs += row.delayHistSumMs;
      total.delayHistSumSqMs += row.delayHistSumSqMs;
      total.delayHistSamples += row.delayHistSamples;
      total.deadlinePenalizedMeanMs += row.delaySumMs + row.lostPackets * qos.latencyDeadlineMs;
      total.delayP05Ms += row.delayP05Ms * row.rxPackets;
      total.delayP95Ms += row.delayP95Ms * row.rxPackets;
      total.throughputMbps += row.throughputMbps;

      if (IsEvTrafficClass (cls))
        {
          evTotal.txPackets += row.txPackets;
          evTotal.rxPackets += row.rxPackets;
          evTotal.lostPackets += row.lostPackets;
          evTotal.txBytes += row.txBytes;
          evTotal.rxBytes += row.rxBytes;
          evTotal.deadlineViolations += row.deadlineViolations;
          evTotal.delaySumMs += row.delaySumMs;
          evTotal.delayHistSumMs += row.delayHistSumMs;
          evTotal.delayHistSumSqMs += row.delayHistSumSqMs;
          evTotal.delayHistSamples += row.delayHistSamples;
          evTotal.deadlinePenalizedMeanMs += row.delaySumMs + row.lostPackets * qos.latencyDeadlineMs;
          evTotal.delayP05Ms += row.delayP05Ms * row.rxPackets;
          evTotal.delayP95Ms += row.delayP95Ms * row.rxPackets;
          evTotal.throughputMbps += row.throughputMbps;
        }
    }

  for (auto& item : totals)
    {
      auto& total = item.second;
      const auto& qos = qosRuntime.at (item.first);
      total.meanDelayMs = total.rxPackets == 0 ? 0.0 : total.delaySumMs / total.rxPackets;
      total.deadlinePenalizedMeanMs =
          total.txPackets == 0 ? 0.0 : total.deadlinePenalizedMeanMs / total.txPackets;
      total.delayP05Ms = total.rxPackets == 0 ? 0.0 : total.delayP05Ms / total.rxPackets;
      total.delayP95Ms = total.rxPackets == 0 ? 0.0 : total.delayP95Ms / total.rxPackets;
      FinalizeDelayConfidenceInterval (total);
      WriteFlowMetricCsvRow (out, total, qos);
    }

  QosRuntimeInfo totalQos;
  totalQos.qfi = -1;
  totalQos.fiveQiName = "AGGREGATE";
  totalQos.rulePrecedence = 0;
  totalQos.latencyDeadlineMs = 0.0;
  totalQos.pdrTarget = 0.0;
  evTotal.meanDelayMs = evTotal.rxPackets == 0 ? 0.0 : evTotal.delaySumMs / evTotal.rxPackets;
  evTotal.deadlinePenalizedMeanMs =
      evTotal.txPackets == 0 ? 0.0 : evTotal.deadlinePenalizedMeanMs / evTotal.txPackets;
  evTotal.delayP05Ms = evTotal.rxPackets == 0 ? 0.0 : evTotal.delayP05Ms / evTotal.rxPackets;
  evTotal.delayP95Ms = evTotal.rxPackets == 0 ? 0.0 : evTotal.delayP95Ms / evTotal.rxPackets;
  FinalizeDelayConfidenceInterval (evTotal);
  WriteFlowMetricCsvRow (out, evTotal, totalQos);
}

} // namespace

int
main (int argc, char* argv[])
{
  std::string nrCfgPath = "ev-maintenance-5g-qos/config/paper-baseline.yaml";

  CommandLine cmd (__FILE__);
  cmd.AddValue ("nrCfg", "Path to the NR scenario YAML.", nrCfgPath);
  cmd.Parse (argc, argv);

  const auto nrConfig = evnr_maintenance::LoadMaintenanceNrScenarioConfig (nrCfgPath);
  const auto trafficConfig =
      evnr_maintenance::LoadMaintenanceTrafficConfig (nrConfig.run.trafficConfigPath);

  RngSeedManager::SetSeed (trafficConfig.rngSeed);
  RngSeedManager::SetRun (trafficConfig.rngRun);
  g_appTraceTimeOriginS = nrConfig.run.appStartOffsetS;

  const std::string runDir =
      evnr_maintenance::CreateUniqueRunDirectory (nrConfig.run.outputRoot,
                                                  nrConfig.run.name,
                                                  trafficConfig.rngSeed,
                                                  trafficConfig.rngRun);
  const std::string appTracePath =
      evnr_maintenance::JoinPath (runDir, nrConfig.logging.appTxTraceCsv);
  g_appTrace.open (appTracePath.c_str ());
  NS_ABORT_MSG_IF (!g_appTrace.is_open (), "Could not open app trace CSV: " << appTracePath);
  g_appTrace << "time_s,traffic_class,payload_B\n";

  const std::string schedulerTypeName =
      evnr_maintenance::SchedulerTypeIdName (nrConfig.schedulerAccess, nrConfig.schedulerType);

  std::cout << "[RUN] output_dir=" << runDir << "\n";
  std::cout << "[RUN] traffic_config=" << nrConfig.run.trafficConfigPath << "\n";
  std::cout << "[RUN] scenario=" << nrConfig.scenario.mode
            << " scheduler=" << schedulerTypeName << "\n";
  std::cout << "[RUN] background_enabled=" << nrConfig.background.enabled
            << " bg_ues=" << nrConfig.background.ueCount
            << " offered_load_mbps=" << nrConfig.background.offeredLoadMbps << "\n";
  std::cout << "[RUN] ev_background_enabled=" << nrConfig.evBackground.enabled
            << " offered_load_mbps=" << nrConfig.evBackground.offeredLoadMbps << "\n";

  NS_ABORT_MSG_IF (nrConfig.background.enabled &&
                       (nrConfig.background.udpPort == trafficConfig.diag.udpPort ||
                        nrConfig.background.udpPort == trafficConfig.alert.udpPort ||
                        nrConfig.background.udpPort == trafficConfig.fault.udpPort),
                   "background.udp_port must not match a maintenance application UDP port.");
  NS_ABORT_MSG_IF (nrConfig.evBackground.enabled &&
                       (nrConfig.evBackground.udpPort == trafficConfig.diag.udpPort ||
                        nrConfig.evBackground.udpPort == trafficConfig.alert.udpPort ||
                        nrConfig.evBackground.udpPort == trafficConfig.fault.udpPort ||
                        nrConfig.evBackground.udpPort == nrConfig.background.udpPort),
                   "ev_background.udp_port must not match another application UDP port.");

  Config::SetDefault ("ns3::NrRlcUm::MaxTxBufferSize", UintegerValue (5 * 1024 * 1024));
  Config::SetDefault ("ns3::NrRlcUm::EnablePdcpDiscarding", BooleanValue (false));
  Config::SetDefault ("ns3::ThreeGppPropagationLossModel::ShadowingEnabled",
                      BooleanValue (nrConfig.radio.shadowingEnabled));
  Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod",
                      TimeValue (MilliSeconds (nrConfig.radio.channelUpdatePeriodMs)));
  Config::SetDefault ("ns3::ThreeGppChannelConditionModel::UpdatePeriod",
                      TimeValue (MilliSeconds (nrConfig.radio.channelConditionUpdatePeriodMs)));
  if (nrConfig.radio.alwaysLos)
    {
      Ptr<AlwaysLosChannelConditionModel> condition = CreateObject<AlwaysLosChannelConditionModel> ();
      Config::SetDefault ("ns3::ThreeGppPropagationLossModel::ChannelConditionModel",
                          PointerValue (condition));
    }

  NodeContainer gnbNodes;
  gnbNodes.Create (1);
  NodeContainer ueNodes;
  const uint32_t totalUeCount = 1 + (nrConfig.background.enabled ? nrConfig.background.ueCount : 0);
  ueNodes.Create (totalUeCount);

  MobilityHelper gnbMobility;
  gnbMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  gnbMobility.Install (gnbNodes);
  gnbNodes.Get (0)->GetObject<MobilityModel> ()->SetPosition (
      Vector (nrConfig.mobility.gnbX, nrConfig.mobility.gnbY, nrConfig.mobility.gnbZ));

  MobilityHelper ueMobility;
  if (nrConfig.mobility.ueSpeedMps == 0.0)
    {
      ueMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
      ueMobility.Install (ueNodes);
      ueNodes.Get (0)->GetObject<MobilityModel> ()->SetPosition (
          Vector (nrConfig.mobility.ueX, nrConfig.mobility.ueY, nrConfig.mobility.ueZ));
      for (uint32_t i = 1; i < totalUeCount; ++i)
        {
          const double bgIndex = static_cast<double> (i - 1);
          ueNodes.Get (i)->GetObject<MobilityModel> ()->SetPosition (
              Vector (nrConfig.background.xStartM + bgIndex * nrConfig.background.xSpacingM,
                      nrConfig.background.yStartM + bgIndex * nrConfig.background.ySpacingM,
                      nrConfig.background.zM));
        }
    }
  else
    {
      ueMobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
      ueMobility.Install (ueNodes);
      Ptr<ConstantVelocityMobilityModel> model =
          ueNodes.Get (0)->GetObject<ConstantVelocityMobilityModel> ();
      model->SetPosition (
          Vector (nrConfig.mobility.ueX, nrConfig.mobility.ueY, nrConfig.mobility.ueZ));
      model->SetVelocity (Vector (nrConfig.mobility.ueSpeedMps, 0.0, 0.0));
      for (uint32_t i = 1; i < totalUeCount; ++i)
        {
          const double bgIndex = static_cast<double> (i - 1);
          Ptr<ConstantVelocityMobilityModel> bgModel =
              ueNodes.Get (i)->GetObject<ConstantVelocityMobilityModel> ();
          bgModel->SetPosition (
              Vector (nrConfig.background.xStartM + bgIndex * nrConfig.background.xSpacingM,
                      nrConfig.background.yStartM + bgIndex * nrConfig.background.ySpacingM,
                      nrConfig.background.zM));
          bgModel->SetVelocity (Vector (0.0, 0.0, 0.0));
        }
    }

  Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper> ();
  Ptr<IdealBeamformingHelper> beamforming = CreateObject<IdealBeamformingHelper> ();
  Ptr<NrHelper> nrHelper = CreateObject<NrHelper> ();
  nrHelper->SetEpcHelper (epcHelper);
  nrHelper->SetBeamformingHelper (beamforming);
  nrHelper->SetSchedulerTypeId (TypeId::LookupByName (schedulerTypeName));
  nrHelper->SetSchedulerAttribute ("EnableHarqReTx", BooleanValue (nrConfig.radio.harqEnabled));

  nrHelper->SetGnbDlAmcAttribute ("AmcModel", EnumValue (NrAmc::ErrorModel));
  nrHelper->SetGnbDlAmcAttribute ("ErrorModelType",
                                  TypeIdValue (TypeId::LookupByName ("ns3::NrEesmIrT1")));
  nrHelper->SetGnbUlAmcAttribute ("AmcModel", EnumValue (NrAmc::ErrorModel));
  nrHelper->SetGnbUlAmcAttribute ("ErrorModelType",
                                  TypeIdValue (TypeId::LookupByName ("ns3::NrEesmIrT1")));

  nrHelper->SetGnbAntennaAttribute ("NumRows",
                                    UintegerValue (nrConfig.radio.gnbAntennaRows));
  nrHelper->SetGnbAntennaAttribute ("NumColumns",
                                    UintegerValue (nrConfig.radio.gnbAntennaColumns));
  nrHelper->SetGnbAntennaAttribute ("AntennaElement",
                                    PointerValue (CreateObject<IsotropicAntennaModel> ()));
  nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (nrConfig.radio.ueAntennaRows));
  nrHelper->SetUeAntennaAttribute ("NumColumns",
                                   UintegerValue (nrConfig.radio.ueAntennaColumns));
  nrHelper->SetUeAntennaAttribute ("AntennaElement",
                                   PointerValue (CreateObject<IsotropicAntennaModel> ()));
  nrHelper->SetGnbPhyAttribute ("TxPower", DoubleValue (nrConfig.radio.gnbTxPowerDbm));
  nrHelper->SetUePhyAttribute ("TxPower", DoubleValue (nrConfig.radio.ueTxPowerDbm));

  CcBwpCreator ccBwpCreator;
  CcBwpCreator::SimpleOperationBandConf bandConf (nrConfig.radio.centerFrequencyHz,
                                                  nrConfig.radio.bandwidthHz,
                                                  nrConfig.radio.bwpCount);
  OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc (bandConf);
  Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper> ();
  channelHelper->ConfigureFactories (nrConfig.radio.scenario, "Default", "ThreeGpp");
  channelHelper->SetPathlossAttribute ("ShadowingEnabled",
                                       BooleanValue (nrConfig.radio.shadowingEnabled));
  channelHelper->SetChannelConditionModelAttribute (
      "UpdatePeriod", TimeValue (MilliSeconds (nrConfig.radio.channelConditionUpdatePeriodMs)));
  channelHelper->AssignChannelsToBands ({band});
  BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps ({band});

  NetDeviceContainer gnbDevices = nrHelper->InstallGnbDevice (gnbNodes, allBwps);
  NetDeviceContainer ueDevices = nrHelper->InstallUeDevice (ueNodes, allBwps);

  int64_t stream = 1;
  stream += nrHelper->AssignStreams (gnbDevices, stream);
  stream += nrHelper->AssignStreams (ueDevices, stream);

  for (uint32_t bwp = 0; bwp < allBwps.size (); ++bwp)
    {
      NrHelper::GetGnbPhy (gnbDevices.Get (0), bwp)
          ->SetAttribute ("Numerology", UintegerValue (nrConfig.radio.numerology));
      NrHelper::GetGnbPhy (gnbDevices.Get (0), bwp)
          ->SetAttribute ("Pattern", StringValue (nrConfig.radio.tddPattern));
    }
  auto [remoteHost, remoteHostAddress] =
      epcHelper->SetupRemoteHost (nrConfig.core.remoteHostLinkDataRate,
                                  nrConfig.core.remoteHostLinkMtu,
                                  Seconds (nrConfig.core.remoteHostLinkDelayS));

  InternetStackHelper internet;
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIfaces = epcHelper->AssignUeIpv4Address (ueDevices);
  Ipv4StaticRoutingHelper routingHelper;
  for (uint32_t i = 0; i < totalUeCount; ++i)
    {
      Ptr<Ipv4StaticRouting> ueStaticRouting =
          routingHelper.GetStaticRouting (ueNodes.Get (i)->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

  nrHelper->AttachToClosestGnb (ueDevices, gnbDevices);

  std::map<std::string, QosRuntimeInfo> qosRuntime;
  // These fields describe effective runtime treatment. Until a dedicated flow
  // is activated, each class uses the default QoS flow (QFI 1).
  qosRuntime["DIAG"] = {1, nrConfig.qos.diag.rulePrecedence, nrConfig.qos.diag.latencyDeadlineMs,
                         nrConfig.qos.diag.pdrTarget, "DEFAULT_QOS_FLOW"};
  qosRuntime["ALERT"] = {1, nrConfig.qos.alert.rulePrecedence, nrConfig.qos.alert.latencyDeadlineMs,
                          nrConfig.qos.alert.pdrTarget, "DEFAULT_QOS_FLOW"};
  qosRuntime["FAULT"] = {1, nrConfig.qos.fault.rulePrecedence, nrConfig.qos.fault.latencyDeadlineMs,
                          nrConfig.qos.fault.pdrTarget, "DEFAULT_QOS_FLOW"};
  qosRuntime["BACKGROUND"] = {1, 255, 0.0, 0.0, "DEFAULT_QOS_FLOW"};
  qosRuntime["EV_BACKGROUND"] = {1, 255, 0.0, 0.0, "DEFAULT_QOS_FLOW"};

  const bool useDedicatedQosFlows =
      nrConfig.scenario.mode == "slice" && nrConfig.scenario.activateDedicatedQosFlows;
  if (useDedicatedQosFlows)
    {
      auto activateQosFlow = [&] (const std::string& name,
                                 const evnr_maintenance::NrQosClassConfig& qos,
                                 uint16_t port)
      {
        if (!qos.dedicatedQosFlowEnabled)
          {
            std::cout << "[QOS] " << name << " uses default QoS flow\n";
            return;
          }
        const uint8_t qfi = nrHelper->ActivateDedicatedQosFlow (ueDevices, CreateQosFlow (qos), CreateUplinkPortQosRule (port, qos.rulePrecedence));
        qosRuntime[name].qfi = qfi;
        qosRuntime[name].fiveQiName = evnr_maintenance::FiveQiName (qos.fiveQi);
        std::cout << "[QOS] " << name << " dedicated QoS flow qfi=" << +qfi
                  << " five_qi=" << evnr_maintenance::FiveQiName (qos.fiveQi)
                  << " precedence=" << +qos.rulePrecedence << " port=" << port << "\n";
      };
      activateQosFlow ("DIAG", nrConfig.qos.diag, trafficConfig.diag.udpPort);
      activateQosFlow ("ALERT", nrConfig.qos.alert, trafficConfig.alert.udpPort);
      activateQosFlow ("FAULT", nrConfig.qos.fault, trafficConfig.fault.udpPort);
    }
  else
    {
      std::cout << "[QOS] using default QoS flow only\n";
    }

  evnr_maintenance::MaintenanceTrafficInstaller installer (trafficConfig);
  installer.SetClientNode (ueNodes.Get (0));
  installer.SetServerNode (remoteHost);
  installer.SetRemoteAddress (remoteHostAddress);
  installer.SetTimeOffsetS (nrConfig.run.appStartOffsetS);
  installer.SetRandomStreamBase (nrConfig.run.trafficRngStreamBase);
  auto installed = installer.Install ();
  if (installed.diagApp)
    {
      installed.diagApp->TraceConnectWithoutContext ("Tx", MakeCallback (&OnDiagTx));
    }
  if (installed.alertApp)
    {
      installed.alertApp->TraceConnectWithoutContext ("Tx", MakeCallback (&OnAlertTx));
    }
  if (installed.faultApp)
    {
      installed.faultApp->TraceConnectWithoutContext ("Tx", MakeCallback (&OnFaultTx));
    }

  ApplicationContainer backgroundApps;
  ApplicationContainer evBackgroundApps;
  if (nrConfig.evBackground.enabled)
    {
      UdpServerHelper evBackgroundServer (nrConfig.evBackground.udpPort);
      ApplicationContainer serverApps = evBackgroundServer.Install (remoteHost);
      serverApps.Start (Seconds (0.0));
      serverApps.Stop (Seconds (nrConfig.run.appStartOffsetS + nrConfig.evBackground.stopS + nrConfig.run.drainS));
      evBackgroundApps.Add (serverApps);

      const double activeS = nrConfig.evBackground.stopS - nrConfig.evBackground.startS;
      const double rateBps = nrConfig.evBackground.offeredLoadMbps * 1e6;
      const double intervalS = (nrConfig.evBackground.packetSizeB * 8.0) / rateBps;
      const uint32_t maxPackets = static_cast<uint32_t> (std::ceil (activeS / intervalS)) + 1;
      UdpClientHelper client (remoteHostAddress, nrConfig.evBackground.udpPort);
      client.SetAttribute ("MaxPackets", UintegerValue (maxPackets));
      client.SetAttribute ("Interval", TimeValue (Seconds (intervalS)));
      client.SetAttribute ("PacketSize", UintegerValue (nrConfig.evBackground.packetSizeB));
      ApplicationContainer apps = client.Install (ueNodes.Get (0));
      apps.Start (Seconds (nrConfig.run.appStartOffsetS + nrConfig.evBackground.startS));
      apps.Stop (Seconds (nrConfig.run.appStartOffsetS + nrConfig.evBackground.stopS));
      evBackgroundApps.Add (apps);
      std::cout << "[EV_BG] installed best-effort EV flow at "
                << nrConfig.evBackground.offeredLoadMbps
                << " Mbps, packet_size_B=" << nrConfig.evBackground.packetSizeB
                << " port=" << nrConfig.evBackground.udpPort << "\n";
    }

  if (nrConfig.background.enabled)
    {
      const double perUeRateMbps = BackgroundPerUeRateMbps (nrConfig.background);
      if (perUeRateMbps > 0.0)
        {
          UdpServerHelper backgroundServer (nrConfig.background.udpPort);
          ApplicationContainer serverApps = backgroundServer.Install (remoteHost);
          serverApps.Start (Seconds (0.0));
          serverApps.Stop (Seconds (nrConfig.run.appStartOffsetS + nrConfig.background.stopS + nrConfig.run.drainS));
          backgroundApps.Add (serverApps);

          const double perUeRateBps = perUeRateMbps * 1e6;
          const double intervalS = (nrConfig.background.packetSizeB * 8.0) / perUeRateBps;
          const double activeS = nrConfig.background.stopS - nrConfig.background.startS;
          const uint32_t maxPackets = static_cast<uint32_t> (std::ceil (activeS / intervalS)) + 1;
          for (uint32_t i = 1; i < totalUeCount; ++i)
            {
              UdpClientHelper client (remoteHostAddress, nrConfig.background.udpPort);
              client.SetAttribute ("MaxPackets", UintegerValue (maxPackets));
              client.SetAttribute ("Interval", TimeValue (Seconds (intervalS)));
              client.SetAttribute ("PacketSize", UintegerValue (nrConfig.background.packetSizeB));
              ApplicationContainer apps = client.Install (ueNodes.Get (i));
              apps.Start (Seconds (nrConfig.run.appStartOffsetS + nrConfig.background.startS));
              apps.Stop (Seconds (nrConfig.run.appStartOffsetS + nrConfig.background.stopS));
              backgroundApps.Add (apps);
            }
        }
      std::cout << "[BG] installed " << nrConfig.background.ueCount
                << " background UEs at " << perUeRateMbps
                << " Mbps per UE, packet_size_B=" << nrConfig.background.packetSizeB
                << " port=" << nrConfig.background.udpPort << "\n";
    }

  FlowMonitorHelper flowHelper;
  flowHelper.SetMonitorAttribute ("DelayBinWidth",
                                  DoubleValue (nrConfig.flowMonitor.delayBinWidthS));
  flowHelper.SetMonitorAttribute ("JitterBinWidth",
                                  DoubleValue (nrConfig.flowMonitor.jitterBinWidthS));
  flowHelper.SetMonitorAttribute ("PacketSizeBinWidth",
                                  DoubleValue (nrConfig.flowMonitor.packetSizeBinWidthB));
  Ptr<FlowMonitor> monitor = flowHelper.InstallAll ();

  if (nrConfig.run.enableNrTraces)
    {
      nrHelper->EnableTraces ();
    }

  WriteRunMetadata (evnr_maintenance::JoinPath (runDir, nrConfig.logging.runMetadataTxt),
                    nrConfig,
                    trafficConfig,
                    runDir,
                    schedulerTypeName,
                    qosRuntime);
  WriteRunManifest (evnr_maintenance::JoinPath (runDir, nrConfig.logging.runManifestCsv),
                    nrConfig,
                    trafficConfig,
                    runDir,
                    schedulerTypeName);

  Simulator::Stop (Seconds (nrConfig.run.appStartOffsetS + trafficConfig.durationS +
                            nrConfig.run.drainS));
  Simulator::Run ();

  if (g_appTrace.is_open ())
    {
      g_appTrace.close ();
    }

  WriteFlowSummary (evnr_maintenance::JoinPath (runDir, nrConfig.logging.flowSummaryCsv),
                    evnr_maintenance::JoinPath (runDir, nrConfig.logging.delayHistogramCsv),
                    monitor,
                    flowHelper,
                    trafficConfig,
                    nrConfig,
                    qosRuntime);
  monitor->SerializeToXmlFile (
      evnr_maintenance::JoinPath (runDir, nrConfig.logging.flowMonitorXml), true, true);

  std::cout << "[APP] DIAG packets=" << g_appCounters["DIAG"].packets
            << " bytes=" << g_appCounters["DIAG"].bytes << "\n";
  std::cout << "[APP] ALERT packets=" << g_appCounters["ALERT"].packets
            << " bytes=" << g_appCounters["ALERT"].bytes << "\n";
  std::cout << "[APP] FAULT packets=" << g_appCounters["FAULT"].packets
            << " bytes=" << g_appCounters["FAULT"].bytes << "\n";
  std::cout << "[DONE] outputs written to " << runDir << "\n";

  Simulator::Destroy ();
  return 0;
}
