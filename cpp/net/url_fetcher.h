#ifndef CERT_TRANS_NET_URL_FETCHER_H_
#define CERT_TRANS_NET_URL_FETCHER_H_

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <ostream>

#include "base/macros.h"
#include "net/url.h"
#include "util/compare.h"
#include "util/task.h"

namespace cert_trans {

namespace libevent {
class Base;
}


class UrlFetcher {
 public:
  typedef std::multimap<std::string, std::string, ci_less<std::string>>
      Headers;

  enum class Verb {
    GET,
    POST,
    PUT,
    DELETE,
  };

  struct Request {
    Request() : verb(Verb::GET) {
    }
    Request(const URL& input_url) : verb(Verb::GET), url(input_url) {
    }

    Verb verb;
    URL url;
    Headers headers;
    std::string body;

    // This is an absolute deadline, which will hold no matter if the
    // request is retried or redirects are followed.
    std::chrono::steady_clock::time_point deadline;
  };

  struct Response {
    Response() : status_code(0) {
    }

    int status_code;
    Headers headers;
    std::string body;
  };

  UrlFetcher(libevent::Base* base);
  virtual ~UrlFetcher();

  // If the status on the task is not OK, the response will be in an
  // undefined state. If it is OK, it only means that the transaction
  // with the remote server went correctly, you should still check
  // Response::status_code.
  virtual void Fetch(const Request& req, Response* resp, util::Task* task);

 protected:
  UrlFetcher();

 private:
  struct Impl;
  const std::unique_ptr<Impl> impl_;

  DISALLOW_COPY_AND_ASSIGN(UrlFetcher);
};


std::ostream& operator<<(std::ostream& output,
                         const UrlFetcher::Response& resp);


}  // namespace cert_trans

#endif  // CERT_TRANS_NET_URL_FETCHER_H_
