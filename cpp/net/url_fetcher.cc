#include "net/url_fetcher.h"

#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include <glog/logging.h>

#include "net/connection_pool.h"

using cert_trans::internal::ConnectionPool;
using cert_trans::internal::evhttp_connection_unique_ptr;
using std::bind;
using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::steady_clock;
using std::endl;
using std::make_pair;
using std::move;
using std::ostream;
using std::string;
using util::Status;
using util::Task;
using util::TaskHold;

DEFINE_int32(url_fetcher_default_timeout_seconds, 60,
             "default timeout for URL fetcher requests");

namespace cert_trans {


struct UrlFetcher::Impl {
  Impl(libevent::Base* base) : base_(CHECK_NOTNULL(base)), pool_(base_) {
  }

  libevent::Base* const base_;
  internal::ConnectionPool pool_;
};


namespace {


evhttp_cmd_type VerbToCmdType(UrlFetcher::Verb verb) {
  switch (verb) {
    case UrlFetcher::Verb::GET:
      return EVHTTP_REQ_GET;

    case UrlFetcher::Verb::POST:
      return EVHTTP_REQ_POST;

    case UrlFetcher::Verb::PUT:
      return EVHTTP_REQ_PUT;

    case UrlFetcher::Verb::DELETE:
      return EVHTTP_REQ_DELETE;
  }

  LOG(FATAL) << "unknown UrlFetcher::Verb: " << static_cast<int>(verb);
}


struct State {
  State(ConnectionPool* pool, const UrlFetcher::Request& request,
        UrlFetcher::Response* response, Task* task);

  ~State() {
    CHECK(!conn_) << "request state object still had a connection at cleanup?";
  }

  // The following methods must only be called on the libevent
  // dispatch thread.
  void MakeRequest();
  void RequestDone(evhttp_request* req);

  ConnectionPool* const pool_;
  const UrlFetcher::Request request_;
  UrlFetcher::Response* const response_;
  Task* const task_;

  evhttp_connection_unique_ptr conn_;
};


std::ostream& operator<<(std::ostream& output, evhttp_cmd_type cmd) {
  switch (cmd) {
#define PRINT_CMD(x) \
  case x:            \
    output << #x;    \
    break
    PRINT_CMD(EVHTTP_REQ_GET);
    PRINT_CMD(EVHTTP_REQ_POST);
    PRINT_CMD(EVHTTP_REQ_HEAD);
    PRINT_CMD(EVHTTP_REQ_PUT);
    PRINT_CMD(EVHTTP_REQ_DELETE);
    PRINT_CMD(EVHTTP_REQ_OPTIONS);
    PRINT_CMD(EVHTTP_REQ_TRACE);
    PRINT_CMD(EVHTTP_REQ_CONNECT);
    PRINT_CMD(EVHTTP_REQ_PATCH);
#undef PRINT_CMD
  }
  return output;
}


void PrintHeaders(std::ostream& output, evkeyvalq* headers) {
  for (evkeyval* ptr = headers->tqh_first; ptr; ptr = ptr->next.tqe_next) {
    output << "  " << ptr->key << ": " << ptr->value << std::endl;
  }
}


std::ostream& operator<<(std::ostream& output, evbuffer& buffer) {
  const size_t length(evbuffer_get_length(&buffer));
  output.write(reinterpret_cast<char*>(evbuffer_pullup(&buffer, length)),
               length);
  return output;
}


std::ostream& operator<<(std::ostream& output, evhttp_request& req) {
  output << "command: " << evhttp_request_get_command(&req) << std::endl;
  // output << ": " << evhttp_request_get_connection(&req) << std::endl;
  // output << ": " << evhttp_request_get_evhttp_uri(&req) << std::endl;
  const char* host(evhttp_request_get_host(&req));
  output << "host: " << (host ? host : "<null>") << std::endl;

  output << "input_buffer: \"" << *evhttp_request_get_input_buffer(&req)
         << "\"" << std::endl;
  output << "input_headers {" << std::endl;
  PrintHeaders(output, evhttp_request_get_input_headers(&req));
  output << "}" << std::endl;

  output << "output_buffer: \"" << *evhttp_request_get_output_buffer(&req)
         << "\"" << std::endl;
  output << "output_headers {" << std::endl;
  PrintHeaders(output, evhttp_request_get_output_headers(&req));
  output << "}" << std::endl;

  output << "response_code: " << evhttp_request_get_response_code(&req)
         << std::endl;
  output << "uri: " << evhttp_request_get_uri(&req) << std::endl;
  output << "is_owned: " << evhttp_request_is_owned(&req) << std::endl;
  return output;
}


void RequestCallback(evhttp_request* req, void* userdata) {
  static_cast<State*>(CHECK_NOTNULL(userdata))->RequestDone(req);
}


UrlFetcher::Request NormaliseRequest(UrlFetcher::Request req) {
  if (req.deadline.time_since_epoch() == steady_clock::duration::zero()) {
    req.deadline = steady_clock::now() +
                   seconds(FLAGS_url_fetcher_default_timeout_seconds);
  }

  if (req.url.Path().empty()) {
    req.url.SetPath("/");
  }

  if (req.headers.find("Host") == req.headers.end()) {
    req.headers.insert(make_pair("Host", req.url.Host()));
  }

  return req;
}


State::State(ConnectionPool* pool, const UrlFetcher::Request& request,
             UrlFetcher::Response* response, Task* task)
    : pool_(CHECK_NOTNULL(pool)),
      request_(NormaliseRequest(request)),
      response_(CHECK_NOTNULL(response)),
      task_(CHECK_NOTNULL(task)) {
  if (request_.url.Protocol() != "http") {
    VLOG(1) << "unsupported protocol: " << request_.url.Protocol();
    task_->Return(Status(util::error::INVALID_ARGUMENT,
                         "UrlFetcher: unsupported protocol: " +
                             request_.url.Protocol()));
    return;
  }
}


void State::MakeRequest() {
  CHECK(libevent::Base::OnEventThread());
  CHECK(!conn_) << "making a request while one is already outstanding?";

  const steady_clock::time_point now(steady_clock::now());
  const auto deadline_left(request_.deadline - now);
  if (deadline_left.count() <= 0) {
    VLOG(1) << "deadline expired: " << duration<float>(deadline_left).count();
    task_->Return(Status(util::error::DEADLINE_EXCEEDED,
                         "URL fetch request exceeded deadline"));
    return;
  }

  evhttp_request* const http_req(
      CHECK_NOTNULL(evhttp_request_new(&RequestCallback, this)));
  for (const auto& header : request_.headers) {
    evhttp_add_header(evhttp_request_get_output_headers(http_req),
                      header.first.c_str(), header.second.c_str());
  }

  if (!request_.body.empty()) {
    if (evbuffer_add_reference(evhttp_request_get_output_buffer(http_req),
                               request_.body.data(), request_.body.size(),
                               nullptr, nullptr) != 0) {
      VLOG(1) << "error when adding the request body";
      task_->Return(
          Status(util::error::INTERNAL, "could not set the request body"));
      return;
    }
  }

  conn_ = pool_->Get(request_.url);

#if 0
  evhttp_connection_set_timeout(conn_.get(), ceil(deadline_left.count()));
#endif

  const evhttp_cmd_type verb(VerbToCmdType(request_.verb));
  VLOG(1) << "evhttp_make_request(" << conn_.get() << ", " << http_req << ", "
          << verb << ", \"" << request_.url.PathQuery() << "\")";
  if (evhttp_make_request(conn_.get(), http_req, verb,
                          request_.url.PathQuery().c_str()) != 0) {
    VLOG(1) << "evhttp_make_request error";
    // Put back the connection, RequestDone is not going to get
    // called.
    pool_->Put(move(conn_));
    task_->Return(Status(util::error::INTERNAL, "evhttp_make_request error"));
    return;
  }
}


void State::RequestDone(evhttp_request* req) {
  CHECK(libevent::Base::OnEventThread());
  CHECK(conn_);
  pool_->Put(move(conn_));

  LOG(INFO) << "RequestDone: " << req;

  if (!req) {
    // TODO(pphaneuf): The dreaded null request... These are fairly
    // fatal things, like protocol parse errors, but could also be a
    // connection timeout. I think we should do retries in this case,
    // with a deadline of our own? At least, then, it would be easier
    // to distinguish between an obscure error, or a more common
    // timeout.
    VLOG(1) << "RequestCallback received a null request";
    task_->Return(Status::UNKNOWN);
    return;
  }

  response_->status_code = evhttp_request_get_response_code(req);
  if (response_->status_code < 100) {
    LOG(INFO) << "evhttp_request:\n" << *req;
    // TODO(pphaneuf): According to my reading of libevent, this is
    // most likely to be a connection refused?
    VLOG(1) << "request has a status code lower than 100: "
            << response_->status_code;
    task_->Return(
        Status(util::error::FAILED_PRECONDITION, "connection refused"));
    return;
  }

  response_->headers.clear();
  for (evkeyval* ptr = evhttp_request_get_input_headers(req)->tqh_first; ptr;
       ptr = ptr->next.tqe_next) {
    response_->headers.insert(make_pair(ptr->key, ptr->value));
  }

  const size_t body_length(
      evbuffer_get_length(evhttp_request_get_input_buffer(req)));
  string body(reinterpret_cast<const char*>(evbuffer_pullup(
                  evhttp_request_get_input_buffer(req), body_length)),
              body_length);
  response_->body.swap(body);

  task_->Return();
}


}  // namespace


// Needs to be defined where Impl is also defined.
UrlFetcher::UrlFetcher() {
}


UrlFetcher::UrlFetcher(libevent::Base* base)
    : impl_(new Impl(CHECK_NOTNULL(base))) {
}


// Needs to be defined where Impl is also defined.
UrlFetcher::~UrlFetcher() {
}


void UrlFetcher::Fetch(const Request& req, Response* resp, Task* task) {
  TaskHold hold(task);

  State* const state(new State(&impl_->pool_, req, resp, task));
  task->DeleteWhenDone(state);

  impl_->base_->Add(bind(&State::MakeRequest, state));
}


ostream& operator<<(ostream& output, const UrlFetcher::Response& resp) {
  output << "status_code: " << resp.status_code << endl
         << "headers {" << endl;
  for (const auto& header : resp.headers) {
    output << "  " << header.first << ": " << header.second << endl;
  }
  output << "}" << endl
         << "body: <<EOF" << endl
         << resp.body << "EOF" << endl;

  return output;
}


}  // namespace cert_trans
