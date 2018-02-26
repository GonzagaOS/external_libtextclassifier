/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "text-classifier.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "model_generated.h"
#include "types-test-util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace libtextclassifier2 {
namespace {

using testing::ElementsAreArray;
using testing::Pair;
using testing::Values;

std::string FirstResult(const std::vector<ClassificationResult>& results) {
  if (results.empty()) {
    return "<INVALID RESULTS>";
  }
  return results[0].collection;
}

MATCHER_P3(IsAnnotatedSpan, start, end, best_class, "") {
  return testing::Value(arg.span, Pair(start, end)) &&
         testing::Value(FirstResult(arg.classification), best_class);
}

std::string ReadFile(const std::string& file_name) {
  std::ifstream file_stream(file_name);
  return std::string(std::istreambuf_iterator<char>(file_stream), {});
}

std::string GetModelPath() {
  return LIBTEXTCLASSIFIER_TEST_DATA_DIR;
}

TEST(TextClassifierTest, EmbeddingExecutorLoadingFails) {
  CREATE_UNILIB_FOR_TESTING;
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + "wrong_embeddings.fb", &unilib);
  EXPECT_FALSE(classifier);
}

class TextClassifierTest : public ::testing::TestWithParam<const char*> {};

INSTANTIATE_TEST_CASE_P(ClickContext, TextClassifierTest,
                        Values("test_model_cc.fb"));
INSTANTIATE_TEST_CASE_P(BoundsSensitive, TextClassifierTest,
                        Values("test_model.fb"));

TEST_P(TextClassifierTest, ClassifyText) {
  CREATE_UNILIB_FOR_TESTING;
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + GetParam(), &unilib);
  ASSERT_TRUE(classifier);

  EXPECT_EQ("other",
            FirstResult(classifier->ClassifyText(
                "this afternoon Barack Obama gave a speech at", {15, 27})));
  EXPECT_EQ("phone", FirstResult(classifier->ClassifyText(
                         "Call me at (800) 123-456 today", {11, 24})));

  // More lines.
  EXPECT_EQ("other",
            FirstResult(classifier->ClassifyText(
                "this afternoon Barack Obama gave a speech at|Visit "
                "www.google.com every today!|Call me at (800) 123-456 today.",
                {15, 27})));
  EXPECT_EQ("phone",
            FirstResult(classifier->ClassifyText(
                "this afternoon Barack Obama gave a speech at|Visit "
                "www.google.com every today!|Call me at (800) 123-456 today.",
                {90, 103})));

  // Single word.
  EXPECT_EQ("other", FirstResult(classifier->ClassifyText("obama", {0, 5})));
  EXPECT_EQ("other", FirstResult(classifier->ClassifyText("asdf", {0, 4})));
  EXPECT_EQ("<INVALID RESULTS>",
            FirstResult(classifier->ClassifyText("asdf", {0, 0})));

  // Junk.
  EXPECT_EQ("<INVALID RESULTS>",
            FirstResult(classifier->ClassifyText("", {0, 0})));
  EXPECT_EQ("<INVALID RESULTS>", FirstResult(classifier->ClassifyText(
                                     "a\n\n\n\nx x x\n\n\n\n\n\n", {1, 5})));
}

std::unique_ptr<RegexModel_::PatternT> MakePattern(
    const std::string& collection_name, const std::string& pattern,
    const bool enabled_for_classification, const bool enabled_for_selection,
    const bool enabled_for_annotation, const float score) {
  std::unique_ptr<RegexModel_::PatternT> result(new RegexModel_::PatternT);
  result->collection_name = collection_name;
  result->pattern = pattern;
  result->enabled_for_selection = enabled_for_selection;
  result->enabled_for_classification = enabled_for_classification;
  result->enabled_for_annotation = enabled_for_annotation;
  result->target_classification_score = score;
  result->priority_score = score;
  return result;
}

#ifdef LIBTEXTCLASSIFIER_UNILIB_ICU
TEST_P(TextClassifierTest, ClassifyTextRegularExpression) {
  CREATE_UNILIB_FOR_TESTING;
  const std::string test_model = ReadFile(GetModelPath() + GetParam());
  std::unique_ptr<ModelT> unpacked_model = UnPackModel(test_model.c_str());

  // Add test regex models.
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "person", "Barack Obama", /*enabled_for_classification=*/true,
      /*enabled_for_selection=*/false, /*enabled_for_annotation=*/false, 1.0));
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "flight", "[a-zA-Z]{2}\\d{2,4}", /*enabled_for_classification=*/true,
      /*enabled_for_selection=*/false, /*enabled_for_annotation=*/false, 0.5));

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Model::Pack(builder, unpacked_model.get()));

  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromUnownedBuffer(
          reinterpret_cast<const char*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib);
  ASSERT_TRUE(classifier);

  EXPECT_EQ("flight",
            FirstResult(classifier->ClassifyText(
                "Your flight LX373 is delayed by 3 hours.", {12, 17})));
  EXPECT_EQ("person",
            FirstResult(classifier->ClassifyText(
                "this afternoon Barack Obama gave a speech at", {15, 27})));
  EXPECT_EQ("email",
            FirstResult(classifier->ClassifyText("you@android.com", {0, 15})));
  EXPECT_EQ("email", FirstResult(classifier->ClassifyText(
                         "Contact me at you@android.com", {14, 29})));

  EXPECT_EQ("url", FirstResult(classifier->ClassifyText(
                       "Visit www.google.com every today!", {6, 20})));

  EXPECT_EQ("flight", FirstResult(classifier->ClassifyText("LX 37", {0, 5})));
  EXPECT_EQ("flight", FirstResult(classifier->ClassifyText("flight LX 37 abcd",
                                                           {7, 12})));

  // More lines.
  EXPECT_EQ("url",
            FirstResult(classifier->ClassifyText(
                "this afternoon Barack Obama gave a speech at|Visit "
                "www.google.com every today!|Call me at (800) 123-456 today.",
                {51, 65})));
}
#endif  // LIBTEXTCLASSIFIER_UNILIB_ICU

#ifdef LIBTEXTCLASSIFIER_UNILIB_ICU

TEST_P(TextClassifierTest, SuggestSelectionRegularExpression) {
  CREATE_UNILIB_FOR_TESTING;
  const std::string test_model = ReadFile(GetModelPath() + GetParam());
  std::unique_ptr<ModelT> unpacked_model = UnPackModel(test_model.c_str());

  // Add test regex models.
  unpacked_model->regex_model.reset(new RegexModelT);
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "person", " (Barack Obama) ", /*enabled_for_classification=*/false,
      /*enabled_for_selection=*/true, /*enabled_for_annotation=*/false, 1.0));
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "flight", "([a-zA-Z]{2} ?\\d{2,4})", /*enabled_for_classification=*/false,
      /*enabled_for_selection=*/true, /*enabled_for_annotation=*/false, 1.0));
  unpacked_model->regex_model->patterns.back()->priority_score = 1.1;

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Model::Pack(builder, unpacked_model.get()));

  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromUnownedBuffer(
          reinterpret_cast<const char*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib);
  ASSERT_TRUE(classifier);

  // Check regular expression selection.
  EXPECT_EQ(classifier->SuggestSelection(
                "Your flight MA 0123 is delayed by 3 hours.", {12, 14}),
            std::make_pair(12, 19));
  EXPECT_EQ(classifier->SuggestSelection(
                "this afternoon Barack Obama gave a speech at", {15, 21}),
            std::make_pair(15, 27));
}
#endif  // LIBTEXTCLASSIFIER_UNILIB_ICU

#ifdef LIBTEXTCLASSIFIER_UNILIB_ICU
TEST_P(TextClassifierTest,
       SuggestSelectionRegularExpressionConflictsModelWins) {
  const std::string test_model = ReadFile(GetModelPath() + GetParam());
  std::unique_ptr<ModelT> unpacked_model = UnPackModel(test_model.c_str());

  // Add test regex models.
  unpacked_model->regex_model.reset(new RegexModelT);
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "person", " (Barack Obama) ", /*enabled_for_classification=*/false,
      /*enabled_for_selection=*/true, /*enabled_for_annotation=*/false, 1.0));
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "flight", "([a-zA-Z]{2} ?\\d{2,4})", /*enabled_for_classification=*/false,
      /*enabled_for_selection=*/true, /*enabled_for_annotation=*/false, 1.0));
  unpacked_model->regex_model->patterns.back()->priority_score = 0.5;

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Model::Pack(builder, unpacked_model.get()));

  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromUnownedBuffer(
          reinterpret_cast<const char*>(builder.GetBufferPointer()),
          builder.GetSize());
  ASSERT_TRUE(classifier);

  // Check conflict resolution.
  EXPECT_EQ(
      classifier->SuggestSelection(
          "saw Barack Obama today .. 350 Third Street, Cambridge, MA 0123",
          {55, 57}),
      std::make_pair(26, 62));
}
#endif  // LIBTEXTCLASSIFIER_UNILIB_ICU

#ifdef LIBTEXTCLASSIFIER_UNILIB_ICU
TEST_P(TextClassifierTest,
       SuggestSelectionRegularExpressionConflictsRegexWins) {
  const std::string test_model = ReadFile(GetModelPath() + GetParam());
  std::unique_ptr<ModelT> unpacked_model = UnPackModel(test_model.c_str());

  // Add test regex models.
  unpacked_model->regex_model.reset(new RegexModelT);
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "person", " (Barack Obama) ", /*enabled_for_classification=*/false,
      /*enabled_for_selection=*/true, /*enabled_for_annotation=*/false, 1.0));
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "flight", "([a-zA-Z]{2} ?\\d{2,4})", /*enabled_for_classification=*/false,
      /*enabled_for_selection=*/true, /*enabled_for_annotation=*/false, 1.0));
  unpacked_model->regex_model->patterns.back()->priority_score = 1.1;

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Model::Pack(builder, unpacked_model.get()));

  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromUnownedBuffer(
          reinterpret_cast<const char*>(builder.GetBufferPointer()),
          builder.GetSize());
  ASSERT_TRUE(classifier);

  // Check conflict resolution.
  EXPECT_EQ(
      classifier->SuggestSelection(
          "saw Barack Obama today .. 350 Third Street, Cambridge, MA 0123",
          {55, 57}),
      std::make_pair(55, 62));
}
#endif  // LIBTEXTCLASSIFIER_UNILIB_ICU

#ifdef LIBTEXTCLASSIFIER_UNILIB_ICU
TEST_P(TextClassifierTest, AnnotateRegex) {
  CREATE_UNILIB_FOR_TESTING;
  const std::string test_model = ReadFile(GetModelPath() + GetParam());
  std::unique_ptr<ModelT> unpacked_model = UnPackModel(test_model.c_str());

  // Add test regex models.
  unpacked_model->regex_model.reset(new RegexModelT);
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "person", " (Barack Obama) ", /*enabled_for_classification=*/false,
      /*enabled_for_selection=*/false, /*enabled_for_annotation=*/true, 1.0));
  unpacked_model->regex_model->patterns.push_back(MakePattern(
      "flight", "([a-zA-Z]{2} ?\\d{2,4})", /*enabled_for_classification=*/false,
      /*enabled_for_selection=*/false, /*enabled_for_annotation=*/true, 0.5));
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Model::Pack(builder, unpacked_model.get()));

  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromUnownedBuffer(
          reinterpret_cast<const char*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib);
  ASSERT_TRUE(classifier);

  const std::string test_string =
      "& saw Barack Obama today .. 350 Third Street, Cambridge\nand my phone "
      "number is 853 225 3556";
  EXPECT_THAT(classifier->Annotate(test_string),
              ElementsAreArray({
                  IsAnnotatedSpan(6, 18, "person"),
                  IsAnnotatedSpan(19, 24, "date"),
                  IsAnnotatedSpan(28, 55, "address"),
                  IsAnnotatedSpan(79, 91, "phone"),
              }));
}

#endif  // LIBTEXTCLASSIFIER_UNILIB_ICU

TEST_P(TextClassifierTest, PhoneFiltering) {
  CREATE_UNILIB_FOR_TESTING;
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + GetParam(), &unilib);
  ASSERT_TRUE(classifier);

  EXPECT_EQ("phone", FirstResult(classifier->ClassifyText(
                         "phone: (123) 456 789", {7, 20})));
  EXPECT_EQ("phone", FirstResult(classifier->ClassifyText(
                         "phone: (123) 456 789,0001112", {7, 25})));
  EXPECT_EQ("other", FirstResult(classifier->ClassifyText(
                         "phone: (123) 456 789,0001112", {7, 28})));
}

TEST_P(TextClassifierTest, SuggestSelection) {
  CREATE_UNILIB_FOR_TESTING;
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + GetParam(), &unilib);
  ASSERT_TRUE(classifier);

  EXPECT_EQ(classifier->SuggestSelection(
                "this afternoon Barack Obama gave a speech at", {15, 21}),
            std::make_pair(15, 21));

  // Try passing whole string.
  // If more than 1 token is specified, we should return back what entered.
  EXPECT_EQ(
      classifier->SuggestSelection("350 Third Street, Cambridge", {0, 27}),
      std::make_pair(0, 27));

  // Single letter.
  EXPECT_EQ(classifier->SuggestSelection("a", {0, 1}), std::make_pair(0, 1));

  // Single word.
  EXPECT_EQ(classifier->SuggestSelection("asdf", {0, 4}), std::make_pair(0, 4));

  EXPECT_EQ(
      classifier->SuggestSelection("call me at 857 225 3556 today", {11, 14}),
      std::make_pair(11, 23));

  // Unpaired bracket stripping.
  EXPECT_EQ(
      classifier->SuggestSelection("call me at (857) 225 3556 today", {11, 16}),
      std::make_pair(11, 25));
  EXPECT_EQ(classifier->SuggestSelection("call me at (857 today", {11, 15}),
            std::make_pair(12, 15));
  EXPECT_EQ(classifier->SuggestSelection("call me at 3556) today", {11, 16}),
            std::make_pair(11, 15));
  EXPECT_EQ(classifier->SuggestSelection("call me at )857( today", {11, 16}),
            std::make_pair(12, 15));

  // If the resulting selection would be empty, the original span is returned.
  EXPECT_EQ(classifier->SuggestSelection("call me at )( today", {11, 13}),
            std::make_pair(11, 13));
  EXPECT_EQ(classifier->SuggestSelection("call me at ( today", {11, 12}),
            std::make_pair(11, 12));
  EXPECT_EQ(classifier->SuggestSelection("call me at ) today", {11, 12}),
            std::make_pair(11, 12));
}

TEST_P(TextClassifierTest, SuggestSelectionsAreSymmetric) {
  CREATE_UNILIB_FOR_TESTING;
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + GetParam(), &unilib);
  ASSERT_TRUE(classifier);

  EXPECT_EQ(classifier->SuggestSelection("350 Third Street, Cambridge", {0, 3}),
            std::make_pair(0, 27));
  EXPECT_EQ(classifier->SuggestSelection("350 Third Street, Cambridge", {4, 9}),
            std::make_pair(0, 27));
  EXPECT_EQ(
      classifier->SuggestSelection("350 Third Street, Cambridge", {10, 16}),
      std::make_pair(0, 27));
  EXPECT_EQ(classifier->SuggestSelection("a\nb\nc\n350 Third Street, Cambridge",
                                         {16, 22}),
            std::make_pair(6, 33));
}

TEST_P(TextClassifierTest, SuggestSelectionWithNewLine) {
  CREATE_UNILIB_FOR_TESTING;
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + GetParam(), &unilib);
  ASSERT_TRUE(classifier);

  EXPECT_EQ(classifier->SuggestSelection("abc\n857 225 3556", {4, 7}),
            std::make_pair(4, 16));
  EXPECT_EQ(classifier->SuggestSelection("857 225 3556\nabc", {0, 3}),
            std::make_pair(0, 12));

  SelectionOptions options;
  EXPECT_EQ(classifier->SuggestSelection("857 225\n3556\nabc", {0, 3}, options),
            std::make_pair(0, 7));
}

TEST_P(TextClassifierTest, SuggestSelectionWithPunctuation) {
  CREATE_UNILIB_FOR_TESTING;
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + GetParam(), &unilib);
  ASSERT_TRUE(classifier);

  // From the right.
  EXPECT_EQ(classifier->SuggestSelection(
                "this afternoon BarackObama, gave a speech at", {15, 26}),
            std::make_pair(15, 26));

  // From the right multiple.
  EXPECT_EQ(classifier->SuggestSelection(
                "this afternoon BarackObama,.,.,, gave a speech at", {15, 26}),
            std::make_pair(15, 26));

  // From the left multiple.
  EXPECT_EQ(classifier->SuggestSelection(
                "this afternoon ,.,.,,BarackObama gave a speech at", {21, 32}),
            std::make_pair(21, 32));

  // From both sides.
  EXPECT_EQ(classifier->SuggestSelection(
                "this afternoon !BarackObama,- gave a speech at", {16, 27}),
            std::make_pair(16, 27));
}

TEST_P(TextClassifierTest, SuggestSelectionNoCrashWithJunk) {
  CREATE_UNILIB_FOR_TESTING;
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + GetParam(), &unilib);
  ASSERT_TRUE(classifier);

  // Try passing in bunch of invalid selections.
  EXPECT_EQ(classifier->SuggestSelection("", {0, 27}), std::make_pair(0, 27));
  EXPECT_EQ(classifier->SuggestSelection("", {-10, 27}),
            std::make_pair(-10, 27));
  EXPECT_EQ(classifier->SuggestSelection("Word 1 2 3 hello!", {0, 27}),
            std::make_pair(0, 27));
  EXPECT_EQ(classifier->SuggestSelection("Word 1 2 3 hello!", {-30, 300}),
            std::make_pair(-30, 300));
  EXPECT_EQ(classifier->SuggestSelection("Word 1 2 3 hello!", {-10, -1}),
            std::make_pair(-10, -1));
  EXPECT_EQ(classifier->SuggestSelection("Word 1 2 3 hello!", {100, 17}),
            std::make_pair(100, 17));
}

TEST_P(TextClassifierTest, Annotate) {
  CREATE_UNILIB_FOR_TESTING;
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + GetParam(), &unilib);
  ASSERT_TRUE(classifier);

  const std::string test_string =
      "& saw Barack Obama today .. 350 Third Street, Cambridge\nand my phone "
      "number is 853 225 3556";
  EXPECT_THAT(classifier->Annotate(test_string),
              ElementsAreArray({
#ifdef LIBTEXTCLASSIFIER_UNILIB_ICU
                  IsAnnotatedSpan(19, 24, "date"),
#endif
                  IsAnnotatedSpan(28, 55, "address"),
                  IsAnnotatedSpan(79, 91, "phone"),
              }));

  AnnotationOptions options;
  EXPECT_THAT(classifier->Annotate("853 225 3556", options),
              ElementsAreArray({IsAnnotatedSpan(0, 12, "phone")}));
  EXPECT_TRUE(classifier->Annotate("853 225\n3556", options).empty());
}

TEST_P(TextClassifierTest, AnnotateSmallBatches) {
  CREATE_UNILIB_FOR_TESTING;
  const std::string test_model = ReadFile(GetModelPath() + GetParam());
  std::unique_ptr<ModelT> unpacked_model = UnPackModel(test_model.c_str());

  // Set the batch size.
  unpacked_model->selection_options->batch_size = 4;
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Model::Pack(builder, unpacked_model.get()));

  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromUnownedBuffer(
          reinterpret_cast<const char*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib);
  ASSERT_TRUE(classifier);

  const std::string test_string =
      "& saw Barack Obama today .. 350 Third Street, Cambridge\nand my phone "
      "number is 853 225 3556";
  EXPECT_THAT(classifier->Annotate(test_string),
              ElementsAreArray({
#ifdef LIBTEXTCLASSIFIER_UNILIB_ICU
                  IsAnnotatedSpan(19, 24, "date"),
#endif
                  IsAnnotatedSpan(28, 55, "address"),
                  IsAnnotatedSpan(79, 91, "phone"),
              }));

  AnnotationOptions options;
  EXPECT_THAT(classifier->Annotate("853 225 3556", options),
              ElementsAreArray({IsAnnotatedSpan(0, 12, "phone")}));
  EXPECT_TRUE(classifier->Annotate("853 225\n3556", options).empty());
}

TEST_P(TextClassifierTest, AnnotateFilteringDiscardAll) {
  CREATE_UNILIB_FOR_TESTING;
  const std::string test_model = ReadFile(GetModelPath() + GetParam());
  std::unique_ptr<ModelT> unpacked_model = UnPackModel(test_model.c_str());

  // Add test thresholds.
  unpacked_model->triggering_options.reset(new ModelTriggeringOptionsT);
  unpacked_model->triggering_options->min_annotate_confidence =
      2.f;  // Discards all results.
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Model::Pack(builder, unpacked_model.get()));

  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromUnownedBuffer(
          reinterpret_cast<const char*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib);
  ASSERT_TRUE(classifier);

  const std::string test_string =
      "& saw Barack Obama today .. 350 Third Street, Cambridge\nand my phone "
      "number is 853 225 3556";
  EXPECT_TRUE(classifier->Annotate(test_string).empty());
}

TEST_P(TextClassifierTest, AnnotateFilteringKeepAll) {
  CREATE_UNILIB_FOR_TESTING;
  const std::string test_model = ReadFile(GetModelPath() + GetParam());
  std::unique_ptr<ModelT> unpacked_model = UnPackModel(test_model.c_str());

  // Add test thresholds.
  unpacked_model->triggering_options.reset(new ModelTriggeringOptionsT);
  unpacked_model->triggering_options->min_annotate_confidence =
      0.f;  // Keeps all results.
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Model::Pack(builder, unpacked_model.get()));

  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromUnownedBuffer(
          reinterpret_cast<const char*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib);
  ASSERT_TRUE(classifier);

  const std::string test_string =
      "& saw Barack Obama today .. 350 Third Street, Cambridge\nand my phone "
      "number is 853 225 3556";
#ifdef LIBTEXTCLASSIFIER_UNILIB_ICU
  EXPECT_EQ(classifier->Annotate(test_string).size(), 3);
#else
  // In non-ICU mode there is no "date" result.
  EXPECT_EQ(classifier->Annotate(test_string).size(), 2);
#endif
}

#ifdef LIBTEXTCLASSIFIER_CALENDAR_ICU
TEST_P(TextClassifierTest, ClassifyTextDate) {
  std::unique_ptr<TextClassifier> classifier =
      TextClassifier::FromPath(GetModelPath() + GetParam());
  EXPECT_TRUE(classifier);

  std::vector<ClassificationResult> result;
  ClassificationOptions options;

  options.reference_timezone = "Europe/Zurich";
  result = classifier->ClassifyText("january 1, 2017", {0, 15}, options);

  ASSERT_EQ(result.size(), 1);
  EXPECT_THAT(result[0].collection, "date");
  EXPECT_EQ(result[0].datetime_parse_result.time_ms_utc, 1483225200000);
  EXPECT_EQ(result[0].datetime_parse_result.granularity,
            DatetimeGranularity::GRANULARITY_DAY);
  result.clear();

  options.reference_timezone = "America/Los_Angeles";
  result = classifier->ClassifyText("march 1, 2017", {0, 13}, options);
  ASSERT_EQ(result.size(), 1);
  EXPECT_THAT(result[0].collection, "date");
  EXPECT_EQ(result[0].datetime_parse_result.time_ms_utc, 1488355200000);
  EXPECT_EQ(result[0].datetime_parse_result.granularity,
            DatetimeGranularity::GRANULARITY_DAY);
  result.clear();

  options.reference_timezone = "America/Los_Angeles";
  result = classifier->ClassifyText("2018/01/01 10:30:20", {0, 19}, options);
  ASSERT_EQ(result.size(), 1);
  EXPECT_THAT(result[0].collection, "date");
  EXPECT_EQ(result[0].datetime_parse_result.time_ms_utc, 1514831420000);
  EXPECT_EQ(result[0].datetime_parse_result.granularity,
            DatetimeGranularity::GRANULARITY_SECOND);
  result.clear();

  // Date on another line.
  options.reference_timezone = "Europe/Zurich";
  result = classifier->ClassifyText(
      "hello world this is the first line\n"
      "january 1, 2017",
      {35, 50}, options);
  ASSERT_EQ(result.size(), 1);
  EXPECT_THAT(result[0].collection, "date");
  EXPECT_EQ(result[0].datetime_parse_result.time_ms_utc, 1483225200000);
  EXPECT_EQ(result[0].datetime_parse_result.granularity,
            DatetimeGranularity::GRANULARITY_DAY);
  result.clear();
}
#endif  // LIBTEXTCLASSIFIER_UNILIB_ICU

class TestingTextClassifier : public TextClassifier {
 public:
  TestingTextClassifier(const std::string& model, const UniLib* unilib)
      : TextClassifier(ViewModel(model.data(), model.size()), unilib) {}

  using TextClassifier::ResolveConflicts;
};

AnnotatedSpan MakeAnnotatedSpan(CodepointSpan span,
                                const std::string& collection,
                                const float score) {
  AnnotatedSpan result;
  result.span = span;
  result.classification.push_back({collection, score});
  return result;
}

TEST(TextClassifierTest, ResolveConflictsTrivial) {
  CREATE_UNILIB_FOR_TESTING;
  TestingTextClassifier classifier("", &unilib);

  std::vector<AnnotatedSpan> candidates{
      {MakeAnnotatedSpan({0, 1}, "phone", 1.0)}};

  std::vector<int> chosen;
  classifier.ResolveConflicts(candidates, /*context=*/"", &chosen);
  EXPECT_THAT(chosen, ElementsAreArray({0}));
}

TEST(TextClassifierTest, ResolveConflictsSequence) {
  CREATE_UNILIB_FOR_TESTING;
  TestingTextClassifier classifier("", &unilib);

  std::vector<AnnotatedSpan> candidates{{
      MakeAnnotatedSpan({0, 1}, "phone", 1.0),
      MakeAnnotatedSpan({1, 2}, "phone", 1.0),
      MakeAnnotatedSpan({2, 3}, "phone", 1.0),
      MakeAnnotatedSpan({3, 4}, "phone", 1.0),
      MakeAnnotatedSpan({4, 5}, "phone", 1.0),
  }};

  std::vector<int> chosen;
  classifier.ResolveConflicts(candidates, /*context=*/"", &chosen);
  EXPECT_THAT(chosen, ElementsAreArray({0, 1, 2, 3, 4}));
}

TEST(TextClassifierTest, ResolveConflictsThreeSpans) {
  CREATE_UNILIB_FOR_TESTING;
  TestingTextClassifier classifier("", &unilib);

  std::vector<AnnotatedSpan> candidates{{
      MakeAnnotatedSpan({0, 3}, "phone", 1.0),
      MakeAnnotatedSpan({1, 5}, "phone", 0.5),  // Looser!
      MakeAnnotatedSpan({3, 7}, "phone", 1.0),
  }};

  std::vector<int> chosen;
  classifier.ResolveConflicts(candidates, /*context=*/"", &chosen);
  EXPECT_THAT(chosen, ElementsAreArray({0, 2}));
}

TEST(TextClassifierTest, ResolveConflictsThreeSpansReversed) {
  CREATE_UNILIB_FOR_TESTING;
  TestingTextClassifier classifier("", &unilib);

  std::vector<AnnotatedSpan> candidates{{
      MakeAnnotatedSpan({0, 3}, "phone", 0.5),  // Looser!
      MakeAnnotatedSpan({1, 5}, "phone", 1.0),
      MakeAnnotatedSpan({3, 7}, "phone", 0.6),  // Looser!
  }};

  std::vector<int> chosen;
  classifier.ResolveConflicts(candidates, /*context=*/"", &chosen);
  EXPECT_THAT(chosen, ElementsAreArray({1}));
}

TEST(TextClassifierTest, ResolveConflictsFiveSpans) {
  CREATE_UNILIB_FOR_TESTING;
  TestingTextClassifier classifier("", &unilib);

  std::vector<AnnotatedSpan> candidates{{
      MakeAnnotatedSpan({0, 3}, "phone", 0.5),
      MakeAnnotatedSpan({1, 5}, "other", 1.0),  // Looser!
      MakeAnnotatedSpan({3, 7}, "phone", 0.6),
      MakeAnnotatedSpan({8, 12}, "phone", 0.6),  // Looser!
      MakeAnnotatedSpan({11, 15}, "phone", 0.9),
  }};

  std::vector<int> chosen;
  classifier.ResolveConflicts(candidates, /*context=*/"", &chosen);
  EXPECT_THAT(chosen, ElementsAreArray({0, 2, 4}));
}

}  // namespace
}  // namespace libtextclassifier2