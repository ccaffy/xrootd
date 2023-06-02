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

#ifndef XROOTD_XRDHTTPCONTENTRANGEPARSER_HH
#define XROOTD_XRDHTTPCONTENTRANGEPARSER_HH

#include "ReadWriteOp.hh"
#include <vector>
#include <string>


/**
 * Class responsible for parsing the HTTP Content-Range header
 * coming from the client
 */
class XrdHttpContentRangeParser {
public:

  /**
   * Simple error structure in case the parsing is wrong
   */
  struct Error {
    int httpRetCode = 0;
    std::string errMsg;
  };

  XrdHttpContentRangeParser(const int vectorReadMaxChunkSize, long long fileSize, std::vector<ReadWriteOp> & rwOps, std::vector<ReadWriteOp> & rwOps_split, long long & length): vectorReadMaxChunkSize(vectorReadMaxChunkSize), fileSize(fileSize), rwOps(rwOps), rwOps_split(rwOps_split), length(length) {}
  /**
   * Parses the Content-Range header value
   * @param line the line under the format "bytes=0-19, 25-30"
   * @return 0 upon success, -1 if an error happened.
   * One needs to call the getError() method to return the error to the client.
   */
  int parseContentRange(char * line);
  const Error & getError() const;
private:
  /**
   * Method responsible for parsing ranges like "0-19, 25-30"
   * @param str the string containing the ranges
   * @return 0 upon success, -1 if an error happened.
   * One needs to call the getError() method to return the error to the client.
   */
  int parseRWOp(char * str);
  // The maximum chunk size for each vector read operation the XRootD server can support
  int vectorReadMaxChunkSize;
  // The size of the file that is going to be transferred (=0 if unknown)
  long long fileSize;
  // The readwrite operations corresponding to each range request (before being chunked) - output
  // parameter that comes from the XrdHttpReq object
  std::vector<ReadWriteOp> & rwOps;
  // The readwrite operations corresponding to reach chunked range request - output
  // parameter that comes from the XrdHttpReq object
  std::vector<ReadWriteOp> & rwOps_split;
  // The total size of the file that will need to be read - output
  // parameter that comes from the XrdHttpReq object
  long long & length;
  // The error object in the case the parsing of the Range header is wrong
  Error error;
};


#endif //XROOTD_XRDHTTPCONTENTRANGEPARSER_HH
