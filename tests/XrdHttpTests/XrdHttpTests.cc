#undef NDEBUG

#include "XrdHttp/XrdHttpReq.hh"
#include "XrdHttp/XrdHttpProtocol.hh"

#include <gtest/gtest.h>

using namespace testing;

class XrdHttpTests : public Test {};

TEST(XrdHttpTests,selectChecksumTest) {
    {
        std::string reqDigest = "adler32";
        const char *configChecksumList = "0:adler32";
        std::string outputChecksum;
        XrdHttpReq::selectXRootDChecksum(configChecksumList, reqDigest, outputChecksum);
        ASSERT_EQ("adler32",outputChecksum);
    }
    {
        std::string reqDigest = "crc32,adler32";
        const char *configChecksumList = "0:adler32,1:crc32";
        std::string outputChecksum;
        XrdHttpReq::selectXRootDChecksum(configChecksumList, reqDigest, outputChecksum);
        ASSERT_EQ("crc32",outputChecksum);
    }
    {
        std::string reqDigest = "DOES_NOT_EXIST";
        const char *configChecksumList = "0:adler32,1:crc32";
        std::string outputChecksum;
        XrdHttpReq::selectXRootDChecksum(configChecksumList, reqDigest, outputChecksum);
        ASSERT_EQ("adler32",outputChecksum);
    }
    {
        std::string reqDigest = "DOES_NOT_EXIST,crc32";
        const char *configChecksumList = "0:adler32,1:crc32";
        std::string outputChecksum;
        XrdHttpReq::selectXRootDChecksum(configChecksumList, reqDigest, outputChecksum);
        ASSERT_EQ("crc32",outputChecksum);
    }
    {
        //Ensure weighted digest (;q=xx) are discarded but still allows to get the correct algorithm
        //depending on the order of submission
        std::string reqDigest = "crc32;q=0.1,adler32;q=0.5";
        const char *configChecksumList = "0:adler32,1:crc32";
        std::string outputChecksum;
        XrdHttpReq::selectXRootDChecksum(configChecksumList, reqDigest, outputChecksum);
        ASSERT_EQ("crc32",outputChecksum);
    }
    {
        //Ensure weighted digest (;q=xx) are discarded but still allows to get the correct algorithm
        //depending on the order of submission
        std::string reqDigest = "crc32;q=0.1,adler32;q=0.5";
        const char *configChecksumList = "0:adler32,1:crc32";
        std::string outputChecksum;
        XrdHttpReq::selectXRootDChecksum(configChecksumList, reqDigest, outputChecksum);
        ASSERT_EQ("crc32",outputChecksum);
    }
    {
        //sha-* algorithms
        std::string reqDigest = "SHA-512";
        const char *configChecksumList = "0:crc32,1:sha512";
        std::string outputChecksum;
        XrdHttpReq::selectXRootDChecksum(configChecksumList, reqDigest, outputChecksum);
        ASSERT_EQ("sha512",outputChecksum);
    }

    {
        //no algorithm configured
        std::string reqDigest = "SHA-512";
        std::string outputChecksum;
        XrdHttpReq::selectXRootDChecksum(nullptr, reqDigest, outputChecksum);
        ASSERT_EQ("unknown",outputChecksum);
    }
}