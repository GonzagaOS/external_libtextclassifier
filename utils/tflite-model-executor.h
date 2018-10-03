/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Contains classes that can execute different models/parts of a model.

#ifndef LIBTEXTCLASSIFIER_UTILS_TFLITE_MODEL_EXECUTOR_H_
#define LIBTEXTCLASSIFIER_UTILS_TFLITE_MODEL_EXECUTOR_H_

#include <memory>

#include "utils/base/logging.h"
#include "utils/tensor-view.h"
#include "tensorflow/contrib/lite/interpreter.h"
#include "tensorflow/contrib/lite/kernels/register.h"
#include "tensorflow/contrib/lite/model.h"
#include "tensorflow/contrib/lite/op_resolver.h"
#include "tensorflow/contrib/lite/string_util.h"

namespace libtextclassifier3 {

std::unique_ptr<tflite::OpResolver> BuildOpResolver();
std::unique_ptr<const tflite::FlatBufferModel> TfLiteModelFromModelSpec(
    const tflite::Model*);
std::unique_ptr<const tflite::FlatBufferModel> TfLiteModelFromBuffer(
    const flatbuffers::Vector<uint8_t>*);

// Executor for the text selection prediction and classification models.
class TfLiteModelExecutor {
 public:
  static std::unique_ptr<TfLiteModelExecutor> FromModelSpec(
      const tflite::Model* model_spec) {
    auto model = TfLiteModelFromModelSpec(model_spec);
    if (!model) {
      return nullptr;
    }
    return std::unique_ptr<TfLiteModelExecutor>(
        new TfLiteModelExecutor(std::move(model)));
  }

  static std::unique_ptr<TfLiteModelExecutor> FromBuffer(
      const flatbuffers::Vector<uint8_t>* model_spec_buffer) {
    auto model = TfLiteModelFromBuffer(model_spec_buffer);
    if (!model) {
      return nullptr;
    }
    return std::unique_ptr<TfLiteModelExecutor>(
        new TfLiteModelExecutor(std::move(model)));
  }

  // Creates an Interpreter for the model that serves as a scratch-pad for the
  // inference. The Interpreter is NOT thread-safe.
  std::unique_ptr<tflite::Interpreter> CreateInterpreter() const;

  template <typename T>
  void SetInput(const int input_index, const TensorView<T>& input_data,
                tflite::Interpreter* interpreter) const {
    input_data.copy_to(interpreter->typed_input_tensor<T>(input_index),
                       input_data.size());
  }

  template <typename T>
  void SetInput(const int input_index, const std::vector<T>& input_data,
                tflite::Interpreter* interpreter) const {
    std::copy(input_data.begin(), input_data.end(),
              interpreter->typed_input_tensor<T>(input_index));
  }

  template <typename T>
  TensorView<T> OutputView(const int output_index,
                           tflite::Interpreter* interpreter) const {
    TfLiteTensor* output_tensor =
        interpreter->tensor(interpreter->outputs()[output_index]);
    return TensorView<T>(interpreter->typed_output_tensor<T>(output_index),
                         std::vector<int>(output_tensor->dims->data,
                                          output_tensor->dims->data +
                                              output_tensor->dims->size));
  }

  template <typename T>
  std::vector<T> Output(const int output_index,
                        tflite::Interpreter* interpreter) const {
    TensorView<T> output_view = OutputView<T>(output_index, interpreter);
    return std::vector<T>(output_view.data(),
                          output_view.data() + output_view.size());
  }

 protected:
  explicit TfLiteModelExecutor(
      std::unique_ptr<const tflite::FlatBufferModel> model);

  std::unique_ptr<const tflite::FlatBufferModel> model_;
  std::unique_ptr<tflite::OpResolver> resolver_;
};

template <>
void TfLiteModelExecutor::SetInput(const int input_index,
                                   const std::vector<std::string>& input_data,
                                   tflite::Interpreter* interpreter) const;

template <>
std::vector<tflite::StringRef> TfLiteModelExecutor::Output(
    const int output_index, tflite::Interpreter* interpreter) const;

template <>
std::vector<std::string> TfLiteModelExecutor::Output(
    const int output_index, tflite::Interpreter* interpreter) const;

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_UTILS_TFLITE_MODEL_EXECUTOR_H_