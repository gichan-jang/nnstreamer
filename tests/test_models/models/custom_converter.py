##
# SPDX-License-Identifier: LGPL-2.1-only
#
# Copyright (C) 2021 Samsung Electronics
#
# @file    custom_converter.py
# @brief   Python custom converter
# @author  Gichan Jang <gichan2.jang@samsung.com>
#
# @note The Flexbuffers Python API is supported if the flatbuffers version is greater than 1.12.
#       See: https://github.com/google/flatbuffers/issues/5306
#       It can be downloaded and used from https://github.com/google/flatbuffers/blob/master/python/flatbuffers/flexbuffers.py

import numpy as np
import nnstreamer_python as nns
from flatbuffers import flexbuffers

## @brief  User-defined custom converter
class CustomConverter(object):

## @breif  Python callback: convert
  def convert (self, input_array):
    data = input_array[0].tobytes()
    root = flexbuffers.GetRoot(data)
    tensors = root.AsMap

    num_tensors = tensors['num_tensors'].AsInt
    rate_n = tensors['rate_n'].AsInt
    rate_d = tensors['rate_d'].AsInt
    raw_data = []
    tensors_info = []
    tensor_type = []

    for i in range(num_tensors):
      tensor_key = "tensor_{idx}".format(idx=i)
      tensor = tensors[tensor_key].AsVector
      tensor_type.append(tensor[1].AsInt)
      tdim = tensor[2].AsTypedVector
      dim = []
      for j in range(4):
        dim.append(tdim[j].AsInt)

      tensors_info.append(nns.TensorShape(dim, np.uint8))
      raw_data.append(np.frombuffer(tensor[3].AsBlob, dtype=np.uint8))

    return (tensors_info, raw_data, tensor_type, rate_n, rate_d)
