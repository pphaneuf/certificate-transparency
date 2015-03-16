#ifndef CERT_TRANS_NET_MOCK_URL_FETCHER_H_
#define CERT_TRANS_NET_MOCK_URL_FETCHER_H_

#include <gmock/gmock.h>

#include "net/url_fetcher.h"

namespace cert_trans {


class MockUrlFetcher : public UrlFetcher {
 public:
  MOCK_METHOD3(Fetch,
               void(const Request& req, Response* resp, util::Task* task));
};


#if 0
class UrlFetchRequestMatcher
    : public testing::MatcherInterface<UrlFetcher::Request> {
 public:
  UrlFetchRequestMatcher(const testing::Matcher<UrlFetcher::Verb>& verb,
                         const testing::Matcher<URL>& url,
                         const testing::Matcher<UrlFetcher::Headers>& headers,
                         const testing::Matcher<std::string>& body)
      : verb_(verb), url_(url), headers_(headers), body_(body) {
  }

  bool MatchAndExplain(UrlFetcher::Request req,
                       testing::MatchResultListener* listener) const override;
  void DescribeTo(::std::ostream* os) const override;

 private:
  const testing::Matcher<UrlFetcher::Verb> verb_;
  const testing::Matcher<URL>& url_;
  const testing::Matcher<UrlFetcher::Headers> headers_;
  const testing::Matcher<std::string> body_;
};
#endif


inline testing::Matcher<const UrlFetcher::Request&> IsUrlFetchRequest(
    const testing::Matcher<UrlFetcher::Verb>& verb,
    const testing::Matcher<URL>& url,
    const testing::Matcher<UrlFetcher::Headers>& headers,
    const testing::Matcher<std::string>& body) {
  // TODO(pphaneuf): We should make our own matcher with an output
  // that is easier to read, and also support form submissions.
  return testing::AllOf(testing::Field(&UrlFetcher::Request::verb, verb),
                        testing::Field(&UrlFetcher::Request::url, url),
                        testing::Field(&UrlFetcher::Request::headers, headers),
                        testing::Field(&UrlFetcher::Request::body, body));
}


}  // namespace cert_trans

#endif  // CERT_TRANS_NET_MOCK_URL_FETCHER_H_
