#undef NDEBUG

#include "XrdHttp/XrdHttpReq.hh"
#include "XrdHttp/XrdHttpProtocol.hh"
#include "XrdHttp/XrdHttpChecksumHandler.hh"
#include <exception>
#include <gtest/gtest.h>
#include <string>
#include <sstream>
#include "XrdHttp/XrdHttpContentRangeParser.hh"


using namespace testing;

class XrdHttpTests : public Test {};

TEST(XrdHttpTests, checksumHandlerTests) {
    {
        XrdHttpChecksumHandlerImpl handler;
        handler.configure("0:sha512,1:crc32");
        auto configuredChecksum = handler.getConfiguredChecksums();
        ASSERT_EQ(2, configuredChecksum.size());
        ASSERT_EQ("sha512", configuredChecksum[0]->getXRootDConfigDigestName());
        ASSERT_EQ("crc32", configuredChecksum[1]->getXRootDConfigDigestName());
    }
    {
        XrdHttpChecksumHandlerImpl handler;
        handler.configure("0:sha512,1:crc32,2:does_not_exist");
        auto configuredChecksum = handler.getConfiguredChecksums();
        auto incompatibleChecksums = handler.getNonIANAConfiguredCksums();
        ASSERT_EQ(1,incompatibleChecksums.size());
        ASSERT_EQ("does_not_exist",incompatibleChecksums[0]);
        ASSERT_EQ(2,configuredChecksum.size());
    }
}

TEST(XrdHttpTests, checksumHandlerSelectionTest) {
    {
        //one algorithm, HTTP-IANA compatible
        std::string reqDigest = "adler32";
        const char *configChecksumList = "0:adler32";
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        auto cksumToRun = handler.getChecksumToRun(reqDigest);
        ASSERT_EQ("adler32",cksumToRun->getXRootDConfigDigestName());
    }
    {
        //sha-512, same as sha512, it is HTTP-IANA compatible
        std::string reqDigest = "sha-512";
        const char *configChecksumList = "0:sha-512";
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        auto cksumToRun = handler.getChecksumToRun(reqDigest);
        ASSERT_EQ("sha-512",cksumToRun->getXRootDConfigDigestName());
        ASSERT_EQ("sha-512",cksumToRun->getHttpName());
        ASSERT_EQ(true,cksumToRun->needsBase64Padding());
    }
    {
        //UNIXCksum
        std::string reqDigest = "UNIXcksum";
        {
            const char *configChecksumList = "0:cksum";
            XrdHttpChecksumHandlerImpl handler;
            handler.configure(configChecksumList);
            auto cksumToRun = handler.getChecksumToRun(reqDigest);
            ASSERT_EQ("cksum",cksumToRun->getXRootDConfigDigestName());
            ASSERT_EQ("UNIXcksum",cksumToRun->getHttpName());
        }
        {
            const char *configChecksumList = "0:unixcksum";
            XrdHttpChecksumHandlerImpl handler;
            handler.configure(configChecksumList);
            auto cksumToRun = handler.getChecksumToRun(reqDigest);
            ASSERT_EQ("unixcksum",cksumToRun->getXRootDConfigDigestName());
            ASSERT_EQ("UNIXcksum",cksumToRun->getHttpName());
            ASSERT_FALSE(cksumToRun->needsBase64Padding());
        }
    }
    {
        //Multiple HTTP-IANA compatible checksum configured, the
        //checksum returned should be the first appearing in the reqDigest
        std::string reqDigest = "crc32,adler32";
        const char *configChecksumList = "0:adler32,1:crc32";
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        auto cksumToRun = handler.getChecksumToRun(reqDigest);
        ASSERT_EQ("crc32",cksumToRun->getXRootDConfigDigestName());
    }
    {
        // If the requested digest does not exist, the first configured HTTP-IANA
        // compatible checksum will be ran
        std::string reqDigest = "DOES_NOT_EXIST";
        const char *configChecksumList = "0:does_not_exist_algo,1:crc32,2:adler32";
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        auto cksumToRun = handler.getChecksumToRun(reqDigest);
        ASSERT_EQ("crc32",cksumToRun->getXRootDConfigDigestName());
    }
    {
        // If the requested digest contains at least one HTTP-IANA compatible
        // digest, then the HTTP-IANA compatible digest will be returned
        std::string reqDigest = "DOES_NOT_EXIST , crc32";
        const char *configChecksumList = "0:adler32,1:crc32";
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        auto cksumToRun = handler.getChecksumToRun(reqDigest);
        ASSERT_EQ("crc32",cksumToRun->getXRootDConfigDigestName());
    }
    {
        //Ensure weighted digest (;q=xx) are discarded but still allows to get the correct algorithm
        //depending on the order of submission
        std::string reqDigest = "crc32;q=0.1,adler32;q=0.5";
        const char *configChecksumList = "0:adler32,1:crc32";
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        auto cksumToRun = handler.getChecksumToRun(reqDigest);
        ASSERT_EQ("crc32",cksumToRun->getXRootDConfigDigestName());
    }
    {
        //sha-* algorithms
        std::string reqDigest = "SHA-512";
        const char *configChecksumList = "0:crc32,1:sha512,2:sha-256";
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        {
            auto cksumToRun = handler.getChecksumToRun(reqDigest);
            ASSERT_EQ("sha512",cksumToRun->getXRootDConfigDigestName());
        }
        {
            reqDigest = "sha512";
            auto cksumToRun = handler.getChecksumToRun(reqDigest);
            ASSERT_EQ("crc32",cksumToRun->getXRootDConfigDigestName());
            ASSERT_FALSE(cksumToRun->needsBase64Padding());
        }
        {
            reqDigest = "sha-256";
            auto cksumToRun = handler.getChecksumToRun(reqDigest);
            ASSERT_EQ("sha-256",cksumToRun->getXRootDConfigDigestName());
            ASSERT_EQ("sha-256",cksumToRun->getHttpName());
            ASSERT_TRUE(cksumToRun->needsBase64Padding());
        }
    }
    {
        //one sha-512 HTTP configured algorithm
        std::string reqDigest = "SHA-512";
        const char *configChecksumList = "0:my_custom_sha512,1:second_custom_sha512,2:sha-512";
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        ASSERT_EQ("sha-512", handler.getChecksumToRun(reqDigest)->getXRootDConfigDigestName());
        ASSERT_EQ("sha-512", handler.getChecksumToRun("adler32")->getXRootDConfigDigestName());
    }
    {
        //algorithm configured but none is compatible with HTTP
        std::string reqDigest = "SHA-512";
        const char *configChecksumList = "0:my_custom_sha512,1:second_custom_sha512";
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        ASSERT_EQ(nullptr, handler.getChecksumToRun(reqDigest));
    }
    {
        // no algorithm configured, should always return a nullptr
        std::string reqDigest = "SHA-512";
        const char *configChecksumList = nullptr;
        XrdHttpChecksumHandlerImpl handler;
        handler.configure(configChecksumList);
        ASSERT_EQ(nullptr, handler.getChecksumToRun(reqDigest));
    }
}

TEST(XrdHttpTests, xrdHttpReqParseContentRangeTwoRangesOfSizeEqualToMaxChunkSize) {
    long long length = 0;
    int rangeBegin = 0;
    int rangeEnd = 3;
    int rangeBegin2 = 4;
    int rangeEnd2 = 7;
    int readvMaxChunkSize = 4;
    std::stringstream ss;
    ss << "bytes=" << rangeBegin << "-" << rangeEnd << ", " << rangeBegin2 << "-" << rangeEnd2;
    char * range = const_cast<char *>(ss.str().c_str());
    std::vector<ReadWriteOp> rwOps, rwOps_split;
    XrdHttpContentRangeParser parser(readvMaxChunkSize,0,rwOps,rwOps_split,length);
    parser.parseContentRange(range);
    ASSERT_EQ(2,rwOps_split.size());
    ASSERT_EQ(0, rwOps_split[0].bytestart);
    ASSERT_EQ(3, rwOps_split[0].byteend);
    ASSERT_EQ(4, rwOps_split[1].bytestart);
    ASSERT_EQ(7, rwOps_split[1].byteend);
    ASSERT_EQ(2,rwOps.size());
    ASSERT_EQ(8,length);
}

TEST(XrdHttpTests, xrdHttpReqParseContentRangeOneRangeSizeLessThanMaxChunkSize) {
  long long length = 0;
  int rangeBegin = 0;
  int rangeEnd = 3;
  int readvMaxChunkSize = 5;
  std::stringstream ss;
  ss << "bytes=" << rangeBegin << "-" << rangeEnd;
  char * range = const_cast<char *>(ss.str().c_str());
  std::vector<ReadWriteOp> rwOps, rwOps_split;
  XrdHttpContentRangeParser parser(readvMaxChunkSize,0,rwOps,rwOps_split,length);
  parser.parseContentRange(range);
  ASSERT_EQ(1,rwOps_split.size());
  ASSERT_EQ(0, rwOps_split[0].bytestart);
  ASSERT_EQ(3, rwOps_split[0].byteend);
  ASSERT_EQ(1,rwOps.size());
  ASSERT_EQ(4,length);
}

TEST(XrdHttpTests, xrdHttpReqParseContentRangeOneRangeSizeGreaterThanMaxChunkSize) {
  long long length = 0;
  int rangeBegin = 0;
  int rangeEnd = 7;
  int readvMaxChunkSize = 3;
  std::stringstream ss;
  ss << "bytes=" << rangeBegin << "-" << rangeEnd;
  char * range = const_cast<char *>(ss.str().c_str());
  std::vector<ReadWriteOp> rwOps, rwOps_split;
  XrdHttpContentRangeParser parser(readvMaxChunkSize,0,rwOps,rwOps_split,length);
  parser.parseContentRange(range);
  ASSERT_EQ(3,rwOps_split.size());
  ASSERT_EQ(0, rwOps_split[0].bytestart);
  ASSERT_EQ(2, rwOps_split[0].byteend);
  ASSERT_EQ(3, rwOps_split[1].bytestart);
  ASSERT_EQ(5, rwOps_split[1].byteend);
  ASSERT_EQ(6, rwOps_split[2].bytestart);
  ASSERT_EQ(7, rwOps_split[2].byteend);
  ASSERT_EQ(1,rwOps.size());
  ASSERT_EQ(8,length);
}

TEST(XrdHttpTests, xrdHttpReqParseContentRangeRange0ToEnd) {
  long long length = 0;
  int rangeBegin = 0;
  int readvMaxChunkSize = 4;
  std::stringstream ss;
  ss << "bytes=" << rangeBegin << "-" << "\r";
  char * range = const_cast<char *>(ss.str().c_str());
  std::vector<ReadWriteOp> rwOps, rwOps_split;
  XrdHttpContentRangeParser parser(readvMaxChunkSize,0,rwOps,rwOps_split,length);
  parser.parseContentRange(range);
  ASSERT_EQ(0,rwOps_split.size());
  ASSERT_EQ(1,rwOps.size());
  ASSERT_EQ(0,rwOps[0].bytestart);
  ASSERT_EQ(-1,rwOps[0].byteend);
  ASSERT_EQ(0,length);
}

TEST(XrdHttpTests, xrdHttpReqParseContentRangeRange0To0) {
  long long length = 0;
  int rangeBegin = 0;
  int rangeEnd = 0;
  int readvMaxChunkSize = 4;
  std::stringstream ss;
  ss << "bytes=" << rangeBegin << "-" << rangeEnd;
  char * range = const_cast<char *>(ss.str().c_str());
  std::vector<ReadWriteOp> rwOps, rwOps_split;
  XrdHttpContentRangeParser parser(readvMaxChunkSize,0,rwOps,rwOps_split,length);
  parser.parseContentRange(range);
  ASSERT_EQ(1,rwOps_split.size());
  ASSERT_EQ(1,rwOps.size());
  ASSERT_EQ(0,rwOps[0].bytestart);
  ASSERT_EQ(0,rwOps[0].byteend);
  ASSERT_EQ(1,length);
}

TEST(XrdHttpTests, xrdHttpReqParseContentRangeRangeEndByteGreaterThanFileSize) {
  long long length = 0;
  long long fileSize = 2;
  int rangeBegin = 0;
  int rangeEnd = 4;
  int readvMaxChunkSize = 10;
  std::stringstream ss;
  ss << "bytes=" << rangeBegin << "-" << rangeEnd;
  char * range = const_cast<char *>(ss.str().c_str());
  std::vector<ReadWriteOp> rwOps, rwOps_split;
  XrdHttpContentRangeParser parser(readvMaxChunkSize,fileSize, rwOps,rwOps_split,length);
  ASSERT_EQ(0,parser.parseContentRange(range));
  ASSERT_EQ(1,rwOps_split.size());
  ASSERT_EQ(0,rwOps_split[0].bytestart);
  ASSERT_EQ(1,rwOps_split[0].byteend);
  ASSERT_EQ(1,rwOps.size());
  ASSERT_EQ(0,rwOps[0].bytestart);
  ASSERT_EQ(4,rwOps[0].byteend);
  ASSERT_EQ(2,length);
}

TEST(XrdHttpTests, xrdHttpReqParseContentRangeRangeBeginGreaterThanFileSize) {
  long long length = 0;
  long long fileSize = 2;
  int rangeBegin = 4;
  int rangeEnd = 6;
  int readvMaxChunkSize = 10;
  std::stringstream ss;
  ss << "bytes=" << rangeBegin << "-" << rangeEnd;
  char * range = const_cast<char *>(ss.str().c_str());
  std::vector<ReadWriteOp> rwOps, rwOps_split;
  XrdHttpContentRangeParser parser(readvMaxChunkSize,fileSize, rwOps,rwOps_split,length);
  ASSERT_NE(0,parser.parseContentRange(range));
  ASSERT_EQ(400,parser.getError().httpRetCode);
  ASSERT_EQ(0,rwOps_split.size());
  ASSERT_EQ(1,rwOps.size());
  ASSERT_EQ(0,length);
}