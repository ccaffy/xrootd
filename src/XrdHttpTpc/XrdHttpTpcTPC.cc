#include "XrdHttp/XrdHttpExtHandler.hh"
#include "XrdNet/XrdNetAddr.hh"
#include "XrdNet/XrdNetUtils.hh"
#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSec/XrdSecEntity.hh"
#include "XrdSfs/XrdSfsInterface.hh"
#include "XrdSys/XrdSysAtomics.hh"
#include "XrdSys/XrdSysFD.hh"
#include "XrdVersion.hh"

#include "XrdXrootd/XrdXrootdTpcMon.hh"
#include "XrdOuc/XrdOucTUtils.hh"
#include "XrdHttpTpc/XrdHttpTpcUtils.hh"

#include <curl/curl.h>

#include <dlfcn.h>
#include <fcntl.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <iostream> // Delete later!!!

#include "XrdHttpTpcState.hh"
#include "XrdHttpTpcStream.hh"
#include "XrdHttpTpcTPC.hh"
#include <fstream>

using namespace TPC;

XrdXrootdTpcMon* TPCHandler::TPCLogRecord::tpcMonitor = 0;

uint64_t TPCHandler::m_monid{0};
int TPCHandler::m_marker_period = 5;
size_t TPCHandler::m_block_size = 16*1024*1024;
size_t TPCHandler::m_small_block_size = 1*1024*1024;
XrdSysMutex TPCHandler::m_monid_mutex;

XrdVERSIONINFO(XrdHttpGetExtHandler, HttpTPC);

/******************************************************************************/
/*   T P C H a n d l e r : : T P C L o g R e c o r d   D e s t r u c t o r    */
/******************************************************************************/
  
TPCHandler::TPCLogRecord::~TPCLogRecord()
{
// Record monitoring data is enabled
//
   if (tpcMonitor)
      {XrdXrootdTpcMon::TpcInfo monInfo;

       monInfo.clID = clID.c_str();
       monInfo.begT = begT;
       gettimeofday(&monInfo.endT, 0);

       if (mTpcType == TpcType::Pull)
          {monInfo.dstURL = local.c_str();
           monInfo.srcURL = remote.c_str();
          } else {
           monInfo.dstURL = remote.c_str();
           monInfo.srcURL = local.c_str();
           monInfo.opts |= XrdXrootdTpcMon::TpcInfo::isaPush;
          }

       if (!status) monInfo.endRC = 0;
          else if (tpc_status > 0) monInfo.endRC = tpc_status;
                  else  monInfo.endRC = 1;
       monInfo.strm  = static_cast<unsigned char>(streams);
       monInfo.fSize = (bytes_transferred < 0 ? 0 : bytes_transferred);
       if (!isIPv6) monInfo.opts |= XrdXrootdTpcMon::TpcInfo::isIPv4;

       tpcMonitor->Report(monInfo);
      }
}
  
/******************************************************************************/
/*               C u r l D e l e t e r : : o p e r a t o r ( )                */
/******************************************************************************/
  
void CurlDeleter::operator()(CURL *curl)
{
    if (curl) curl_easy_cleanup(curl);
}

/******************************************************************************/
/*           s o c k o p t _ s e t c l o e x e c _ c a l l b a c k            */
/******************************************************************************/
  
/**
 * The callback that will be called by libcurl when the socket has been created
 * https://curl.se/libcurl/c/CURLOPT_SOCKOPTFUNCTION.html
 *
 * Note: that this callback has been replaced by the opensocket_callback as it
 *       was needed for monitoring to report what IP protocol was being used.
 *       It has been kept in case we will need this callback in the future.
 */
int TPCHandler::sockopt_callback(void *clientp, curl_socket_t curlfd, curlsocktype purpose) {
  TPCLogRecord * rec = (TPCLogRecord *)clientp;
  if (purpose == CURLSOCKTYPE_IPCXN && rec && rec->pmarkManager.isEnabled()) {
      // We will not reach this callback if the corresponding socket could not have been connected
      // the socket is already connected only if the packet marking is enabled
      return CURL_SOCKOPT_ALREADY_CONNECTED;
  }
  return CURL_SOCKOPT_OK;
}

/******************************************************************************/
/*                   o p e n s o c k e t _ c a l l b a c k                    */
/******************************************************************************/
  
  
/**
 * The callback that will be called by libcurl when the socket is about to be
 * opened so we can capture the protocol that will be used.
 */
int TPCHandler::opensocket_callback(void *clientp,
                                    curlsocktype purpose,
                                    struct curl_sockaddr *aInfo)
{
  //Return a socket file descriptor (note the clo_exec flag will be set).
  int fd = XrdSysFD_Socket(aInfo->family, aInfo->socktype, aInfo->protocol);
  // See what kind of address will be used to connect
  //
  if(fd < 0) {
    return CURL_SOCKET_BAD;
  }
  TPCLogRecord * rec = (TPCLogRecord *)clientp;
  if (purpose == CURLSOCKTYPE_IPCXN && clientp)
  {XrdNetAddr thePeer(&(aInfo->addr));
    rec->isIPv6 =  (thePeer.isIPType(XrdNetAddrInfo::IPv6)
                    && !thePeer.isMapped());
    std::stringstream connectErrMsg;

    if(!rec->pmarkManager.connect(fd, &(aInfo->addr), aInfo->addrlen, CONNECT_TIMEOUT, connectErrMsg)) {
      rec->m_log->Emsg(rec->log_prefix.c_str(),"Unable to connect socket:", connectErrMsg.str().c_str());
      return CURL_SOCKET_BAD;
    }
  }

  return fd;
}

int TPCHandler::closesocket_callback(void *clientp, curl_socket_t fd) {
  TPCLogRecord * rec = (TPCLogRecord *)clientp;

  // Destroy the PMark handle associated to the file descriptor before closing it.
  // Otherwise, we would lose the socket usage information if the socket is closed before
  // the PMark handle is closed.
  rec->pmarkManager.endPmark(fd);

  return close(fd);
}

/******************************************************************************/
/*                            p r e p a r e U R L                             */
/******************************************************************************/

// See XrdHttpTpcUtils::prepareOpenURL() documentation
std::string TPCHandler::prepareURL(XrdHttpExtReq &req) {
  return XrdHttpTpcUtils::prepareOpenURL(req.resource, req.headers,hdr2cgimap);
}

/******************************************************************************/
/*           e n c o d e _ x r o o t d _ o p a q u e _ t o _ u r i            */
/******************************************************************************/
  
// When processing a redirection from the filesystem layer, it is permitted to return
// some xrootd opaque data.  The quoting rules for xrootd opaque data are significantly
// more permissive than a URI (basically, only '&' and '=' are disallowed while some
// URI parsers may dislike characters like '"').  This function takes an opaque string
// (e.g., foo=1&bar=2&baz=") and makes it safe for all URI parsers.
std::string encode_xrootd_opaque_to_uri(CURL *curl, const std::string &opaque)
{
    std::stringstream parser(opaque);
    std::string sequence;
    std::stringstream output;
    bool first = true;
    while (getline(parser, sequence, '&')) {
        if (sequence.empty()) {continue;}
        size_t equal_pos = sequence.find('=');
        char *val = NULL;
        if (equal_pos != std::string::npos)
            val = curl_easy_escape(curl, sequence.c_str() + equal_pos + 1, sequence.size()  - equal_pos - 1);
        // Do not emit parameter if value exists and escaping failed.
        if (!val && equal_pos != std::string::npos) {continue;}

        if (!first) output << "&";
        first = false;
        output << sequence.substr(0, equal_pos);
        if (val) {
            output << "=" << val;
            curl_free(val);
        }
    }
    return output.str();
}

/******************************************************************************/
/*           T P C H a n d l e r : : C o n f i g u r e C u r l C A            */
/******************************************************************************/
  
void
TPCHandler::ConfigureCurlCA(CURL *curl)
{
    auto ca_filename = m_ca_file ? m_ca_file->CAFilename() : "";
    auto crl_filename = m_ca_file ? m_ca_file->CRLFilename() : "";
    if (!ca_filename.empty() && !crl_filename.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_filename.c_str());
        //Check that the CRL file contains at least one entry before setting this option to curl
        //Indeed, an empty CRL file will make curl unhappy and therefore will fail
        //all HTTP TPC transfers (https://github.com/xrootd/xrootd/issues/1543)
        std::ifstream in(crl_filename, std::ifstream::ate | std::ifstream::binary);
        if(in.tellg() > 0 && m_ca_file->atLeastOneValidCRLFound()){
            curl_easy_setopt(curl, CURLOPT_CRLFILE, crl_filename.c_str());
        } else {
            std::ostringstream oss;
            oss << "No valid CRL file has been found in the file " << crl_filename << ". Disabling CRL checking.";
            m_log.Log(Warning,"TpcHandler",oss.str().c_str());
        }
    }
    else if (!m_cadir.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAPATH, m_cadir.c_str());
    }
    if (!m_cafile.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, m_cafile.c_str());
    }
}


bool TPCHandler::MatchesPath(const char *verb, const char *path) {
    return !strcmp(verb, "COPY") || !strcmp(verb, "OPTIONS");
}

/******************************************************************************/
/*                            P r e p a r e U R L                             */
/******************************************************************************/
  
static std::string PrepareURL(const std::string &input) {
    if (!strncmp(input.c_str(), "davs://", 7)) {
        return "https://" + input.substr(7);
    }
    return input;
}

/******************************************************************************/
/*                T P C H a n d l e r : : P r o c e s s R e q                 */
/******************************************************************************/
  
int TPCHandler::ProcessReq(XrdHttpExtReq &req) {
    if (req.verb == "OPTIONS") {
        return ProcessOptionsReq(req);
    }
    auto header = XrdOucTUtils::caseInsensitiveFind(req.headers,"credential");
    if (header != req.headers.end()) {
        if (header->second != "none") {
            m_log.Emsg("ProcessReq", "COPY requested an unsupported credential type: ", header->second.c_str());
            return req.SendSimpleResp(400, NULL, NULL, "COPY requestd an unsupported Credential type", 0);
        }
    }
    header = XrdOucTUtils::caseInsensitiveFind(req.headers,"source");
    if (header != req.headers.end()) {
        std::string src = PrepareURL(header->second);
        return ProcessPullReq(src, req);
    }
    header = XrdOucTUtils::caseInsensitiveFind(req.headers,"destination");
    if (header != req.headers.end()) {
        return ProcessPushReq(header->second, req);
    }
    m_log.Emsg("ProcessReq", "COPY verb requested but no source or destination specified.");
    return req.SendSimpleResp(400, NULL, NULL, "No Source or Destination specified", 0);
}

/******************************************************************************/
/*                 T P C H a n d l e r   D e s t r u c t o r                  */
/******************************************************************************/
  
TPCHandler::~TPCHandler() {
    m_sfs = NULL;
}

/******************************************************************************/
/*                T P C H a n d l e r   C o n s t r u c t o r                 */
/******************************************************************************/
  
TPCHandler::TPCHandler(XrdSysError *log, const char *config, XrdOucEnv *myEnv) :
        m_desthttps(false),
        m_fixed_route(false),
        m_timeout(60),
        m_first_timeout(120),
        m_log(log->logger(), "TPC_"),
        m_sfs(NULL)
{
    if (!Configure(config, myEnv)) {
        throw std::runtime_error("Failed to configure the HTTP third-party-copy handler.");
    }

// Extract out the TPC monitoring object (we share it with xrootd).
//
   XrdXrootdGStream *gs = (XrdXrootdGStream*)myEnv->GetPtr("Tpc.gStream*");
   if (gs)
      TPCLogRecord::tpcMonitor = new XrdXrootdTpcMon("http",log->logger(),*gs);
}

/******************************************************************************/
/*         T P C H a n d l e r : : P r o c e s s O p t i o n s R e q          */
/******************************************************************************/
  
/**
 * Handle the OPTIONS verb as we have added a new one...
 */
int TPCHandler::ProcessOptionsReq(XrdHttpExtReq &req) {
    return req.SendSimpleResp(200, NULL, (char *) "DAV: 1\r\nDAV: <http://apache.org/dav/propset/fs/1>\r\nAllow: HEAD,GET,PUT,PROPFIND,DELETE,OPTIONS,COPY", NULL, 0);
}

/******************************************************************************/
/*                  T P C H a n d l e r : : G e t A u t h z                   */
/******************************************************************************/
  
std::string TPCHandler::GetAuthz(XrdHttpExtReq &req) {
    std::string authz;
    auto authz_header = XrdOucTUtils::caseInsensitiveFind(req.headers,"authorization");
    if (authz_header != req.headers.end()) {
        std::stringstream ss;
        ss << "authz=" << encode_str(authz_header->second);
        authz += ss.str();
    }
    return authz;
}

/******************************************************************************/
/*          T P C H a n d l e r : : R e d i r e c t T r a n s f e r           */
/******************************************************************************/
  
int TPCHandler::RedirectTransfer(CURL *curl, const std::string &redirect_resource,
    XrdHttpExtReq &req, XrdOucErrInfo &error, TPCLogRecord &rec)
{
    int port;
    const char *ptr = error.getErrText(port);
    if ((ptr == NULL) || (*ptr == '\0') || (port == 0)) {
        rec.status = 500;
        std::stringstream ss;
        ss << "Internal error: redirect without hostname";
        logTransferEvent(LogMask::Error, rec, "REDIRECT_INTERNAL_ERROR", ss.str());
        return req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0);
    }

    // Construct redirection URL taking into consideration any opaque info
    std::string rdr_info = ptr;
    std::string host, opaque;
    size_t pos = rdr_info.find('?');
    host = rdr_info.substr(0, pos);

    if (pos != std::string::npos) {
      opaque = rdr_info.substr(pos + 1);
    }

    std::stringstream ss;
    ss << "Location: http" << (m_desthttps ? "s" : "") << "://" << host << ":" << port << "/" << redirect_resource;

    if (!opaque.empty()) {
      ss << "?" << encode_xrootd_opaque_to_uri(curl, opaque);
    }

    rec.status = 307;
    logTransferEvent(LogMask::Info, rec, "REDIRECT", ss.str());
    return req.SendSimpleResp(rec.status, NULL, const_cast<char *>(ss.str().c_str()),
        NULL, 0);
}

/******************************************************************************/
/*             T P C H a n d l e r : : O p e n W a i t S t a l l              */
/******************************************************************************/
  
int TPCHandler::OpenWaitStall(XrdSfsFile &fh, const std::string &resource,
                      int mode, int openMode, const XrdSecEntity &sec,
                      const std::string &authz)
{
    int open_result;
    while (1) {
        int orig_ucap = fh.error.getUCap();
        fh.error.setUCap(orig_ucap | XrdOucEI::uIPv64);
        std::string opaque;
        size_t pos = resource.find('?');
        // Extract the path and opaque info from the resource
        std::string path = resource.substr(0, pos);

        if (pos != std::string::npos) {
          opaque = resource.substr(pos + 1);
        }

        // Append the authz information if there are some
        if(!authz.empty()) {
            opaque += (opaque.empty() ? "" : "&");
            opaque += authz;
        }
        open_result = fh.open(path.c_str(), mode, openMode, &sec, opaque.c_str());

        if ((open_result == SFS_STALL) || (open_result == SFS_STARTED)) {
            int secs_to_stall = fh.error.getErrInfo();
            if (open_result == SFS_STARTED) {secs_to_stall = secs_to_stall/2 + 5;}
            std::this_thread::sleep_for (std::chrono::seconds(secs_to_stall));
        }
        break;
    }
    return open_result;
}

/******************************************************************************/
/*         T P C H a n d l e r : : D e t e r m i n e X f e r S i z e          */
/******************************************************************************/



/**
 * Determine size at remote end.
 */
int TPCHandler::DetermineXferSize(CURL *curl, XrdHttpExtReq &req, State &state,
                                  bool &success, TPCLogRecord &rec, bool shouldReturnErrorToClient) {
    success = false;
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    // Set a custom timeout of 60 seconds (= CONNECT_TIMEOUT for convenience) for the HEAD request
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, CONNECT_TIMEOUT);
    CURLcode res;
    res = curl_easy_perform(curl);
    //Immediately set the CURLOPT_NOBODY flag to 0 as we anyway
    //don't want the next curl call to do be a HEAD request
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0);
    // Reset the CURLOPT_TIMEOUT to no timeout (default)
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    if (res == CURLE_HTTP_RETURNED_ERROR) {
        std::stringstream ss;
        ss << "Remote server failed request while fetching remote size";
        std::stringstream ss2;
        ss2 << ss.str() << ": " << curl_easy_strerror(res);
        rec.status = 500;
        logTransferEvent(LogMask::Error, rec, "SIZE_FAIL", ss2.str());
        return shouldReturnErrorToClient ? req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec, res).c_str(), 0) : -1;
    } else if (state.GetStatusCode() >= 400) {
        std::stringstream ss;
        ss << "Remote side " << req.clienthost << " failed with status code " << state.GetStatusCode() << " while fetching remote size";
        rec.status = 500;
        logTransferEvent(LogMask::Error, rec, "SIZE_FAIL", ss.str());
        return shouldReturnErrorToClient ? req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0) : -1;
    } else if (res) {
        std::stringstream ss;
        ss << "Internal transfer failure while fetching remote size";
        std::stringstream ss2;
        ss2 << ss.str() << " - HTTP library failed: " << curl_easy_strerror(res);
        rec.status = 500;
        logTransferEvent(LogMask::Error, rec, "SIZE_FAIL", ss2.str());
        return shouldReturnErrorToClient ? req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec, res).c_str(), 0) : -1;
    }
    std::stringstream ss;
    ss << "Successfully determined remote size for pull request: "
       << state.GetContentLength();
    logTransferEvent(LogMask::Debug, rec, "SIZE_SUCCESS", ss.str());
    success = true;
    return 0;
}

int TPCHandler::GetContentLengthTPCPull(CURL *curl, XrdHttpExtReq &req, uint64_t &contentLength, bool & success, TPCLogRecord &rec) {
    State state(curl,req.tpcForwardCreds);
    //Don't forget to copy the headers of the client's request before doing the HEAD call. Otherwise, if there is a need for authentication,
    //it will fail
    state.SetupHeaders(req);
    int result;
    //In case we cannot get the content length, we return the error to the client
    if ((result = DetermineXferSize(curl, req, state, success, rec)) || !success) {
        return result;
    }
    contentLength = state.GetContentLength();
    return result;
}
  
/******************************************************************************/
/*            T P C H a n d l e r : : S e n d P e r f M a r k e r             */
/******************************************************************************/
  
int TPCHandler::SendPerfMarker(XrdHttpExtReq &req, TPCLogRecord &rec, TPC::State &state) {
    std::stringstream ss;
    const std::string crlf = "\n";
    ss << "Perf Marker" << crlf;
    ss << "Timestamp: " << time(NULL) << crlf;
    ss << "Stripe Index: 0" << crlf;
    ss << "Stripe Bytes Transferred: " << state.BytesTransferred() << crlf;
    ss << "Total Stripe Count: 1" << crlf;
    // Include the TCP connection associated with this transfer; used by
    // the TPC client for monitoring purposes.
    std::string desc = state.GetConnectionDescription();
    if (!desc.empty())
        ss << "RemoteConnections: " << desc << crlf;
    ss << "End" << crlf;
    rec.bytes_transferred = state.BytesTransferred();
    logTransferEvent(LogMask::Debug, rec, "PERF_MARKER");

    return req.ChunkResp(ss.str().c_str(), 0);
}

/******************************************************************************/
/*            T P C H a n d l e r : : S e n d P e r f M a r k e r             */
/******************************************************************************/
  
int TPCHandler::SendPerfMarker(XrdHttpExtReq &req, TPCLogRecord &rec, std::vector<State*> &state,
    off_t bytes_transferred)
{
    // The 'performance marker' format is largely derived from how GridFTP works
    // (e.g., the concept of `Stripe` is not quite so relevant here).  See:
    //    https://twiki.cern.ch/twiki/bin/view/LCG/HttpTpcTechnical
    // Example marker:
    //    Perf Marker\n
    //    Timestamp: 1537788010\n
    //    Stripe Index: 0\n
    //    Stripe Bytes Transferred: 238745\n
    //    Total Stripe Count: 1\n
    //    RemoteConnections: tcp:129.93.3.4:1234,tcp:[2600:900:6:1301:268a:7ff:fef6:a590]:2345\n
    //    End\n
    //
    std::stringstream ss;
    const std::string crlf = "\n";
    ss << "Perf Marker" << crlf;
    ss << "Timestamp: " << time(NULL) << crlf;
    ss << "Stripe Index: 0" << crlf;
    ss << "Stripe Bytes Transferred: " << bytes_transferred << crlf;
    ss << "Total Stripe Count: 1" << crlf;
    // Build a list of TCP connections associated with this transfer; used by
    // the TPC client for monitoring purposes.
    bool first = true;
    std::stringstream ss2;
    for (std::vector<State*>::const_iterator iter = state.begin();
        iter != state.end(); iter++)
    {
        std::string desc = (*iter)->GetConnectionDescription();
        if (!desc.empty()) {
            ss2 << (first ? "" : ",") << desc;
            first = false;
        }
    }
    if (!first)
        ss << "RemoteConnections: " << ss2.str() << crlf;
    ss << "End" << crlf;
    rec.bytes_transferred = bytes_transferred;
    logTransferEvent(LogMask::Debug, rec, "PERF_MARKER");

    return req.ChunkResp(ss.str().c_str(), 0);
}

/******************************************************************************/
/*        T P C H a n d l e r : : R u n C u r l W i t h U p d a t e s         */
/******************************************************************************/
  
int TPCHandler::RunCurlWithUpdates(CURL *curl, XrdHttpExtReq &req, State &state,
    TPCLogRecord &rec)
{
    // Create the multi-handle and add in the current transfer to it.
    CURLM *multi_handle = curl_multi_init();
    if (!multi_handle) {
        rec.status = 500;
        logTransferEvent(LogMask::Error, rec, "CURL_INIT_FAIL",
            "Failed to initialize a libcurl multi-handle");
        std::stringstream ss;
        ss << "Failed to initialize internal server memory";
        return req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0);
    }

    //curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 128*1024);

    CURLMcode mres;
    mres = curl_multi_add_handle(multi_handle, curl);
    if (mres) {
        rec.status = 500;
        std::stringstream ss;
        ss << "Failed to add transfer to libcurl multi-handle: HTTP library failure=" << curl_multi_strerror(mres);
        logTransferEvent(LogMask::Error, rec, "CURL_INIT_FAIL", ss.str());
        curl_multi_cleanup(multi_handle);
        return req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0);
    }

    // Start response to client prior to the first call to curl_multi_perform
    int retval = req.StartChunkedResp(201, "Created", "Content-Type: text/plain");
    if (retval) {
        curl_multi_cleanup(multi_handle);
        logTransferEvent(LogMask::Error, rec, "RESPONSE_FAIL",
            "Failed to send the initial response to the TPC client");
        return retval;
    } else {
        logTransferEvent(LogMask::Debug, rec, "RESPONSE_START",
            "Initial transfer response sent to the TPC client");
    }

    // Transfer loop: use curl to actually run the transfer, but periodically
    // interrupt things to send back performance updates to the client.
    int running_handles = 1;
    time_t last_marker = 0;
    // Track how long it's been since the last time we recorded more bytes being transferred.
    off_t last_advance_bytes = 0;
    time_t last_advance_time = time(NULL);
    time_t transfer_start = last_advance_time;
    CURLcode res = static_cast<CURLcode>(-1);
    do {
        time_t now = time(NULL);
        time_t next_marker = last_marker + m_marker_period;
        if (now >= next_marker) {
            off_t bytes_xfer = state.BytesTransferred();
            if (bytes_xfer > last_advance_bytes) {
                last_advance_bytes = bytes_xfer;
                last_advance_time = now;
            }
            if (SendPerfMarker(req, rec, state)) {
                curl_multi_remove_handle(multi_handle, curl);
                curl_multi_cleanup(multi_handle);
                logTransferEvent(LogMask::Error, rec, "PERFMARKER_FAIL",
                    "Failed to send a perf marker to the TPC client");
                return -1;
            }
            int timeout = (transfer_start == last_advance_time) ? m_first_timeout : m_timeout;
            if (now > last_advance_time + timeout) {
                const char *log_prefix = rec.log_prefix.c_str();
                bool tpc_pull = strncmp("Pull", log_prefix, 4) == 0;

                state.SetErrorCode(10);
                std::stringstream ss;
                ss << "Transfer failed because no bytes have been "
                   << (tpc_pull ? "received from the source (pull mode) in "
                                : "transmitted to the destination (push mode) in ") << timeout << " seconds.";
                state.SetErrorMessage(ss.str());
                curl_multi_remove_handle(multi_handle, curl);
                curl_multi_cleanup(multi_handle);
                break;
            }
            last_marker = now;
        }
        // The transfer will start after this point, notify the packet marking manager
        rec.pmarkManager.startTransfer();
        mres = curl_multi_perform(multi_handle, &running_handles);
        if (mres == CURLM_CALL_MULTI_PERFORM) {
            // curl_multi_perform should be called again immediately.  On newer
            // versions of curl, this is no longer used.
            continue;
        } else if (mres != CURLM_OK) {
            break;
        } else if (running_handles == 0) {
            break;
        }

        rec.pmarkManager.beginPMarks();
        //printf("There are %d running handles\n", running_handles);

        // Harvest any messages, looking for CURLMSG_DONE.
        CURLMsg *msg;
        do {
            int msgq = 0;
            msg = curl_multi_info_read(multi_handle, &msgq);
            if (msg && (msg->msg == CURLMSG_DONE)) {
                CURL *easy_handle = msg->easy_handle;
                res = msg->data.result;
                curl_multi_remove_handle(multi_handle, easy_handle);
            }
        } while (msg);

        int64_t max_sleep_time = next_marker - time(NULL);
        if (max_sleep_time <= 0) {
            continue;
        }
        int fd_count;
        mres = curl_multi_wait(multi_handle, NULL, 0, max_sleep_time*1000, &fd_count);
        if (mres != CURLM_OK) {
            break;
        }
    } while (running_handles);

    if (mres != CURLM_OK) {
        std::stringstream ss;
        ss << "Internal libcurl multi-handle error: HTTP library failure=" << curl_multi_strerror(mres);
        logTransferEvent(LogMask::Error, rec, "TRANSFER_CURL_ERROR", ss.str());

        curl_multi_remove_handle(multi_handle, curl);
        curl_multi_cleanup(multi_handle);

        if ((retval = req.ChunkResp(generateClientErr(ss, rec).c_str(), 0))) {
            logTransferEvent(LogMask::Error, rec, "RESPONSE_FAIL",
                "Failed to send error message to the TPC client");
            return retval;
        }
        return req.ChunkResp(NULL, 0);
    }

    // Harvest any messages, looking for CURLMSG_DONE.
    CURLMsg *msg;
    do {
        int msgq = 0;
        msg = curl_multi_info_read(multi_handle, &msgq);
        if (msg && (msg->msg == CURLMSG_DONE)) {
            CURL *easy_handle = msg->easy_handle;
            res = msg->data.result;
            curl_multi_remove_handle(multi_handle, easy_handle);
        }
    } while (msg);

    if (!state.GetErrorCode() && res == static_cast<CURLcode>(-1)) { // No transfers returned?!?
        curl_multi_remove_handle(multi_handle, curl);
        curl_multi_cleanup(multi_handle);
        std::stringstream ss;
        ss << "Internal state error in libcurl";
        logTransferEvent(LogMask::Error, rec, "TRANSFER_CURL_ERROR", ss.str());

        if ((retval = req.ChunkResp(generateClientErr(ss, rec).c_str(), 0))) {
            logTransferEvent(LogMask::Error, rec, "RESPONSE_FAIL",
                "Failed to send error message to the TPC client");
            return retval;
        }
        return req.ChunkResp(NULL, 0);
    }
    curl_multi_cleanup(multi_handle);

    state.Flush();

    rec.bytes_transferred = state.BytesTransferred();
    rec.tpc_status = state.GetStatusCode();

    // Explicitly finalize the stream (which will close the underlying file
    // handle) before the response is sent.  In some cases, subsequent HTTP
    // requests can occur before the filesystem is done closing the handle -
    // and those requests may occur against partial data.
    state.Finalize();

    // Generate the final response back to the client.
    std::stringstream ss;
    bool success = false;
    if (state.GetStatusCode() >= 400) {
        std::string err = state.GetErrorMessage();
        std::stringstream ss2;
        ss2 << "Remote side failed with status code " << state.GetStatusCode();
        if (!err.empty()) {
            std::replace(err.begin(), err.end(), '\n', ' ');
            ss2 << "; error message: \"" << err << "\"";
        }
        logTransferEvent(LogMask::Error, rec, "TRANSFER_FAIL", ss2.str());
        ss << generateClientErr(ss2, rec);
    } else if (state.GetErrorCode()) {
        std::string err = state.GetErrorMessage();
        if (err.empty()) {err = "(no error message provided)";}
        else {std::replace(err.begin(), err.end(), '\n', ' ');}
        std::stringstream ss2;
        ss2 << "Error when interacting with local filesystem: " << err;
        logTransferEvent(LogMask::Error, rec, "TRANSFER_FAIL", ss2.str());
        ss << generateClientErr(ss2, rec);
    } else if (res != CURLE_OK) {
        std::stringstream ss2;
        ss2 << "Internal transfer failure";
        std::stringstream ss3;
        ss3 << ss2.str() << ": " << curl_easy_strerror(res);
        logTransferEvent(LogMask::Error, rec, "TRANSFER_FAIL", ss3.str());
        ss << generateClientErr(ss2, rec, res);
    } else {
        ss << "success: Created";
        success = true;
    }

    if ((retval = req.ChunkResp(ss.str().c_str(), 0))) {
        logTransferEvent(LogMask::Error, rec, "TRANSFER_ERROR",
            "Failed to send last update to remote client");
        return retval;
    } else if (success) {
        logTransferEvent(LogMask::Info, rec, "TRANSFER_SUCCESS");
        rec.status = 0;
    }
    return req.ChunkResp(NULL, 0);
}

/******************************************************************************/
/*            T P C H a n d l e r : : P r o c e s s P u s h R e q             */
/******************************************************************************/
  
int TPCHandler::ProcessPushReq(const std::string & resource, XrdHttpExtReq &req) {
    TPCLogRecord rec(req, TpcType::Push);
    rec.log_prefix = "PushRequest";
    rec.local = req.resource;
    rec.remote = resource;
    rec.m_log = &m_log;
    char *name = req.GetSecEntity().name;
    req.GetClientID(rec.clID);
    if (name) rec.name = name;
    logTransferEvent(LogMask::Info, rec, "PUSH_START", "Starting a push request");

    ManagedCurlHandle curlPtr(curl_easy_init());
    auto curl = curlPtr.get();
    if (!curl) {
        std::stringstream ss;
        ss << "Failed to initialize internal transfer resources";
        rec.status = 500;
        logTransferEvent(LogMask::Error, rec, "PUSH_FAIL", ss.str());
        return req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0);
    }
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long) CURL_HTTP_VERSION_1_1);
//  curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockopt_setcloexec_callback);

    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, opensocket_callback);
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, &rec);
    curl_easy_setopt(curl, CURLOPT_CLOSESOCKETFUNCTION, closesocket_callback);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockopt_callback);
    curl_easy_setopt(curl, CURLOPT_CLOSESOCKETDATA, &rec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT);
    auto query_header = XrdOucTUtils::caseInsensitiveFind(req.headers,"xrd-http-fullresource");
    std::string redirect_resource = req.resource;
    if (query_header != req.headers.end()) {
        redirect_resource = query_header->second;
    }

    AtomicBeg(m_monid_mutex);
    uint64_t file_monid = AtomicInc(m_monid);
    AtomicEnd(m_monid_mutex);
    std::unique_ptr<XrdSfsFile> fh(m_sfs->newFile(name, file_monid));
    if (!fh.get()) {
        rec.status = 500;
        std::stringstream ss;
        ss <<  "Failed to initialize internal transfer file handle";
        logTransferEvent(LogMask::Error, rec, "OPEN_FAIL",
                         ss.str());
        return req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0);
    }
    std::string full_url = prepareURL(req);

    std::string authz = GetAuthz(req);

    int open_results = OpenWaitStall(*fh, full_url, SFS_O_RDONLY, 0644,
                                     req.GetSecEntity(), authz);
    if (SFS_REDIRECT == open_results) {
        int result = RedirectTransfer(curl, redirect_resource, req, fh->error, rec);
        return result;
    } else if (SFS_OK != open_results) {
        int code;
        std::stringstream ss;
        const char *msg = fh->error.getErrText(code);
        if (msg == NULL) ss << "Failed to open local resource";
        else ss << msg;
        rec.status = 400;
        if (code == EACCES) rec.status = 401;
        else if (code == EEXIST) rec.status = 412;
        logTransferEvent(LogMask::Error, rec, "OPEN_FAIL", msg);
        int resp_result = req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0);
        fh->close();
        return resp_result;
    }
    ConfigureCurlCA(curl);
    curl_easy_setopt(curl, CURLOPT_URL, resource.c_str());

    Stream stream(std::move(fh), 0, 0, m_log);
    State state(0, stream, curl, true, req.tpcForwardCreds);
    state.SetupHeaders(req);

    return RunCurlWithUpdates(curl, req, state, rec);
}

/******************************************************************************/
/*            T P C H a n d l e r : : P r o c e s s P u l l R e q             */
/******************************************************************************/
  
int TPCHandler::ProcessPullReq(const std::string &resource, XrdHttpExtReq &req) {
    TPCLogRecord rec(req,TpcType::Pull);
    rec.log_prefix = "PullRequest";
    rec.local = req.resource;
    rec.remote = resource;
    rec.m_log = &m_log;
    char *name = req.GetSecEntity().name;
    req.GetClientID(rec.clID);
    if (name) rec.name = name;
    logTransferEvent(LogMask::Info, rec, "PULL_START", "Starting a pull request");

    ManagedCurlHandle curlPtr(curl_easy_init());
    auto curl = curlPtr.get();
    if (!curl) {
        std::stringstream ss;
        ss << "Failed to initialize internal transfer resources";
        rec.status = 500;
        logTransferEvent(LogMask::Error, rec, "PULL_FAIL", ss.str());
        return req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0);
    }
    // ddavila 2023-01-05:
    // The following change was required by the Rucio/SENSE project where
    // multiple IP addresses, each from a different subnet, are assigned to a
    // single server and routed differently by SENSE.
    // The above requires the server to utilize the same IP, that was used to
    // start the TPC, for the resolution of the given TPC instead of
    // using any of the IPs available.
    if (m_fixed_route){
        XrdNetAddr *nP;
        int numIP = 0;
        char buff[1024];
        char * ip;

        // Get the hostname used to contact the server from the http header
        auto host_header = XrdOucTUtils::caseInsensitiveFind(req.headers,"host");
        std::string host_used;
        if (host_header != req.headers.end()) {
            host_used = host_header->second;
        }

        // Get the IP addresses associated with the above hostname
        XrdNetUtils::GetAddrs(host_used.c_str(), &nP, numIP, XrdNetUtils::prefAuto, 0);
        int ip_size = nP[0].Format(buff, 1024, XrdNetAddrInfo::fmtAddr,XrdNetAddrInfo::noPort);
        ip = (char *)malloc(ip_size-1);

	// Substring to get only the address, remove brackets and garbage
        memcpy(ip, buff+1, ip_size-2);
        ip[ip_size-2]='\0';
        logTransferEvent(LogMask::Info, rec, "LOCAL IP", ip);

        curl_easy_setopt(curl, CURLOPT_INTERFACE, ip);
    }
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long) CURL_HTTP_VERSION_1_1);
//  curl_easy_setopt(curl,CURLOPT_SOCKOPTFUNCTION,sockopt_setcloexec_callback);
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, opensocket_callback);
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, &rec);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockopt_callback);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA , &rec);
    curl_easy_setopt(curl, CURLOPT_CLOSESOCKETFUNCTION, closesocket_callback);
    curl_easy_setopt(curl, CURLOPT_CLOSESOCKETDATA, &rec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT);
    std::unique_ptr<XrdSfsFile> fh(m_sfs->newFile(name, m_monid++));
    if (!fh.get()) {
        std::stringstream ss;
        ss << "Failed to initialize internal transfer file handle";
        rec.status = 500;
        logTransferEvent(LogMask::Error, rec, "PULL_FAIL", ss.str());
        return req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0);
    }
    auto query_header = XrdOucTUtils::caseInsensitiveFind(req.headers,"xrd-http-fullresource");
    std::string redirect_resource = req.resource;
    if (query_header != req.headers.end()) {
        redirect_resource = query_header->second;
    }
    XrdSfsFileOpenMode mode = SFS_O_CREAT;
    auto overwrite_header = XrdOucTUtils::caseInsensitiveFind(req.headers,"overwrite");
    if ((overwrite_header == req.headers.end()) || (overwrite_header->second == "T")) {
        if (! usingEC) mode = SFS_O_TRUNC;
    }
    int streams = 1;
    {
        auto streams_header = XrdOucTUtils::caseInsensitiveFind(req.headers,"x-number-of-streams");
        if (streams_header != req.headers.end()) {
            int stream_req = -1;
            try {
                stream_req = std::stol(streams_header->second);
            } catch (...) { // Handled below
            }
            if (stream_req < 0 || stream_req > 100) {
                std::stringstream ss;
                ss << "Invalid request for number of streams";
                rec.status = 400;
                logTransferEvent(LogMask::Info, rec, "INVALID_REQUEST", ss.str());
                return req.SendSimpleResp(rec.status, NULL, NULL, generateClientErr(ss, rec).c_str(), 0);
            }
            streams = stream_req == 0 ? 1 : stream_req;
        }
    }
    rec.streams = streams;
    std::string full_url = prepareURL(req);
    std::string authz = GetAuthz(req);
    curl_easy_setopt(curl, CURLOPT_URL, resource.c_str());
    ConfigureCurlCA(curl);
    uint64_t sourceFileContentLength = 0;
    {
        //Get the content-length of the source file and pass it to the OSS layer
        //during the open
        bool success;
        GetContentLengthTPCPull(curl, req, sourceFileContentLength, success, rec);
        if(success) {
            //In the case we cannot get the information from the source server (offline or other error)
            //we just don't add the size information to the opaque of the local file to open
            full_url += "&oss.asize=" + std::to_string(sourceFileContentLength);
        } else {
          // In the case the GetContentLength is not successful, an error will be returned to the client
          // just exit here so we don't open the file!
          return 0;
        }
    }
    int open_result = OpenWaitStall(*fh, full_url, mode|SFS_O_WRONLY,
                                    0644 | SFS_O_MKPTH,
                                    req.GetSecEntity(), authz);
    if (SFS_REDIRECT == open_result) {
        int result = RedirectTransfer(curl, redirect_resource, req, fh->error, rec);
        return result;
    } else if (SFS_OK != open_result) {
        int code;
        std::stringstream ss;
        const char *msg = fh->error.getErrText(code);
        if ((msg == NULL) || (*msg == '\0')) ss << "Failed to open local resource";
        else ss << msg;
        rec.status = 400;
        if (code == EACCES) rec.status = 401;
        else if (code == EEXIST) rec.status = 412;
        logTransferEvent(LogMask::Error, rec, "OPEN_FAIL", ss.str());
        int resp_result = req.SendSimpleResp(rec.status, NULL, NULL,
                                             generateClientErr(ss, rec).c_str(), 0);
        fh->close();
        return resp_result;
    }
    Stream stream(std::move(fh), streams * m_pipelining_multiplier, streams > 1 ? m_block_size : m_small_block_size, m_log);
    State state(0, stream, curl, false, req.tpcForwardCreds);
    state.SetupHeaders(req);
    state.SetContentLength(sourceFileContentLength);

    if (streams > 1) {
        return RunCurlWithStreams(req, state, streams, rec);
    } else {
        return RunCurlWithUpdates(curl, req, state, rec);
    }
}

/******************************************************************************/
/*          T P C H a n d l e r : : l o g T r a n s f e r E v e n t           */
/******************************************************************************/
  
void TPCHandler::logTransferEvent(LogMask mask, const TPCLogRecord &rec,
        const std::string &event, const std::string &message)
{
    if (!(m_log.getMsgMask() & mask)) {return;}

    std::stringstream ss;
    ss << "event=" << event << ", local=" << rec.local << ", remote=" << rec.remote;
    if (rec.name.empty())
       ss << ", user=(anonymous)";
    else
       ss << ", user=" << rec.name;
    if (rec.streams != 1)
       ss << ", streams=" << rec.streams;
    if (rec.bytes_transferred >= 0)
       ss << ", bytes_transferred=" << rec.bytes_transferred;
    if (rec.status >= 0)
       ss << ", status=" << rec.status;
    if (rec.tpc_status >= 0)
       ss << ", tpc_status=" << rec.tpc_status;
    if (!message.empty())
       ss << "; " << message;
    m_log.Log(mask, rec.log_prefix.c_str(), ss.str().c_str());
}

std::string TPCHandler::generateClientErr(std::stringstream &err_ss, const TPCLogRecord &rec, CURLcode cCode) {
  std::stringstream ssret;
  ssret << "failure: " << err_ss.str() << ", local=" << rec.local <<", remote=" << rec.remote;
  if(cCode != CURLcode::CURLE_OK) {
    ssret << ", HTTP library failure=" << curl_easy_strerror(cCode);
  }
  return ssret.str();
}
/******************************************************************************/
/*                  X r d H t t p G e t E x t H a n d l e r                   */
/******************************************************************************/
  
extern "C" {

XrdHttpExtHandler *XrdHttpGetExtHandler(XrdSysError *log, const char * config, const char * /*parms*/, XrdOucEnv *myEnv) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT)) {
        log->Emsg("TPCInitialize", "libcurl failed to initialize");
        return NULL;
    }

    TPCHandler *retval{NULL};
    if (!config) {
        log->Emsg("TPCInitialize", "TPC handler requires a config filename in order to load");
        return NULL;
    }
    try {
        log->Emsg("TPCInitialize", "Will load configuration for the TPC handler from", config);
        retval = new TPCHandler(log, config, myEnv);
    } catch (std::runtime_error &re) {
        log->Emsg("TPCInitialize", "Encountered a runtime failure when loading ", re.what());
        //printf("Provided env vars: %p, XrdInet*: %p\n", myEnv, myEnv->GetPtr("XrdInet*"));
    }
    return retval;
}

}
