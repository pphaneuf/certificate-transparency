#include <glog/logging.h>
#include <gtest/gtest.h>

#include "net/url_fetcher.h"
#include "util/libevent_wrapper.h"
#include "util/sync_task.h"
#include "util/testing.h"

namespace libevent = cert_trans::libevent;

using cert_trans::URL;
using cert_trans::UrlFetcher;
using std::make_shared;
using std::shared_ptr;
using std::unique_ptr;
using util::SyncTask;


int main(int argc, char** argv) {
  cert_trans::test::InitTesting(argv[0], &argc, &argv, true);
  google::InstallFailureSignalHandler();

  shared_ptr<libevent::Base> event_base(make_shared<libevent::Base>());
  libevent::EventPumpThread pump(event_base);
  UrlFetcher fetcher(event_base.get());

  URL url("http://www.google.com/?hello=foo");
  LOG(INFO) << "protocol: " << url.Protocol();
  LOG(INFO) << "host: " << url.Host();
  LOG(INFO) << "port: " << url.Port();
  LOG(INFO) << "path: " << url.Path();
  LOG(INFO) << "query: " << url.Query();
  LOG(INFO) << "PathQuery: " << url.PathQuery();

  UrlFetcher::Request req(url);
  UrlFetcher::Response resp;
  SyncTask sync(event_base.get());
  fetcher.Fetch(req, &resp, sync.task());
  sync.Wait();

  LOG(INFO) << "fetch status: " << sync.status();
#if 1
  LOG(INFO) << "response:\n" << resp;
#else
  LOG(INFO) << "status code: " << resp.status_code;
#if 1
  for (const auto& header : resp.headers) {
    LOG(INFO) << header.first << ": " << header.second;
  }
#endif
  LOG(INFO) << "body:\n" << resp.body;
#endif

  return 0;

#if 0
  return RUN_ALL_TESTS();
#endif
}
