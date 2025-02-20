#include <chrono>  // NOLINT
#include <ctime>
#include <thread>  // NOLINT

#include "caffe2/core/net.h"
#include "caffe2/core/operator.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

namespace caffe2 {

using std::clock_t;
using std::clock;

// When measuring time, we relax the measured time by +- 20ms.
const int kTimeThreshold = 20;

// SleepOp basically sleeps for a given number of seconds.
class SleepOp final : public OperatorBase {
 public:
  SleepOp(const OperatorDef& operator_def, Workspace* ws)
      : OperatorBase(operator_def, ws),
        ms_(OperatorBase::GetSingleArgument<int>("ms", 1000)) {
    CAFFE_DCHECK_GT(ms_, 0);
    CAFFE_DCHECK_LT(ms_, 3600 * 1000) << "Really? This long?";
  }

  bool Run() {
    clock_t start = clock();
    std::this_thread::sleep_for(std::chrono::milliseconds(ms_));
    clock_t end = clock();
    if (OperatorBase::OutputSize()) {
      vector<clock_t>* output = OperatorBase::Output<vector<clock_t> >(0);
      output->resize(2);
      (*output)[0] = start;
      (*output)[1] = end;
    }
    return true;
  }

 private:
  int ms_;
  // We allow arbitrary inputs and at most one output so that we can
  // test scaffolding of networks. If the output is 1, it will be filled with
  // vector<clock_t> with two elements: start time and end time.
  INPUT_OUTPUT_STATS(0, INT_MAX, 0, 1);
  DISABLE_COPY_AND_ASSIGN(SleepOp);
};

namespace {
REGISTER_CPU_OPERATOR(Sleep, SleepOp);
REGISTER_CUDA_OPERATOR(Sleep, SleepOp);
}  // namespace

const char kSleepNetDefString[] =
"  name: \"sleepnet\""
"  net_type: \"dag\""
"  num_workers: 2"
"  op {"
"    output: \"sleep1\""
"    name: \"sleep1\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 100"
"    }"
"  }"
"  op {"
"    input: \"sleep1\""
"    output: \"sleep2\""
"    name: \"sleep2\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 100"
"    }"
"  }"
"  op {"
"    output: \"sleep3\""
"    name: \"sleep3\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 150"
"    }"
"  }";

namespace {
// Run a network and get its duration in milliseconds.
int RunNetAndGetDuration(const string& net_def_str, const string& net_type) {
  NetDef net_def;
  CAFFE_CHECK(google::protobuf::TextFormat::ParseFromString(
    net_def_str, &net_def));
  net_def.set_net_type(net_type);
  Workspace ws;
  unique_ptr<NetBase> net(CreateNet(net_def, &ws));
  CAFFE_CHECK(net.get() != nullptr);
  CAFFE_CHECK(net->Verify());
  auto start_time = std::chrono::system_clock::now();
  CAFFE_CHECK(net->Run());
  // Inspect the time - it should be around 200 milliseconds, since sleep3 can
  // run in parallel with sleep1 and sleep2.
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now() - start_time);
  int milliseconds = duration.count();
  return milliseconds;
}
}  // namespace

TEST(DAGNetTest, TestDAGNetTiming) {
  int ms = RunNetAndGetDuration(string(kSleepNetDefString), "dag");
  EXPECT_NEAR(ms, 200, kTimeThreshold);
}

// For sanity check, we also test the sequential time - it should take 0.35
// seconds instead since everything has to be sequential.
TEST(SimpleNetTest, TestSimpleNetTiming) {
  int ms = RunNetAndGetDuration(string(kSleepNetDefString), "simple");
  EXPECT_NEAR(ms, 350, kTimeThreshold);
}

// This network has two operators reading the same blob at the same time. This
// should not change anything and the DAG should still make sleep2 and sleep3
// run in parallel.
const char kSleepNetDefStringReadAfterRead[] =
"  name: \"sleepnet\""
"  net_type: \"dag\""
"  num_workers: 2"
"  op {"
"    output: \"sleep1\""
"    name: \"sleep1\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 100"
"    }"
"  }"
"  op {"
"    input: \"sleep1\""
"    output: \"sleep2\""
"    name: \"sleep2\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 100"
"    }"
"  }"
"  op {"
"    input: \"sleep1\""
"    output: \"sleep3\""
"    name: \"sleep3\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 150"
"    }"
"  }";

TEST(DAGNetTest, TestDAGNetTimingReadAfterRead) {
  int ms = RunNetAndGetDuration(string(kSleepNetDefStringReadAfterRead), "dag");
  EXPECT_NEAR(ms, 250, kTimeThreshold);
}

// For sanity check, we also test the sequential time - it should take 0.35
// seconds instead since everything has to be sequential.
TEST(SimpleNetTest, TestSimpleNetTimingReadAfterRead) {
  int ms = RunNetAndGetDuration(string(kSleepNetDefStringReadAfterRead), "simple");
  EXPECT_NEAR(ms, 350, kTimeThreshold);
}

// This network has two operators writing out the sleep2 blob. As a result, the
// operator sleep2-again creates a write after write dependency and the whole
// process should be sequential.
const char kSleepNetDefStringWriteAfterWrite[] =
"  name: \"sleepnet\""
"  net_type: \"dag\""
"  num_workers: 2"
"  op {"
"    output: \"sleep1\""
"    name: \"sleep1\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 100"
"    }"
"  }"
"  op {"
"    input: \"sleep1\""
"    output: \"sleep2\""
"    name: \"sleep2\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 100"
"    }"
"  }"
"  op {"
"    output: \"sleep2\""
"    name: \"sleep2-again\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 150"
"    }"
"  }";

TEST(DAGNetTest, TestDAGNetTimingWriteAfterWrite) {
  int ms = RunNetAndGetDuration(
      string(kSleepNetDefStringWriteAfterWrite), "dag");
  EXPECT_NEAR(ms, 350, kTimeThreshold);
}

TEST(SimpleNetTest, TestSimpleNetTimingWriteAfterWrite) {
  int ms = RunNetAndGetDuration(
      string(kSleepNetDefStringWriteAfterWrite), "simple");
  EXPECT_NEAR(ms, 350, kTimeThreshold);
}

// This network has an operator writing to sleep1 while another operator is
// accessing it. As a result, the operator sleep1-again creates a write after
// read dependency and the whole process should be sequential.
const char kSleepNetDefStringWriteAfterRead[] =
"  name: \"sleepnet\""
"  net_type: \"dag\""
"  num_workers: 2"
"  op {"
"    output: \"sleep1\""
"    name: \"sleep1\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 100"
"    }"
"  }"
"  op {"
"    input: \"sleep1\""
"    output: \"sleep2\""
"    name: \"sleep2\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 100"
"    }"
"  }"
"  op {"
"    output: \"sleep1\""
"    name: \"sleep1-again\""
"    type: \"Sleep\""
"    arg {"
"      name: \"ms\""
"      i: 150"
"    }"
"  }";

TEST(DAGNetTest, TestDAGNetTimingWriteAfterRead) {
  int ms = RunNetAndGetDuration(
      string(kSleepNetDefStringWriteAfterRead), "dag");
  EXPECT_NEAR(ms, 350, kTimeThreshold);
}

TEST(SimpleNetTest, TestSimpleNetTimingWriteAfterRead) {
  int ms = RunNetAndGetDuration(
      string(kSleepNetDefStringWriteAfterRead), "simple");
  EXPECT_NEAR(ms, 350, kTimeThreshold);
}

}  // namespace caffe2



