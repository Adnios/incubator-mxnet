/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * Copyright (c) 2017 by Contributors
 * \file pooling-inl.h
 * \brief
 * \author Bing Xu, Jun Wu, Da Zheng, Hao Jin
*/

#ifndef MXNET_OPERATOR_NN_POOLING_INL_H_
#define MXNET_OPERATOR_NN_POOLING_INL_H_

#include <dmlc/logging.h>
#include <dmlc/parameter.h>
#include <mxnet/operator.h>
#include <algorithm>
#include <map>
#include <vector>
#include <string>
#include <utility>
#include "../operator_common.h"
#include "./pool.h"

//! herewj
#include <unistd.h>

namespace mxnet {
namespace op {

void PoolingParamParser(nnvm::NodeAttrs *attrs);

struct PoolingParam : public dmlc::Parameter<PoolingParam> {
  TShape kernel;
  TShape stride;
  TShape pad;
  int pool_type;
  int pooling_convention;
  bool global_pool;
  //! herewj
  bool virtual_compute;
  uint64_t sleep_time;
  uint64_t backward_sleep_time;
  bool cudnn_off;
  dmlc::optional<int> p_value;
  dmlc::optional<bool> count_include_pad;
  DMLC_DECLARE_PARAMETER(PoolingParam) {
    DMLC_DECLARE_FIELD(kernel).set_default(TShape())  // add default value here
    .enforce_nonzero()
    .describe("Pooling kernel size: (y, x) or (d, y, x)");

    DMLC_DECLARE_FIELD(pool_type).set_default(pool_enum::kMaxPooling)  // add default pooling method
    .add_enum("max", pool_enum::kMaxPooling)
    .add_enum("avg", pool_enum::kAvgPooling)
    .add_enum("sum", pool_enum::kSumPooling)
    .add_enum("lp", pool_enum::kLpPooling)
    .describe("Pooling type to be applied.");

    DMLC_DECLARE_FIELD(global_pool).set_default(false)
    .describe("Ignore kernel size, do global pooling based on current input feature map. ");

    //! herewj
    DMLC_DECLARE_FIELD(virtual_compute).set_default(false)
    .describe("Whether to disable compute.");

    DMLC_DECLARE_FIELD(sleep_time).set_default(0)
    .describe("When virtual_compute is true, the sleeping time");

    DMLC_DECLARE_FIELD(backward_sleep_time).set_default(0)
    .describe("When virtual_compute is true, the backward sleeping time");

    DMLC_DECLARE_FIELD(cudnn_off).set_default(false)
    .describe("Turn off cudnn pooling and use MXNet pooling operator. ");

    DMLC_DECLARE_FIELD(pooling_convention).set_default(pool_enum::kValid)
    .add_enum("full", pool_enum::kFull)
    .add_enum("valid", pool_enum::kValid)
    .add_enum("same", pool_enum::kSame)
    .describe("Pooling convention to be applied.");

    DMLC_DECLARE_FIELD(stride).set_default(TShape())
    .enforce_nonzero()
    .describe("Stride: for pooling (y, x) or (d, y, x). Defaults to 1 for each dimension.");

    DMLC_DECLARE_FIELD(pad).set_default(TShape())
    .describe("Pad for pooling: (y, x) or (d, y, x). Defaults to no padding.");

    DMLC_DECLARE_FIELD(p_value).set_default(dmlc::optional<int>())
    .describe("Value of p for Lp pooling, can be 1 or 2, required for Lp Pooling.");

    DMLC_DECLARE_FIELD(count_include_pad).set_default(dmlc::optional<bool>())
    .describe("Only used for AvgPool, specify whether to count padding elements for average"
              "calculation. For example, with a 5*5 kernel on a 3*3 corner of a image,"
              "the sum of the 9 valid elements will be divided by 25 if this is set to true,"
              "or it will be divided by 9 if this is set to false. Defaults to true.");
  }

  bool operator==(const PoolingParam& other) const {
    return this->kernel             == other.kernel &&
           this->stride             == other.stride &&
           this->pad                == other.pad &&
           this->pool_type          == other.pool_type &&
           this->pooling_convention == other.pooling_convention &&
           this->global_pool        == other.global_pool &&
           this->virtual_compute == other.virtual_compute &&
           this->sleep_time == other.sleep_time &&
           this->backward_sleep_time == other.backward_sleep_time &&
           this->cudnn_off          == other.cudnn_off &&
           this->p_value            == other.p_value &&
           this->count_include_pad  == other.count_include_pad;
  }
};

}  // namespace op
}  // namespace mxnet

namespace std {
template<>
struct hash<mxnet::op::PoolingParam> {
  size_t operator()(const mxnet::op::PoolingParam& val) {
    size_t ret = 0;
    ret = dmlc::HashCombine(ret, val.kernel);
    ret = dmlc::HashCombine(ret, val.stride);
    ret = dmlc::HashCombine(ret, val.pad);
    ret = dmlc::HashCombine(ret, val.pool_type);
    ret = dmlc::HashCombine(ret, val.pooling_convention);
    ret = dmlc::HashCombine(ret, val.global_pool);
    //! herewj
    ret = dmlc::HashCombine(ret, val.virtual_compute);
    ret = dmlc::HashCombine(ret, val.sleep_time);
    ret = dmlc::HashCombine(ret, val.backward_sleep_time);
    ret = dmlc::HashCombine(ret, val.cudnn_off);
    ret = dmlc::HashCombine(ret, val.p_value);
    ret = dmlc::HashCombine(ret, val.count_include_pad);
    return ret;
  }
};
}  // namespace std

namespace mxnet {
namespace op {

/*
 * When MKLDNN is enabled, we might want 2 outputs instead of one inputs, which
 * also changes the number of inputs for backward.
 */
int GetNumOutputs(const PoolingParam &param);
int GetNumBackInputs(const PoolingParam &param);

template<typename xpu, typename DType>
class PoolingOp {
 public:
  void Init(PoolingParam p) {
    this->param_ = p;
  }

  void Forward(const OpContext& ctx, const TBlob& in_data,
               const OpReqType& req, const TBlob& out_data) {
    using namespace mshadow;
    Stream<xpu> *s = ctx.get_stream<xpu>();
    const TShape& ishape = in_data.shape_;
    TShape kernel = param_.kernel;
    TShape padding = param_.pad;
    TShape stride = param_.stride;
    if (param_.global_pool) {
      kernel = TShape(ishape.data() + 2,
               ishape.data() + ishape.ndim());
      padding = TShape(ishape.ndim() - 2);
      for (index_t i = 0; i < ishape.ndim() - 2; i++) {
        padding[i] = 0;
      }
      stride = TShape(ishape.ndim() - 2);
    }
    const int p_value = (param_.pool_type == pool_enum::kLpPooling && param_.p_value.has_value()) ?
                        param_.p_value.value() : 1;
    const bool count_include_pad = (param_.count_include_pad.has_value()) ?
                                   param_.count_include_pad.value() : true;
    switch (p_value) {
      case 1:
        pool<DType, 1>(s, in_data.dptr<DType>(), in_data.shape_, out_data.shape_,
          kernel,
          padding,
          stride,
          param_.pool_type, req, out_data.dptr<DType>(), count_include_pad);
        break;
      case 2:
        pool<DType, 2>(s, in_data.dptr<DType>(), in_data.shape_, out_data.shape_,
          kernel,
          padding,
          stride,
          param_.pool_type, req, out_data.dptr<DType>(), count_include_pad);
        break;
      case 3:
        pool<DType, 3>(s, in_data.dptr<DType>(), in_data.shape_, out_data.shape_,
          kernel,
          padding,
          stride,
          param_.pool_type, req, out_data.dptr<DType>(), count_include_pad);
        break;
      default:
        LOG(FATAL) << "p value of " << p_value << " is not supported yet...";
    }
  }

  void Backward(const OpContext& ctx, const TBlob& out_grad,
                const TBlob& in_data, const TBlob& out_data,
                const OpReqType& req, const TBlob& in_grad) {
    using namespace mshadow;
    Stream<xpu> *s = ctx.get_stream<xpu>();
    const TShape& ishape = in_data.shape_;
    TShape kernel = param_.kernel;
    TShape padding = param_.pad;
    TShape stride = param_.stride;
    if (param_.global_pool) {
      kernel = TShape(ishape.data() + 2,
               ishape.data() + ishape.ndim());
      padding = TShape(ishape.ndim() - 2);
      for (index_t i = 0; i < ishape.ndim() - 2; i++) {
        padding[i] = 0;
      }
      stride = TShape(ishape.ndim() - 2);
    }

    const int p_value = (param_.pool_type == pool_enum::kLpPooling && param_.p_value.has_value()) ?
                        param_.p_value.value() : 1;
    const bool count_include_pad = (param_.count_include_pad.has_value()) ?
                                   param_.count_include_pad.value() : true;
    switch (p_value) {
      case 1:
        unpool<DType, 1>(s, out_grad.dptr<DType>(), in_data.dptr<DType>(), out_data.dptr<DType>(),
           in_grad.shape_, out_grad.shape_,
           kernel,
           padding,
           stride,
           param_.pool_type, req, in_grad.dptr<DType>(), count_include_pad);
        break;
      case 2:
        unpool<DType, 2>(s, out_grad.dptr<DType>(), in_data.dptr<DType>(), out_data.dptr<DType>(),
           in_grad.shape_, out_grad.shape_,
           kernel,
           padding,
           stride,
           param_.pool_type, req, in_grad.dptr<DType>(), count_include_pad);
        break;
      case 3:
        unpool<DType, 3>(s, out_grad.dptr<DType>(), in_data.dptr<DType>(), out_data.dptr<DType>(),
           in_grad.shape_, out_grad.shape_,
           kernel,
           padding,
           stride,
           param_.pool_type, req, in_grad.dptr<DType>(), count_include_pad);
        break;
      default:
        LOG(FATAL) << "p value of " << p_value << " is not supported yet...";
    }
  }

 private:
  PoolingParam param_;
};  // class PoolingOp

template<typename xpu>
void PoolingCompute(const nnvm::NodeAttrs& attrs,
                    const OpContext& ctx,
                    const std::vector<TBlob>& inputs,
                    const std::vector<OpReqType>& req,
                    const std::vector<TBlob>& outputs) {
  const PoolingParam& param = nnvm::get<PoolingParam>(attrs.parsed);
  CHECK_EQ(inputs.size(), 1U);
  CHECK_EQ(outputs.size(), GetNumOutputs(param));
  if (!param.global_pool) {
    // check if filter size assigned correctly
    CHECK_GT(param.kernel.ndim(), 0U)
        << "You need to set the kernel size if global pooling is not used";
  }
  MSHADOW_REAL_TYPE_SWITCH(inputs[0].type_flag_, DType, {
    if (pool_enum::kMaxPooling == param.pool_type
        || pool_enum::kAvgPooling == param.pool_type
        || pool_enum::kSumPooling == param.pool_type
        || pool_enum::kLpPooling == param.pool_type) {
      PoolingOp<xpu, DType> op;
      // herewj
      if(param.virtual_compute == false) {
        op.Init(param);
        op.Forward(ctx, inputs[0], req[0], outputs[0]);
      } else {
        useconds_t time = param.sleep_time;
        usleep(time);
      }
    } else {
      LOG(FATAL) << "unknown pooling type";
    }
  });
}

template<typename xpu>
void PoolingGradCompute(const nnvm::NodeAttrs& attrs,
                        const OpContext& ctx,
                        const std::vector<TBlob>& inputs,
                        const std::vector<OpReqType>& req,
                        const std::vector<TBlob>& outputs) {
  const PoolingParam& param = nnvm::get<PoolingParam>(attrs.parsed);
  CHECK_EQ(inputs.size(), GetNumBackInputs(param));
  CHECK_EQ(outputs.size(), 1U);
  CHECK_EQ(req.size(), 1U);
  if (!param.global_pool) {
    // check if filter size assigned correctly
    CHECK_GT(param.kernel.ndim(), 0U)
        << "You need to set the kernel size if global pooling is not used";
  }
  off_t ograd_idx, in_data_idx, out_data_idx;
  // When MKLDNN is enabled, the input data may contains arrays for workspace.
  if (GetNumBackInputs(param) == 5) {
    ograd_idx = 0;
    in_data_idx = 2;
    out_data_idx = 3;
  } else {
    ograd_idx = 0;
    in_data_idx = 1;
    out_data_idx = 2;
  }
  MSHADOW_REAL_TYPE_SWITCH(inputs[0].type_flag_, DType, {
    if (pool_enum::kMaxPooling == param.pool_type
        || pool_enum::kAvgPooling == param.pool_type
        || pool_enum::kSumPooling == param.pool_type
        || pool_enum::kLpPooling == param.pool_type) {
      PoolingOp<xpu, DType> op;
      // herewj
      if(param.virtual_compute == false) {
        op.Init(param);
        op.Backward(ctx, inputs[ograd_idx], inputs[in_data_idx],
                    inputs[out_data_idx], req[0], outputs[0]);
      } else {
        useconds_t time = param.backward_sleep_time;
        usleep(time);
      }
    } else {
      LOG(FATAL) << "unknown pooling type";
    }
  });
}

}  // namespace op
}  // namespace mxnet

#endif  // MXNET_OPERATOR_NN_POOLING_INL_H_
