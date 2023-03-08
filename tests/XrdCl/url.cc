#undef NDEBUG

#include <XrdCl/XrdClURL.hh>

#include <gtest/gtest.h>

using namespace testing;

class UtilsTest : public Test {};

TEST(UtilsTest, URLTest) {
  XrdCl::URL url1("root://user1:passwd1@host1:123//path?param1=val1&param2=val2");
  XrdCl::URL url2("root://user1@host1//path?param1=val1&param2=val2");
  XrdCl::URL url3("root://host1");
  XrdCl::URL url4("root://user1:passwd1@[::1]:123//path?param1=val1&param2=val2");
  XrdCl::URL url5("root://user1@192.168.1.1:123//path?param1=val1&param2=val2");
  XrdCl::URL url6("root://[::1]");
  XrdCl::URL url7(
      "root://lxfsra02a08.cern.ch:1095//eos/dev/"
      "SMWZd3pdExample_NTUP_SMWZ.526666._000073.root.1?&cap.sym="
      "sfdDqALWo3W3tWUJ2O5XwQ5GG8U=&cap.msg=eGj/mh+9TrecFBAZBNr/"
      "nLau4p0kjlEOjc1JC+9DVjLL1Tq+g311485W0baMBAsM#"
      "W8lNFdVQcKNAu8K5yVskIcLDOEi6oNpvoxDA1DN4oCxtHR6LkOWhO91MLn/"
      "ZosJ5#Dc7aeBCIz/"
      "kKs261mnL4dJeUu6r25acCn4vhyp8UKyL1cVmmnyBnjqe6tz28qFO2#"
      "0fQHrHf6Z9N0MNhw1fplYjpGeNwFH2jQSfSo24zSZKGa/"
      "PKClGYnXoXBWDGU1spm#kJsGGrErhBHYvLq3eS+jEBr8l+c1BhCQU7ZaLZiyaKOnspYnR/"
      "Tw7bMrooWMh7eL#&mgm.logid=766877e6-9874-11e1-a77f-003048cf8cd8&mgm."
      "recdcdcdcdplicaindex=0&mgm.replicahead=0");
  XrdCl::URL url8("/etc/passwd");
  XrdCl::URL url9("localhost:1094//data/cb4aacf1-6f28-42f2-b68a-90a73460f424.dat");
  XrdCl::URL url10("localhost:1094/?test=123");

  XrdCl::URL urlInvalid1("root://user1:passwd1@host1:asd//path?param1=val1&param2=val2");
  XrdCl::URL urlInvalid2("root://user1:passwd1host1:123//path?param1=val1&param2=val2");
  XrdCl::URL urlInvalid3("root:////path?param1=val1&param2=val2");
  XrdCl::URL urlInvalid4("root://@//path?param1=val1&param2=val2");
  XrdCl::URL urlInvalid5("root://:@//path?param1=val1&param2=val2");
  XrdCl::URL urlInvalid6("root://");
  XrdCl::URL urlInvalid7("://asds");
  XrdCl::URL urlInvalid8("root://asd@://path?param1=val1&param2=val2");

  //----------------------------------------------------------------------------
  // Full url
  //----------------------------------------------------------------------------
  EXPECT_EQ(url1.IsValid(), true);
  EXPECT_EQ(url1.GetProtocol(), "root");
  EXPECT_EQ(url1.GetUserName(), "user1");
  EXPECT_EQ(url1.GetPassword(), "passwd1");
  EXPECT_EQ(url1.GetHostName(), "host1");
  EXPECT_EQ(url1.GetPort(), 123);
  EXPECT_EQ(url1.GetPathWithParams(), "/path?param1=val1&param2=val2");
  EXPECT_EQ(url1.GetPath(), "/path");
  EXPECT_EQ(url1.GetParams().size(), 2);

  XrdCl::URL::ParamsMap::const_iterator it;
  it = url1.GetParams().find("param1");
  EXPECT_NE(it, url1.GetParams().end());
  EXPECT_EQ(it->second, "val1");
  it = url1.GetParams().find("param2");
  EXPECT_NE(it, url1.GetParams().end());
  EXPECT_EQ(it->second, "val2");
  it = url1.GetParams().find("param3");
  EXPECT_EQ(it, url1.GetParams().end());

  //----------------------------------------------------------------------------
  // No password, no port
  //----------------------------------------------------------------------------
  EXPECT_EQ(url2.IsValid(), true);
  EXPECT_EQ(url2.GetProtocol(), "root");
  EXPECT_EQ(url2.GetUserName(), "user1");
  EXPECT_EQ(url2.GetPassword(), "");
  EXPECT_EQ(url2.GetHostName(), "host1");
  EXPECT_EQ(url2.GetPort(), 1094);
  EXPECT_EQ(url2.GetPath(), "/path");
  EXPECT_EQ(url2.GetPathWithParams(), "/path?param1=val1&param2=val2");
  EXPECT_EQ(url1.GetParams().size(), 2);

  it = url2.GetParams().find("param1");
  EXPECT_NE(it, url2.GetParams().end());
  EXPECT_EQ(it->second, "val1");
  it = url2.GetParams().find("param2");
  EXPECT_NE(it, url2.GetParams().end());
  EXPECT_EQ(it->second, "val2");
  it = url2.GetParams().find("param3");
  EXPECT_EQ(it, url2.GetParams().end());

  //----------------------------------------------------------------------------
  // Just the host and the protocol
  //----------------------------------------------------------------------------
  EXPECT_EQ(url3.IsValid(), true);
  EXPECT_EQ(url3.GetProtocol(), "root");
  EXPECT_EQ(url3.GetUserName(), "");
  EXPECT_EQ(url3.GetPassword(), "");
  EXPECT_EQ(url3.GetHostName(), "host1");
  EXPECT_EQ(url3.GetPort(), 1094);
  EXPECT_EQ(url3.GetPath(), "");
  EXPECT_EQ(url3.GetPathWithParams(), "");
  EXPECT_EQ(url3.GetParams().size(), 0);

  //----------------------------------------------------------------------------
  // Full url - IPv6
  //----------------------------------------------------------------------------
  EXPECT_EQ(url4.IsValid(), true);
  EXPECT_EQ(url4.GetProtocol(), "root");
  EXPECT_EQ(url4.GetUserName(), "user1");
  EXPECT_EQ(url4.GetPassword(), "passwd1");
  EXPECT_EQ(url4.GetHostName(), "[::1]");
  EXPECT_EQ(url4.GetPort(), 123);
  EXPECT_EQ(url4.GetPathWithParams(), "/path?param1=val1&param2=val2");
  EXPECT_EQ(url4.GetPath(), "/path");
  EXPECT_EQ(url4.GetParams().size(), 2);

  it = url4.GetParams().find("param1");
  EXPECT_NE(it, url4.GetParams().end());
  EXPECT_EQ(it->second, "val1");
  it = url4.GetParams().find("param2");
  EXPECT_NE(it, url4.GetParams().end());
  EXPECT_EQ(it->second, "val2");
  it = url4.GetParams().find("param3");
  EXPECT_EQ(it, url4.GetParams().end());

  //----------------------------------------------------------------------------
  // No password, no port
  //----------------------------------------------------------------------------
  EXPECT_EQ(url5.IsValid(), true);
  EXPECT_EQ(url5.GetProtocol(), "root");
  EXPECT_EQ(url5.GetUserName(), "user1");
  EXPECT_EQ(url5.GetPassword(), "");
  EXPECT_EQ(url5.GetHostName(), "192.168.1.1");
  EXPECT_EQ(url5.GetPort(), 123);
  EXPECT_EQ(url5.GetPath(), "/path");
  EXPECT_EQ(url5.GetPathWithParams(), "/path?param1=val1&param2=val2");
  EXPECT_EQ(url5.GetParams().size(), 2);

  it = url5.GetParams().find("param1");
  EXPECT_NE(it, url5.GetParams().end());
  EXPECT_EQ(it->second, "val1");
  it = url5.GetParams().find("param2");
  EXPECT_NE(it, url2.GetParams().end());
  EXPECT_EQ(it->second, "val2");
  it = url5.GetParams().find("param3");
  EXPECT_EQ(it, url5.GetParams().end());

  //----------------------------------------------------------------------------
  // Just the host and the protocol
  //----------------------------------------------------------------------------
  EXPECT_EQ(url6.IsValid(), true);
  EXPECT_EQ(url6.GetProtocol(), "root");
  EXPECT_EQ(url6.GetUserName(), "");
  EXPECT_EQ(url6.GetPassword(), "");
  EXPECT_EQ(url6.GetHostName(), "[::1]");
  EXPECT_EQ(url6.GetPort(), 1094);
  EXPECT_EQ(url6.GetPath(), "");
  EXPECT_EQ(url6.GetPathWithParams(), "");
  EXPECT_EQ(url6.GetParams().size(), 0);

  //----------------------------------------------------------------------------
  // Local file
  //----------------------------------------------------------------------------
  EXPECT_EQ(url8.IsValid(), true);
  EXPECT_EQ(url8.GetProtocol(), "file");
  EXPECT_EQ(url8.GetUserName(), "");
  EXPECT_EQ(url8.GetPassword(), "");
  EXPECT_EQ(url8.GetHostName(), "localhost");
  EXPECT_EQ(url8.GetPath(), "/etc/passwd");
  EXPECT_EQ(url8.GetHostId(), "localhost");
  EXPECT_EQ(url8.GetPathWithParams(), "/etc/passwd");
  EXPECT_EQ(url8.GetParams().size(), 0);

  //----------------------------------------------------------------------------
  // URL without a protocol spec
  //----------------------------------------------------------------------------
  EXPECT_EQ(url9.IsValid(), true);
  EXPECT_EQ(url9.GetProtocol(), "root");
  EXPECT_EQ(url9.GetUserName(), "");
  EXPECT_EQ(url9.GetPassword(), "");
  EXPECT_EQ(url9.GetHostName(), "localhost");
  EXPECT_EQ(url9.GetPort(), 1094);
  EXPECT_EQ(url9.GetPath(),
               "/data/cb4aacf1-6f28-42f2-b68a-90a73460f424.dat");
  EXPECT_EQ(url9.GetPathWithParams(),
               "/data/cb4aacf1-6f28-42f2-b68a-90a73460f424.dat");
  EXPECT_EQ(url9.GetParams().size(), 0);

  //----------------------------------------------------------------------------
  // URL cgi without path
  //----------------------------------------------------------------------------
  EXPECT_EQ(url10.IsValid(), true);
  EXPECT_EQ(url10.GetProtocol(), "root");
  EXPECT_EQ(url10.GetUserName(), "");
  EXPECT_EQ(url10.GetPassword(), "");
  EXPECT_EQ(url10.GetHostName(), "localhost");
  EXPECT_EQ(url10.GetPort(), 1094);
  EXPECT_EQ(url10.GetPath(), "");

  EXPECT_EQ(url10.GetParams().size(), 1);
  EXPECT_EQ(url10.GetParamsAsString(), "?test=123");

  //----------------------------------------------------------------------------
  // Bunch od invalid ones
  //----------------------------------------------------------------------------
  EXPECT_EQ(urlInvalid1.IsValid(), false);
  EXPECT_EQ(urlInvalid2.IsValid(), false);
  EXPECT_EQ(urlInvalid3.IsValid(), false);
  EXPECT_EQ(urlInvalid4.IsValid(), false);
  EXPECT_EQ(urlInvalid5.IsValid(), false);
  EXPECT_EQ(urlInvalid6.IsValid(), false);
  EXPECT_EQ(urlInvalid7.IsValid(), false);
  EXPECT_EQ(urlInvalid8.IsValid(), false);
}
