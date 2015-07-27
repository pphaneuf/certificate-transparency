#include <event2/thread.h>
#include <functional>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <memory>

#include "base/notification.h"
#include "client/async_log_client.h"
#include "log/cert.h"
#include "log/ct_extensions.h"
#include "log/logged_certificate.h"
#include "merkletree/tree_hasher.h"
#include "net/url_fetcher.h"
#include "util/libevent_wrapper.h"
#include "util/thread_pool.h"
#include "util/util.h"

namespace libevent = cert_trans::libevent;

using cert_trans::AsyncLogClient;
using cert_trans::Cert;
using cert_trans::LoggedCertificate;
using cert_trans::Notification;
using cert_trans::PreCertChain;
using cert_trans::ThreadPool;
using cert_trans::UrlFetcher;
using ct::SignedCertificateTimestamp;
using std::make_shared;
using std::placeholders::_1;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;


const int64_t kEntry = 42;


void Done(Notification* done, AsyncLogClient::Status* save,
          AsyncLogClient::Status status) {
  *CHECK_NOTNULL(save) = status;
  CHECK_NOTNULL(done)->Notify();
}


int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  evthread_use_pthreads();
  evhtp_ssl_use_threads();
  OpenSSL_add_all_algorithms();
  ERR_load_BIO_strings();
  ERR_load_crypto_strings();
  SSL_load_error_strings();
  SSL_library_init();

  cert_trans::LoadCtExtensions();

  const shared_ptr<libevent::Base> event_base(make_shared<libevent::Base>());
  libevent::EventPumpThread pump(event_base);
  ThreadPool pool;
  UrlFetcher fetcher(event_base.get(), &pool);
  AsyncLogClient client(event_base.get(), &fetcher,
                        "http://ct.googleapis.com/testtube");

  Notification done_get;
  vector<AsyncLogClient::Entry> entries;
  AsyncLogClient::Status status;
  client.GetEntries(kEntry, kEntry, &entries, bind(Done, &done_get, &status, _1));
  done_get.WaitForNotification();

  CHECK_EQ(status, AsyncLogClient::OK);
  CHECK_EQ(entries.size(), 1);

  LoggedCertificate cert;
  CHECK(cert.CopyFromClientLogEntry(entries.front()));
  //CHECK_EQ(cert.entry().type(), ct::PRECERT_ENTRY);

  string leaf;
  CHECK(cert.SerializeForLeaf(&leaf));
  const string leafhash(TreeHasher(new Sha256Hasher).HashLeaf(leaf));

  LOG(INFO) << "entry:\n" << cert.entry().DebugString();
  LOG(INFO) << "hash: " << util::ToBase64(leafhash);
  LOG(INFO) << "hash: " << util::HexString(leafhash);

  return 0;
}
