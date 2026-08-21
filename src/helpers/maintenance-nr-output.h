/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/**
 * @file maintenance-nr-output.h
 * @brief Provide collision-safe output paths and run metadata helpers.
 *
 * Inputs: output root, experiment name, seed, run, and metadata key/value pairs.
 * Outputs: unique run directories, metadata text, and path strings.
 */
#pragma once

#include "ns3/system-path.h"

#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace evnr_maintenance {

inline std::string JoinPath (const std::string& dir, const std::string& file)
{
  if (dir.empty ())
    {
      return file;
    }
  if (dir.back () == '/')
    {
      return dir + file;
    }
  return dir + "/" + file;
}

inline std::string TimestampForPath ()
{
  std::time_t now = std::time (nullptr);
  std::tm local = *std::localtime (&now);
  std::ostringstream out;
  out << (local.tm_year + 1900)
      << std::setw (2) << std::setfill ('0') << (local.tm_mon + 1)
      << std::setw (2) << std::setfill ('0') << local.tm_mday
      << "_"
      << std::setw (2) << std::setfill ('0') << local.tm_hour
      << std::setw (2) << std::setfill ('0') << local.tm_min
      << std::setw (2) << std::setfill ('0') << local.tm_sec;
  return out.str ();
}

inline std::string CreateUniqueRunDirectory (const std::string& root,
                                             const std::string& runName,
                                             uint32_t seed,
                                             uint32_t run)
{
  std::ostringstream leaf;
  leaf << runName << "_seed" << seed << "_run" << run << "_" << TimestampForPath ();
  const std::string path = JoinPath (root, leaf.str ());
  ns3::SystemPath::MakeDirectories (path);
  return path;
}

} // namespace evnr_maintenance
