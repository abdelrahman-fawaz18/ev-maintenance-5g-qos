/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/**
 * @file maintenance-yaml-config.h
 * @brief Parse and validate the dependency-free maintenance-traffic YAML subset.
 *
 * Inputs: a traffic YAML file with simulation, RNG, and class-specific parameters.
 * Outputs: a typed MaintenanceTrafficConfig or a descriptive validation exception.
 */
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace evnr_maintenance {

// The parser flattens nested YAML keys into dotted keys. For example:
//   alert:
//     iat:
//       mu: -7.0
// becomes:
//   alert.iat.mu -> -7.0
using FlatYaml = std::map<std::string, std::string>;

// IAT parameters for one stream. Only the fields required by the selected
// model are used: fixed uses periodS, lognormal uses mu/sigma/minIatS.
struct IatConfig
{
  std::string model;
  double periodS = 0.0;
  double mu = 0.0;
  double sigma = 0.0;
  double minIatS = 0.0;
};

// One DIAG, ALERT, or FAULT stream after YAML parsing and validation.
// This structure stores only application-layer traffic parameters. RAN QoS
// and slicing remain outside the generator and are handled by the full ns-3
// scenario using ports or TFT filters.
struct StreamConfig
{
  bool enabled = false;
  double startS = 0.0;
  double stopS = 0.0;
  uint16_t udpPort = 0;
  uint32_t payloadB = 0;
  uint32_t packetsPerFullRefresh = 1;
  IatConfig iat;
};

// Full smoke-test traffic configuration. The traffic fields are used by the
// apps; the smoke fields are only for the simple point-to-point example.
struct MaintenanceTrafficConfig
{
  double durationS = 0.0;
  uint32_t rngSeed = 1;
  uint32_t rngRun = 1;
  uint32_t maxUdpPayloadB = 0;
  bool txTraceEnable = true;
  bool printSummary = true;
  std::string packetTraceCsv;
  std::string smokeP2pDataRate;
  std::string smokeP2pDelay;
  std::string smokeNetworkBase;
  std::string smokeNetworkMask;
  double plotBinWidthS = 0.01;
  std::string paperStyleFigurePng;
  std::string loadSummaryFigurePng;
  StreamConfig diag;
  StreamConfig alert;
  StreamConfig fault;
};

// The trim helpers keep the YAML parser dependency-free. They are deliberately
// small because this parser supports only the subset of YAML used by the
// project config, not arbitrary YAML documents.
inline std::string LTrim (std::string s)
{
  s.erase (s.begin (),
           std::find_if (s.begin (), s.end (),
                         [] (unsigned char ch) { return !std::isspace (ch); }));
  return s;
}

inline std::string RTrim (std::string s)
{
  s.erase (std::find_if (s.rbegin (), s.rend (),
                         [] (unsigned char ch) { return !std::isspace (ch); }).base (),
           s.end ());
  return s;
}

inline std::string Trim (std::string s)
{
  return RTrim (LTrim (s));
}

inline std::string StripQuotes (std::string s)
{
  s = Trim (s);
  if (s.size () >= 2)
    {
      const char first = s.front ();
      const char last = s.back ();
      if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
        {
          return s.substr (1, s.size () - 2);
        }
    }
  return s;
}

inline std::string StripInlineComment (const std::string& s)
{
  // Remove comments while respecting quoted strings. This allows values such
  // as "folder#1" without treating the hash as a comment marker.
  bool inSingle = false;
  bool inDouble = false;
  for (std::size_t i = 0; i < s.size (); ++i)
    {
      if (s[i] == '\'' && !inDouble)
        {
          inSingle = !inSingle;
        }
      else if (s[i] == '"' && !inSingle)
        {
          inDouble = !inDouble;
        }
      else if (s[i] == '#' && !inSingle && !inDouble)
        {
          return s.substr (0, i);
        }
    }
  return s;
}

inline FlatYaml LoadFlatYamlOrThrow (const std::string& path)
{
  // Load a simple indentation-based YAML file and flatten it to dotted keys.
  // The function throws on missing files or malformed scalar lines so bad
  // configs fail before ns-3 starts scheduling traffic.
  std::ifstream in (path.c_str ());
  if (!in.is_open ())
    {
      throw std::runtime_error ("Could not open YAML config: " + path);
    }

  FlatYaml kv;
  std::vector<std::pair<int, std::string>> stack;
  std::string raw;

  while (std::getline (in, raw))
    {
      raw = StripInlineComment (raw);
      if (raw.find_first_not_of (" \t\r\n") == std::string::npos)
        {
          continue;
        }

      int indent = 0;
      while (indent < static_cast<int> (raw.size ()) &&
             (raw[indent] == ' ' || raw[indent] == '\t'))
        {
          ++indent;
        }

      std::string line = RTrim (raw.substr (indent));
      if (line.empty ())
        {
          continue;
        }

      if (line.back () == ':' && line.find (':') == line.size () - 1)
        {
          const std::string key = Trim (line.substr (0, line.size () - 1));
          while (!stack.empty () && stack.back ().first >= indent)
            {
              stack.pop_back ();
            }
          const std::string parent = stack.empty () ? std::string () : stack.back ().second;
          const std::string fullPath = parent.empty () ? key : parent + "." + key;
          stack.push_back (std::make_pair (indent, fullPath));
          continue;
        }

      const std::size_t pos = line.find (':');
      if (pos == std::string::npos)
        {
          throw std::runtime_error ("Malformed YAML line: " + line);
        }

      const std::string key = Trim (line.substr (0, pos));
      const std::string value = StripQuotes (Trim (line.substr (pos + 1)));
      while (!stack.empty () && stack.back ().first > indent)
        {
          stack.pop_back ();
        }

      const std::string parent = stack.empty () ? std::string () : stack.back ().second;
      const std::string fullKey = parent.empty () ? key : parent + "." + key;
      kv[fullKey] = value;
    }

  return kv;
}

inline std::string RequireString (const FlatYaml& yaml, const std::string& key)
{
  // Required accessors enforce the "YAML is the source of truth" rule.
  // We do not silently replace missing scientific parameters with defaults.
  const auto it = yaml.find (key);
  if (it == yaml.end () || Trim (it->second).empty ())
    {
      throw std::runtime_error ("Missing required YAML key: " + key);
    }
  return Trim (it->second);
}

inline double RequireDouble (const FlatYaml& yaml, const std::string& key)
{
  const std::string value = RequireString (yaml, key);
  try
    {
      std::size_t idx = 0;
      const double parsed = std::stod (value, &idx);
      if (idx != value.size ())
        {
          throw std::runtime_error ("extra characters");
        }
      return parsed;
    }
  catch (...)
    {
      throw std::runtime_error ("Invalid numeric YAML value for " + key + ": " + value);
    }
}

inline uint32_t RequireUint32 (const FlatYaml& yaml, const std::string& key)
{
  const double value = RequireDouble (yaml, key);
  if (value < 0.0 || value != static_cast<uint32_t> (value))
    {
      throw std::runtime_error ("YAML key must be a non-negative integer: " + key);
    }
  return static_cast<uint32_t> (value);
}

inline uint16_t RequireUint16 (const FlatYaml& yaml, const std::string& key)
{
  const uint32_t value = RequireUint32 (yaml, key);
  if (value == 0 || value > 65535)
    {
      throw std::runtime_error ("YAML UDP port must be in [1, 65535]: " + key);
    }
  return static_cast<uint16_t> (value);
}

inline bool RequireBool (const FlatYaml& yaml, const std::string& key)
{
  std::string value = RequireString (yaml, key);
  std::transform (value.begin (), value.end (), value.begin (),
                  [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
  if (value == "true" || value == "1" || value == "yes")
    {
      return true;
    }
  if (value == "false" || value == "0" || value == "no")
    {
      return false;
    }
  throw std::runtime_error ("Invalid boolean YAML value for " + key + ": " + value);
}

inline void RequireNoKeyOrPrefix (const FlatYaml& yaml, const std::string& key)
{
  // Old traffic files used target rates. This rewrite derives bitrate from
  // payload and IAT, so any old rate key is rejected, including nested forms.
  const std::string prefix = key + ".";
  for (const auto& item : yaml)
    {
      if (item.first == key || item.first.find (prefix) == 0)
        {
          throw std::runtime_error ("Remove unsupported YAML key " + item.first +
                                    "; bitrate must be derived from payload and IAT.");
        }
    }
}

inline void ValidateWindow (const std::string& prefix,
                            const StreamConfig& stream,
                            double durationS)
{
  // Disabled streams still parse, but active streams must fit inside the
  // simulation horizon.
  if (!stream.enabled)
    {
      return;
    }
  if (stream.startS < 0.0)
    {
      throw std::runtime_error (prefix + ".start_s cannot be negative.");
    }
  if (stream.stopS <= stream.startS)
    {
      throw std::runtime_error (prefix + ".stop_s must be greater than start_s.");
    }
  if (stream.stopS > durationS)
    {
      throw std::runtime_error (prefix + ".stop_s cannot exceed sim.duration_s.");
    }
}

inline void ValidatePayload (const std::string& prefix,
                             const StreamConfig& stream,
                             uint32_t maxPayloadB)
{
  // The payload is the application payload passed to Create<Packet>(N).
  // The max check prevents accidental IP fragmentation assumptions.
  if (!stream.enabled)
    {
      return;
    }
  if (stream.payloadB == 0)
    {
      throw std::runtime_error (prefix + ".payload_B must be positive.");
    }
  if (stream.payloadB > maxPayloadB)
    {
      throw std::runtime_error (prefix + ".payload_B exceeds validation.max_udp_payload_B.");
    }
}

inline void ValidateIat (const std::string& prefix, const StreamConfig& stream)
{
  // Only models implemented by the C++ apps are accepted. This prevents a YAML
  // typo from silently changing the traffic process.
  if (!stream.enabled)
    {
      return;
    }
  if (stream.iat.model == "fixed")
    {
      if (stream.iat.periodS <= 0.0)
        {
          throw std::runtime_error (prefix + ".iat.period_s must be positive for fixed IAT.");
        }
      return;
    }
  if (stream.iat.model == "lognormal")
    {
      if (stream.iat.sigma <= 0.0)
        {
          throw std::runtime_error (prefix + ".iat.sigma must be positive for lognormal IAT.");
        }
      if (stream.iat.minIatS < 0.0)
        {
          throw std::runtime_error (prefix + ".iat.min_iat_s cannot be negative.");
        }
      return;
    }
  throw std::runtime_error (prefix + ".iat.model must be fixed or lognormal.");
}

inline StreamConfig LoadStreamConfig (const FlatYaml& yaml,
                                      const std::string& prefix,
                                      bool requirePacketsPerFullRefresh)
{
  // Parse one class block. Required model-specific keys are requested only
  // after the IAT model is known.
  StreamConfig stream;
  stream.enabled = RequireBool (yaml, prefix + ".enabled");
  stream.startS = RequireDouble (yaml, prefix + ".start_s");
  stream.stopS = RequireDouble (yaml, prefix + ".stop_s");
  stream.udpPort = RequireUint16 (yaml, prefix + ".udp_port");
  stream.payloadB = RequireUint32 (yaml, prefix + ".payload_B");
  stream.iat.model = RequireString (yaml, prefix + ".iat.model");

  if (stream.iat.model == "fixed")
    {
      stream.iat.periodS = RequireDouble (yaml, prefix + ".iat.period_s");
    }
  else if (stream.iat.model == "lognormal")
    {
      stream.iat.mu = RequireDouble (yaml, prefix + ".iat.mu");
      stream.iat.sigma = RequireDouble (yaml, prefix + ".iat.sigma");
      stream.iat.minIatS = RequireDouble (yaml, prefix + ".iat.min_iat_s");
    }

  if (requirePacketsPerFullRefresh)
    {
      stream.packetsPerFullRefresh = RequireUint32 (yaml, prefix + ".packets_per_full_refresh");
      if (stream.packetsPerFullRefresh == 0)
        {
          throw std::runtime_error (prefix + ".packets_per_full_refresh must be positive.");
        }
    }

  return stream;
}

inline MaintenanceTrafficConfig LoadMaintenanceTrafficConfig (const std::string& path)
{
  // Top-level loader used by the smoke test and by the future NR scenario.
  // It centralizes validation so all simulations obey the same config rules.
  const FlatYaml yaml = LoadFlatYamlOrThrow (path);

  RequireNoKeyOrPrefix (yaml, "diag.target_rate_bps");
  RequireNoKeyOrPrefix (yaml, "alert.target_rate_bps");
  RequireNoKeyOrPrefix (yaml, "alert.rate_on_bps");
  RequireNoKeyOrPrefix (yaml, "fault.target_rate_bps");

  MaintenanceTrafficConfig config;
  config.durationS = RequireDouble (yaml, "sim.duration_s");
  config.rngSeed = RequireUint32 (yaml, "rng.seed");
  config.rngRun = RequireUint32 (yaml, "rng.run");
  config.maxUdpPayloadB = RequireUint32 (yaml, "validation.max_udp_payload_B");
  config.txTraceEnable = RequireBool (yaml, "logging.tx_trace_enable");
  config.printSummary = RequireBool (yaml, "logging.print_summary");
  config.packetTraceCsv = RequireString (yaml, "logging.packet_trace_csv");
  config.smokeP2pDataRate = RequireString (yaml, "smoke_test.p2p_data_rate");
  config.smokeP2pDelay = RequireString (yaml, "smoke_test.p2p_delay");
  config.smokeNetworkBase = RequireString (yaml, "smoke_test.network_base");
  config.smokeNetworkMask = RequireString (yaml, "smoke_test.network_mask");
  config.plotBinWidthS = RequireDouble (yaml, "plotting.bin_width_s");
  config.paperStyleFigurePng = RequireString (yaml, "plotting.paper_style_figure_png");
  config.loadSummaryFigurePng = RequireString (yaml, "plotting.load_summary_figure_png");
  config.diag = LoadStreamConfig (yaml, "diag", false);
  config.alert = LoadStreamConfig (yaml, "alert", false);
  config.fault = LoadStreamConfig (yaml, "fault", true);

  if (config.durationS <= 0.0)
    {
      throw std::runtime_error ("sim.duration_s must be positive.");
    }
  if (config.maxUdpPayloadB == 0)
    {
      throw std::runtime_error ("validation.max_udp_payload_B must be positive.");
    }
  if (config.plotBinWidthS <= 0.0)
    {
      throw std::runtime_error ("plotting.bin_width_s must be positive.");
    }

  ValidateWindow ("diag", config.diag, config.durationS);
  ValidateWindow ("alert", config.alert, config.durationS);
  ValidateWindow ("fault", config.fault, config.durationS);
  ValidatePayload ("diag", config.diag, config.maxUdpPayloadB);
  ValidatePayload ("alert", config.alert, config.maxUdpPayloadB);
  ValidatePayload ("fault", config.fault, config.maxUdpPayloadB);
  ValidateIat ("diag", config.diag);
  ValidateIat ("alert", config.alert);
  ValidateIat ("fault", config.fault);

  return config;
}

inline double MedianIatForLoadEstimate (const StreamConfig& stream)
{
  // For fixed IAT, the median is the period. For lognormal IAT, the median is
  // exp(mu). This is used only for summary/load estimates.
  if (stream.iat.model == "fixed")
    {
      return stream.iat.periodS;
    }
  if (stream.iat.model == "lognormal")
    {
      return std::exp (stream.iat.mu);
    }
  throw std::runtime_error ("Unsupported IAT model for load estimate: " + stream.iat.model);
}

inline double OfferedBitrateBpsFromMedianIat (const StreamConfig& stream)
{
  // Offered load is a derived quantity, not an independently fitted input.
  return static_cast<double> (stream.payloadB) * 8.0 / MedianIatForLoadEstimate (stream);
}

} // namespace evnr_maintenance
