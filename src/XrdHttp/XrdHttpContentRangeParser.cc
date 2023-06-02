//------------------------------------------------------------------------------
// This file is part of XrdHTTP: A pragmatic implementation of the
// HTTP/WebDAV protocol for the Xrootd framework
//
// Copyright (c) 2013 by European Organization for Nuclear Research (CERN)
// Author: Cedric Caffy <ccaffy@cern.ch>
// File Date: Jun 2023
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


#include "XrdHttpContentRangeParser.hh"
#include "XProtocol/XPtypes.hh"
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <sstream>

const XrdHttpContentRangeParser::Error & XrdHttpContentRangeParser::getError() const {
  return error;
}

int XrdHttpContentRangeParser::parseContentRange(char *line) {
  int j;
  char *str1, *saveptr1, *token;



  for (j = 1, str1 = line;; j++, str1 = NULL) {
    token = strtok_r(str1, " ,\n=", &saveptr1);
    if (token == NULL)
      break;

    //printf("%d: %s\n", j, token);

    if (!strlen(token)) continue;


    int retCodeParseRWOp = parseRWOp(token);
    if(retCodeParseRWOp) {
      return retCodeParseRWOp;
    }
  }

  return 0;
}

int XrdHttpContentRangeParser::parseRWOp(char *str) {
  ReadWriteOp o1;
  int j;
  char *saveptr2, *str2, *subtoken, *endptr;
  bool ok = false;

  for (str2 = str, j = 0;; str2 = NULL, j++) {
    subtoken = strtok_r(str2, "-", &saveptr2);
    if (subtoken == NULL)
      break;

    switch (j) {
      case 0:
        o1.bytestart = strtoll(subtoken, &endptr, 0);
        if (!o1.bytestart && (endptr == subtoken)) o1.bytestart = -1;
        break;
      case 1:
        o1.byteend = strtoll(subtoken, &endptr, 0);
        if (!o1.byteend && (endptr == subtoken)) o1.byteend = -1;
        ok = true;
        break;
      default:
        // Malformed!
        ok = false;
        break;
    }

  }


  if (ok) {
    rwOps.push_back(o1);
    // In the case there is no bytestart or byteend set, the entire file will be read
    if(o1.bytestart == -1 || o1.byteend == -1) {
      return 0;
    }
    if(fileSize > 0) {
      // We know the size of the file on which the range request is done
      if(o1.byteend > fileSize - 1) {
        o1.byteend = fileSize - 1;
      }
      if(o1.bytestart > fileSize - 1) {
        //If the beginning of the range is greater than the fileSize, return a bad request error to the client
        std::stringstream ss;
        ss << "Invalid range " << o1.bytestart << "-" << o1.byteend << ". The beginning of the range is > than the file size = " << fileSize;
        error =  {400,ss.str()};
        return -1;
      }
    }
    long long totalRangeSize = o1.byteend - o1.bytestart + 1;
    if(totalRangeSize > vectorReadMaxChunkSize) {
      // Determine the amount of ranges to create
      uint64_t nbRange = (uint64_t)(totalRangeSize / vectorReadMaxChunkSize) + (totalRangeSize % vectorReadMaxChunkSize > 0 ? 1 : 0);
      long long rangeStart = o1.bytestart;
      for(uint64_t i = 0; i < nbRange; ++i) {
        ReadWriteOp splittedRange;
        long long rangeEnd = std::min(o1.byteend,rangeStart + vectorReadMaxChunkSize - 1);
        splittedRange.bytestart = rangeStart;
        splittedRange.byteend = rangeEnd;
        length += rangeEnd - rangeStart + 1;
        rwOps_split.push_back(splittedRange);
        rangeStart = rangeEnd + 1;
      }
    } else {
      rwOps_split.push_back(o1);
      length += o1.byteend - o1.bytestart + 1;
    }
  }
  // This can be largely optimized
  // Previous algorithm for future reference
  /*
  kXR_int32 len_ok = 0;
  long long sz = o1.byteend - o1.bytestart + 1;
  kXR_int32 newlen = sz;

  if (fileSize > 0)
    newlen = (kXR_int32) std::min(fileSize - o1.bytestart, sz);

  rwOps.push_back(o1);

  while (len_ok < newlen) {
    ReadWriteOp nfo;
    int len = std::min(newlen - len_ok, vectorReadMaxChunkSize);

    nfo.bytestart = o1.bytestart + len_ok;
    nfo.byteend = nfo.bytestart + len - 1;
    len_ok += len;
    rwOps_split.push_back(nfo);
  }
  length += len_ok;
}
*/
  return 0;
}