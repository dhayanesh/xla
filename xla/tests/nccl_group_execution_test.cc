/* Copyright 2024 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "xla/hlo/testlib/verified_hlo_module.h"
#include "xla/literal.h"
#include "xla/service/hlo_module_config.h"
#include "xla/tests/hlo_test_base.h"
#include "xla/tests/test_macros.h"
#include "tsl/platform/statusor.h"

namespace xla {
namespace {

// Tests NCCL group execution.

class NcclGroupExecutionTest : public HloTestBase {
 public:
  NcclGroupExecutionTest() {
    VLOG(1) << "Running with " << num_devices() << " devices";
  }
};

XLA_TEST_F(NcclGroupExecutionTest, NcclGroupSendRecvNoWhileLoop) {
  const absl::string_view kModuleStr = R"(
  HloModule module_main, entry_computation_layout={()->(f32[], f32[])}

  wrapped_send_recv {
    param0 = f32[] parameter(0)
    param1 = token[] parameter(1)
    send1 = (f32[], u32[], token[]) send(param0, param1), channel_id=1
    param2 = f32[] parameter(2)
    param3 = token[] parameter(3)
    send2 = (f32[], u32[], token[]) send(param2, param3), channel_id=2
    param4 = token[] parameter(4)
    recv1 = (f32[], u32[], token[]) recv(param4), channel_id=1
    param5 = token[] parameter(5)
    recv2 = (f32[], u32[], token[]) recv(param5), channel_id=2
    ROOT out = ((f32[], u32[], token[]), (f32[], u32[], token[]),
      (f32[], u32[], token[]), (f32[], u32[], token[]))
      tuple(send1, send2, recv1, recv2)
  }

  ENTRY main {
    data1 = f32[] constant(1)
    after-all1 = token[] after-all()
    data2 = f32[] constant(2)
    after-all2 = token[] after-all()
    async-comp-start = ((f32[], token[], f32[], token[], token[], token[]),
      ((f32[], u32[], token[]), (f32[], u32[], token[]), (f32[], u32[], token[]),
      (f32[], u32[], token[])), s32[]) async-start(data1, after-all1,
      data2, after-all2, after-all1, after-all2), calls=wrapped_send_recv
    async-comp-done = ((f32[], u32[], token[]), (f32[], u32[], token[]),
      (f32[], u32[], token[]), (f32[], u32[], token[])) async-done(async-comp-start)
    unpack-recv-done1 = (f32[], u32[], token[]) get-tuple-element(async-comp-done), index=2
    recv-done-data1 = f32[] get-tuple-element(unpack-recv-done1), index=0
    recv-done-token1 = token[] get-tuple-element(unpack-recv-done1), index=2
    recv-done1 = (f32[], token[]) tuple(recv-done-data1, recv-done-token1),
      control-predecessors={async-comp-start}
    data-out1 = f32[] get-tuple-element(recv-done1), index=0
    unpack-recv-done2 = (f32[], u32[], token[]) get-tuple-element(async-comp-done), index=3
    recv-done-data2 = f32[] get-tuple-element(unpack-recv-done2), index=0
    recv-done-token2 = token[] get-tuple-element(unpack-recv-done2), index=2
    recv-done2 = (f32[], token[]) tuple(recv-done-data2, recv-done-token2),
      control-predecessors={async-comp-start}
    data-out2 = f32[] get-tuple-element(recv-done2), index=0
    ROOT out = (f32[], f32[]) tuple(data-out1, data-out2)
    unpack-send-done1 = (f32[], u32[], token[]) get-tuple-element(async-comp-done), index=0
    send-done1 = token[] get-tuple-element(unpack-send-done1), index=2
    unpack-send-done2 = (f32[], u32[], token[]) get-tuple-element(async-comp-done), index=1
    send-done2 = token[] get-tuple-element(unpack-send-done2), index=2
  }

  )";
  const int64_t kNumReplicas = 4;
  SKIP_TEST_IF_NUM_DEVICES_LESS_THAN(kNumReplicas)

  HloModuleConfig config =
      GetModuleConfigForTest(/*replica_count=*/kNumReplicas);
  std::unique_ptr<VerifiedHloModule> module;
  TF_ASSERT_OK_AND_ASSIGN(module,
                          ParseAndReturnVerifiedModule(kModuleStr, config));
  TF_ASSERT_OK_AND_ASSIGN(
      std::vector<Literal> results,
      ExecuteReplicated(std::move(module), absl::Span<Literal* const>{},
                        kNumReplicas,
                        /*run_hlo_passes=*/true));
}

}  // namespace

}  // namespace xla