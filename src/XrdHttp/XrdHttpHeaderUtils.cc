//------------------------------------------------------------------------------
// This file is part of XrdHTTP: A pragmatic implementation of the
// HTTP/WebDAV protocol for the Xrootd framework
//
// Copyright (c) 2025 by European Organization for Nuclear Research (CERN)
// Author: Cedric Caffy <ccaffy@cern.ch>
// File Date: Jun 2025
//------------------------------------------------------------------------------
// XRootD is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// XRootD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with XRootD.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------
#include "XrdHttpHeaderUtils.hh"
#include "XrdOuc/XrdOucTUtils.hh"
#include "XrdOuc/XrdOucUtils.hh"

#include <vector>
#include <algorithm>


void XrdHttpHeaderUtils::parseReprDigest(const std::string &value, std::map<std::string, std::string> &output) {
  // Expected format per entry: <cksumType>=:<digestValue>:
  std::vector<std::string> digestNameValuePairs;
  XrdOucTUtils::splitString(digestNameValuePairs, value, ",");

  for (const auto &digestNameValue : digestNameValuePairs) {
    std::string_view digestNameValueSV {digestNameValue};
    auto equalPos = digestNameValueSV.find('=');
    if (equalPos == std::string_view::npos || equalPos >= digestNameValueSV.size() - 1)
      continue;

    std::string_view cksumTypeSV = digestNameValueSV.substr(0, equalPos);
    XrdOucUtils::trim(cksumTypeSV);
    if (cksumTypeSV.empty())
      continue;

    std::string_view cksumValueInSV = digestNameValueSV.substr(equalPos + 1);
    size_t beginCksumPos = cksumValueInSV.find(':');
    size_t endCksumPos = cksumValueInSV.rfind(':');

    // Check that the string starts with ':' and contains two distinct colons
    if (beginCksumPos == 0 && endCksumPos > beginCksumPos + 1 && endCksumPos < cksumValueInSV.size()) {
      std::string_view cksumValue = cksumValueInSV.substr(beginCksumPos + 1, endCksumPos - beginCksumPos - 1);
      XrdOucUtils::trim(cksumValue);
      if (!cksumValue.empty())
        output[std::string(cksumTypeSV)] = cksumValue;
    }
    // Malformed entries are silently ignored
  }
}

void XrdHttpHeaderUtils::parseWantReprDigest(const std::string & value, std::map<std::string, ushort> &output) {
  // Expected format per entry: <cksumType>=<preference>
  std::vector<std::string_view> digestPreferencePairs;
  XrdOucTUtils::splitString(digestPreferencePairs,value,",");

  for (const auto & digestPreference: digestPreferencePairs) {
    std::vector<std::string_view> digestNameValue;
    XrdOucTUtils::splitString(digestNameValue,digestPreference,"=");
    if (digestNameValue.size() >= 2 && digestNameValue.size() % 2 == 0) {
      try {
        ushort preference = XrdOucUtils::toushort(digestNameValue[1]);
        if(preference > 10) {
          // Max value for preference according to rfc9530 is 10
          preference = 10;
        }
        XrdOucUtils::trim(digestNameValue[0]);
        output.emplace(digestNameValue[0],preference);
        // Discard invalid values
      } catch (...) {
        // Discard invalid values
      }
    }
  }
}