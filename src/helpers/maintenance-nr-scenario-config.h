/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/**
 * @file maintenance-nr-scenario-config.h
 * @brief Parse and validate the radio, scheduler, topology, and QoS scenario contract.
 *
 * Inputs: a scenario YAML path and optional command-line overrides.
 * Outputs: a typed MaintenanceNrScenarioConfig or a validation exception.
 */
#pragma once

#include "maintenance-yaml-config.h"

#include "ns3/nr-qos-flow.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace evnr_maintenance {

struct NrRunConfig
{
  std::string name;
  std::string outputRoot;
  std::string trafficConfigPath;
  double appStartOffsetS = 0.0;
  int64_t trafficRngStreamBase = -1;
  double drainS = 0.0;
  bool enableNrTraces = false;
};

struct NrScenarioModeConfig
{
  std::string mode;
  bool activateDedicatedQosFlows = true;
};

struct NrCoreConfig
{
  std::string remoteHostLinkDataRate;
  uint16_t remoteHostLinkMtu = 2500;
  double remoteHostLinkDelayS = 0.0;
};

struct NrRadioConfig
{
  std::string scenario;
  double centerFrequencyHz = 0.0;
  double bandwidthHz = 0.0;
  uint32_t bwpCount = 1;
  uint32_t numerology = 1;
  std::string tddPattern;
  double gnbTxPowerDbm = 0.0;
  double ueTxPowerDbm = 0.0;
  uint32_t gnbAntennaRows = 1;
  uint32_t gnbAntennaColumns = 1;
  uint32_t ueAntennaRows = 1;
  uint32_t ueAntennaColumns = 1;
  bool shadowingEnabled = true;
  double channelUpdatePeriodMs = 20.0;
  double channelConditionUpdatePeriodMs = 0.0;
  bool alwaysLos = true;
  bool harqEnabled = true;
};

struct NrMobilityConfig
{
  double gnbX = 0.0;
  double gnbY = 0.0;
  double gnbZ = 10.0;
  double ueX = 50.0;
  double ueY = 0.0;
  double ueZ = 1.5;
  double ueSpeedMps = 0.0;
};

struct NrBackgroundTrafficConfig
{
  bool enabled = false;
  uint32_t ueCount = 0;
  std::string loadMode = "aggregate";
  double offeredLoadMbps = 0.0;
  uint32_t packetSizeB = 1200;
  uint16_t udpPort = 4999;
  double startS = 0.0;
  double stopS = 0.0;
  double xStartM = 60.0;
  double yStartM = 10.0;
  double zM = 1.5;
  double xSpacingM = 5.0;
  double ySpacingM = 0.0;
};

struct NrEvBackgroundTrafficConfig
{
  bool enabled = false;
  double offeredLoadMbps = 0.0;
  uint32_t packetSizeB = 1200;
  uint16_t udpPort = 5000;
  double startS = 0.0;
  double stopS = 0.0;
};

struct NrQosClassConfig
{
  bool dedicatedQosFlowEnabled = true;
  ns3::NrQosFlow::FiveQi fiveQi = ns3::NrQosFlow::NGBR_VIDEO_TCP_DEFAULT;
  std::string fiveQiName;
  uint8_t arpPriority = 15;
  uint8_t rulePrecedence = 255;
  double latencyDeadlineMs = 0.0;
  double pdrTarget = 0.0;
  bool preemptionCapability = false;
  bool preemptionVulnerability = true;
  double gbrUlBps = 0.0;
  double mbrUlBps = 0.0;
  double gbrDlBps = 0.0;
  double mbrDlBps = 0.0;
};

struct NrQosConfig
{
  NrQosClassConfig diag;
  NrQosClassConfig alert;
  NrQosClassConfig fault;
};

struct NrFlowMonitorConfig
{
  double delayBinWidthS = 0.0005;
  double jitterBinWidthS = 0.0005;
  double packetSizeBinWidthB = 20.0;
};

struct NrLoggingConfig
{
  std::string appTxTraceCsv;
  std::string flowSummaryCsv;
  std::string delayHistogramCsv;
  std::string runMetadataTxt;
  std::string runManifestCsv;
  std::string flowMonitorXml;
};

struct MaintenanceNrScenarioConfig
{
  NrRunConfig run;
  NrScenarioModeConfig scenario;
  std::string schedulerAccess;
  std::string schedulerType;
  NrCoreConfig core;
  NrRadioConfig radio;
  NrMobilityConfig mobility;
  NrBackgroundTrafficConfig background;
  NrEvBackgroundTrafficConfig evBackground;
  NrQosConfig qos;
  NrFlowMonitorConfig flowMonitor;
  NrLoggingConfig logging;
};

inline std::string ToLowerCopy (std::string value)
{
  std::transform (value.begin (), value.end (), value.begin (),
                  [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
  return value;
}

inline ns3::NrQosFlow::FiveQi ParseFiveQiOrThrow (const std::string& value)
{
  const std::string key = ToLowerCopy (Trim (value));
  if (key == "9" || key == "ngbr_video_tcp_default")
    {
      return ns3::NrQosFlow::NGBR_VIDEO_TCP_DEFAULT;
    }
  if (key == "3" || key == "gbr_gaming")
    {
      return ns3::NrQosFlow::GBR_GAMING;
    }
  if (key == "80" || key == "ngbr_low_lat_embb")
    {
      return ns3::NrQosFlow::NGBR_LOW_LAT_EMBB;
    }
  if (key == "83" || key == "dgbr_discrete_aut_large")
    {
      return ns3::NrQosFlow::DGBR_DISCRETE_AUT_LARGE;
    }
  if (key == "85" || key == "dgbr_electricity")
    {
      return ns3::NrQosFlow::DGBR_ELECTRICITY;
    }
  if (key == "86" || key == "dgbr_v2x")
    {
      return ns3::NrQosFlow::DGBR_V2X;
    }
  throw std::runtime_error ("Unsupported QoS 5QI value: " + value);
}

inline std::string FiveQiName (ns3::NrQosFlow::FiveQi fiveQi)
{
  switch (fiveQi)
    {
    case ns3::NrQosFlow::NGBR_VIDEO_TCP_DEFAULT:
      return "NGBR_VIDEO_TCP_DEFAULT";
    case ns3::NrQosFlow::GBR_GAMING:
      return "GBR_GAMING";
    case ns3::NrQosFlow::NGBR_LOW_LAT_EMBB:
      return "NGBR_LOW_LAT_EMBB";
    case ns3::NrQosFlow::DGBR_DISCRETE_AUT_LARGE:
      return "DGBR_DISCRETE_AUT_LARGE";
    case ns3::NrQosFlow::DGBR_ELECTRICITY:
      return "DGBR_ELECTRICITY";
    case ns3::NrQosFlow::DGBR_V2X:
      return "DGBR_V2X";
    default:
      return "FIVE_QI_" + std::to_string (static_cast<uint32_t> (fiveQi));
    }
}

inline NrQosClassConfig LoadQosClassConfig (const FlatYaml& yaml, const std::string& prefix)
{
  NrQosClassConfig config;
  config.dedicatedQosFlowEnabled = RequireBool (yaml, prefix + ".dedicated_qos_flow_enabled");
  config.fiveQiName = RequireString (yaml, prefix + ".five_qi");
  config.fiveQi = ParseFiveQiOrThrow (config.fiveQiName);
  config.arpPriority = static_cast<uint8_t> (RequireUint32 (yaml, prefix + ".arp_priority"));
  if (config.arpPriority == 0 || config.arpPriority > 15)
    {
      throw std::runtime_error (prefix + ".arp_priority must be in [1, 15].");
    }
  config.rulePrecedence = static_cast<uint8_t> (RequireUint32 (yaml, prefix + ".rule_precedence"));
  if (config.rulePrecedence == 0)
    {
      throw std::runtime_error (prefix + ".rule_precedence must be in [1, 255].");
    }
  config.latencyDeadlineMs = RequireDouble (yaml, prefix + ".latency_deadline_ms");
  if (config.latencyDeadlineMs <= 0.0)
    {
      throw std::runtime_error (prefix + ".latency_deadline_ms must be positive.");
    }
  config.pdrTarget = RequireDouble (yaml, prefix + ".pdr_target");
  if (config.pdrTarget < 0.0 || config.pdrTarget > 1.0)
    {
      throw std::runtime_error (prefix + ".pdr_target must be in [0, 1].");
    }
  config.preemptionCapability = RequireBool (yaml, prefix + ".preemption_capability");
  config.preemptionVulnerability = RequireBool (yaml, prefix + ".preemption_vulnerability");
  config.gbrUlBps = RequireDouble (yaml, prefix + ".gbr_ul_bps");
  config.mbrUlBps = RequireDouble (yaml, prefix + ".mbr_ul_bps");
  config.gbrDlBps = RequireDouble (yaml, prefix + ".gbr_dl_bps");
  config.mbrDlBps = RequireDouble (yaml, prefix + ".mbr_dl_bps");
  return config;
}

inline MaintenanceNrScenarioConfig LoadMaintenanceNrScenarioConfig (const std::string& path)
{
  const FlatYaml yaml = LoadFlatYamlOrThrow (path);

  MaintenanceNrScenarioConfig config;
  config.run.name = RequireString (yaml, "run.name");
  config.run.outputRoot = RequireString (yaml, "run.output_root");
  config.run.trafficConfigPath = RequireString (yaml, "run.traffic_config_path");
  config.run.appStartOffsetS = RequireDouble (yaml, "run.app_start_offset_s");
  config.run.trafficRngStreamBase =
      static_cast<int64_t> (RequireUint32 (yaml, "run.traffic_rng_stream_base"));
  config.run.drainS = RequireDouble (yaml, "run.drain_s");
  config.run.enableNrTraces = RequireBool (yaml, "run.enable_nr_traces");

  config.scenario.mode = ToLowerCopy (RequireString (yaml, "scenario.mode"));
  config.scenario.activateDedicatedQosFlows =
      RequireBool (yaml, "scenario.activate_dedicated_qos_flows");
  if (config.scenario.mode != "slice" && config.scenario.mode != "baseline")
    {
      throw std::runtime_error ("scenario.mode must be 'slice' or 'baseline'.");
    }

  config.schedulerAccess = ToLowerCopy (RequireString (yaml, "scheduler.access"));
  if (config.schedulerAccess != "tdma" && config.schedulerAccess != "ofdma")
    {
      throw std::runtime_error ("scheduler.access must be tdma or ofdma.");
    }
  config.schedulerType = ToLowerCopy (RequireString (yaml, "scheduler.type"));
  if (config.schedulerType != "qos" && config.schedulerType != "pf" &&
      config.schedulerType != "rr")
    {
      throw std::runtime_error ("scheduler.type must be qos, pf, or rr.");
    }

  config.core.remoteHostLinkDataRate =
      RequireString (yaml, "core.remote_host_link_data_rate");
  config.core.remoteHostLinkMtu =
      static_cast<uint16_t> (RequireUint32 (yaml, "core.remote_host_link_mtu"));
  config.core.remoteHostLinkDelayS =
      RequireDouble (yaml, "core.remote_host_link_delay_s");

  config.radio.scenario = RequireString (yaml, "radio.scenario");
  config.radio.centerFrequencyHz = RequireDouble (yaml, "radio.center_frequency_hz");
  config.radio.bandwidthHz = RequireDouble (yaml, "radio.bandwidth_hz");
  config.radio.bwpCount = RequireUint32 (yaml, "radio.bwp_count");
  config.radio.numerology = RequireUint32 (yaml, "radio.numerology");
  config.radio.tddPattern = RequireString (yaml, "radio.tdd_pattern");
  config.radio.gnbTxPowerDbm = RequireDouble (yaml, "radio.gnb_tx_power_dbm");
  config.radio.ueTxPowerDbm = RequireDouble (yaml, "radio.ue_tx_power_dbm");
  config.radio.gnbAntennaRows = RequireUint32 (yaml, "radio.gnb_antenna_rows");
  config.radio.gnbAntennaColumns = RequireUint32 (yaml, "radio.gnb_antenna_columns");
  config.radio.ueAntennaRows = RequireUint32 (yaml, "radio.ue_antenna_rows");
  config.radio.ueAntennaColumns = RequireUint32 (yaml, "radio.ue_antenna_columns");
  config.radio.shadowingEnabled = RequireBool (yaml, "radio.shadowing_enabled");
  config.radio.channelUpdatePeriodMs =
      RequireDouble (yaml, "radio.channel_update_period_ms");
  config.radio.channelConditionUpdatePeriodMs =
      RequireDouble (yaml, "radio.channel_condition_update_period_ms");
  config.radio.alwaysLos = RequireBool (yaml, "radio.always_los");
  config.radio.harqEnabled = RequireBool (yaml, "radio.harq_enabled");

  config.mobility.gnbX = RequireDouble (yaml, "mobility.gnb_x_m");
  config.mobility.gnbY = RequireDouble (yaml, "mobility.gnb_y_m");
  config.mobility.gnbZ = RequireDouble (yaml, "mobility.gnb_z_m");
  config.mobility.ueX = RequireDouble (yaml, "mobility.ue_x_m");
  config.mobility.ueY = RequireDouble (yaml, "mobility.ue_y_m");
  config.mobility.ueZ = RequireDouble (yaml, "mobility.ue_z_m");
  config.mobility.ueSpeedMps = RequireDouble (yaml, "mobility.ue_speed_mps");

  config.background.enabled = RequireBool (yaml, "background.enabled");
  config.background.ueCount = RequireUint32 (yaml, "background.ue_count");
  config.background.loadMode = ToLowerCopy (RequireString (yaml, "background.load_mode"));
  config.background.offeredLoadMbps = RequireDouble (yaml, "background.offered_load_mbps");
  config.background.packetSizeB = RequireUint32 (yaml, "background.packet_size_B");
  config.background.udpPort = static_cast<uint16_t> (RequireUint32 (yaml, "background.udp_port"));
  config.background.startS = RequireDouble (yaml, "background.start_s");
  config.background.stopS = RequireDouble (yaml, "background.stop_s");
  config.background.xStartM = RequireDouble (yaml, "background.x_start_m");
  config.background.yStartM = RequireDouble (yaml, "background.y_start_m");
  config.background.zM = RequireDouble (yaml, "background.z_m");
  config.background.xSpacingM = RequireDouble (yaml, "background.x_spacing_m");
  config.background.ySpacingM = RequireDouble (yaml, "background.y_spacing_m");

  config.evBackground.enabled = RequireBool (yaml, "ev_background.enabled");
  config.evBackground.offeredLoadMbps = RequireDouble (yaml, "ev_background.offered_load_mbps");
  config.evBackground.packetSizeB = RequireUint32 (yaml, "ev_background.packet_size_B");
  config.evBackground.udpPort = static_cast<uint16_t> (RequireUint32 (yaml, "ev_background.udp_port"));
  config.evBackground.startS = RequireDouble (yaml, "ev_background.start_s");
  config.evBackground.stopS = RequireDouble (yaml, "ev_background.stop_s");

  config.qos.diag = LoadQosClassConfig (yaml, "qos.diag");
  config.qos.alert = LoadQosClassConfig (yaml, "qos.alert");
  config.qos.fault = LoadQosClassConfig (yaml, "qos.fault");

  config.flowMonitor.delayBinWidthS =
      RequireDouble (yaml, "flow_monitor.delay_bin_width_s");
  config.flowMonitor.jitterBinWidthS =
      RequireDouble (yaml, "flow_monitor.jitter_bin_width_s");
  config.flowMonitor.packetSizeBinWidthB =
      RequireDouble (yaml, "flow_monitor.packet_size_bin_width_B");

  config.logging.appTxTraceCsv = RequireString (yaml, "logging.app_tx_trace_csv");
  config.logging.flowSummaryCsv = RequireString (yaml, "logging.flow_summary_csv");
  config.logging.delayHistogramCsv = RequireString (yaml, "logging.delay_histogram_csv");
  config.logging.runMetadataTxt = RequireString (yaml, "logging.run_metadata_txt");
  config.logging.runManifestCsv = RequireString (yaml, "logging.run_manifest_csv");
  config.logging.flowMonitorXml = RequireString (yaml, "logging.flow_monitor_xml");

  if (config.run.drainS < 0.0)
    {
      throw std::runtime_error ("run.drain_s cannot be negative.");
    }
  if (config.run.appStartOffsetS < 0.0)
    {
      throw std::runtime_error ("run.app_start_offset_s cannot be negative.");
    }
  if (config.radio.centerFrequencyHz <= 0.0 || config.radio.bandwidthHz <= 0.0)
    {
      throw std::runtime_error ("radio frequency and bandwidth must be positive.");
    }
  if (config.radio.bwpCount == 0)
    {
      throw std::runtime_error ("radio.bwp_count must be positive.");
    }
  if (config.background.loadMode != "aggregate" && config.background.loadMode != "per_ue")
    {
      throw std::runtime_error ("background.load_mode must be aggregate or per_ue.");
    }
  if (config.background.enabled)
    {
      if (config.background.ueCount == 0)
        {
          throw std::runtime_error ("background.ue_count must be positive when background.enabled is true.");
        }
      if (config.background.offeredLoadMbps < 0.0)
        {
          throw std::runtime_error ("background.offered_load_mbps cannot be negative when background is enabled.");
        }
      if (config.background.packetSizeB == 0)
        {
          throw std::runtime_error ("background.packet_size_B must be positive when background is enabled.");
        }
      if (config.background.stopS <= config.background.startS)
        {
          throw std::runtime_error ("background.stop_s must be greater than background.start_s.");
        }
    }
  if (config.evBackground.enabled)
    {
      if (config.evBackground.offeredLoadMbps <= 0.0)
        {
          throw std::runtime_error ("ev_background.offered_load_mbps must be positive when ev_background is enabled.");
        }
      if (config.evBackground.packetSizeB == 0)
        {
          throw std::runtime_error ("ev_background.packet_size_B must be positive when ev_background is enabled.");
        }
      if (config.evBackground.stopS <= config.evBackground.startS)
        {
          throw std::runtime_error ("ev_background.stop_s must be greater than ev_background.start_s.");
        }
    }
  return config;
}

inline std::string SchedulerTypeIdName (const std::string& schedulerAccess,
                                       const std::string& schedulerType)
{
  const std::string prefix = schedulerAccess == "ofdma" ? "ns3::NrMacSchedulerOfdma"
                                                        : "ns3::NrMacSchedulerTdma";
  if (schedulerType == "qos")
    {
      return prefix + "Qos";
    }
  if (schedulerType == "pf")
    {
      return prefix + "PF";
    }
  return prefix + "RR";
}

} // namespace evnr_maintenance
