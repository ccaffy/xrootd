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


void XrdHttpHeaderUtils::parseReprDigest(const std::string &header, std::map<std::string, std::string> &output) {
  // val contains values like: adler32=:<digest_value>:, sha256=:<digest_value>:
  std::vector<std::string> digestNameValuePairs;
  // Split the input by "comma"
  XrdOucTUtils::splitString(digestNameValuePairs,header,",");
  std::for_each(digestNameValuePairs.begin(),digestNameValuePairs.end(),[&output](const std::string & str) {
    // For each digestName=digestValue pair
    auto equalPos = str.find_first_of('=');
    // Get first equal and ensure there's a character after it
    if(equalPos < str.size() - 1) {
      // Extract the checksum type
      std::string cksumType = str.substr(0,equalPos);
      XrdOucUtils::trim(cksumType);
      // Extract the checksum value which is after the "=" sign
      std::string cksumValueIn = str.substr(equalPos + 1);
      // The checksum value should start with ':' and should have at least one character at the end
      size_t beginCksumPos = cksumValueIn.find_first_of(':');
      if((beginCksumPos == 0) && beginCksumPos < cksumValueIn.size() - 1) {
        size_t endCksumPos = cksumValueIn.find_last_of(':');
        // The second ':' should not be the first matched ':'
        if(beginCksumPos < endCksumPos) {
          std::string cksumValue = cksumValueIn.substr(beginCksumPos + 1,endCksumPos - 1);
          XrdOucUtils::trim(cksumValue);
          output[cksumType] = cksumValue;
        }
        // Anything that is malformed is not put to the output map
      }
    }
  });
}