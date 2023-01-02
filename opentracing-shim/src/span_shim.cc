/*
 * Copyright The OpenTelemetry Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "span_shim.h"
#include "span_context_shim.h"
#include "tracer_shim.h"
#include "shim_utils.h"

#include "opentelemetry/trace/semantic_conventions.h"
#include "opentelemetry/trace/span_metadata.h"
#include <opentracing/ext/tags.h>

OPENTELEMETRY_BEGIN_NAMESPACE
namespace opentracingshim
{

void SpanShim::handleError(const opentracing::Value& value) noexcept
{
  using opentelemetry::trace::StatusCode;
  // The error tag MUST be mapped to StatusCode:
  // - true maps to Error.
  // - false maps to Ok
  // - no value being set maps to Unset.
  auto code = StatusCode::kUnset;
  const auto& str_value = shimutils::stringFromValue(value);

  if (str_value == "true")
  {
    code = StatusCode::kError;
  }
  else if (str_value == "false")
  {
    code = StatusCode::kOk;
  }

  span_->SetStatus(code);
}

void SpanShim::FinishWithOptions(const opentracing::FinishSpanOptions& finish_span_options) noexcept
{
  // If an explicit timestamp is specified, a conversion MUST
  // be done to match the OpenTracing and OpenTelemetry units.
  span_->End({{ finish_span_options.finish_steady_timestamp }});
}

void SpanShim::SetOperationName(opentracing::string_view name) noexcept
{
  span_->UpdateName(name.data());
}

void SpanShim::SetTag(opentracing::string_view key, const opentracing::Value& value) noexcept
{
  // Calls Set Attribute on the underlying OpenTelemetry Span with the specified key/value pair.
  if (key == opentracing::ext::error)
  {
    handleError(value);
  }
  else
  {
    span_->SetAttribute(key.data(), shimutils::attributeFromValue(value));
  }
}

void SpanShim::SetBaggageItem(opentracing::string_view restricted_key, opentracing::string_view value) noexcept
{
  // Creates a new SpanContext Shim with a new OpenTelemetry Baggage containing the specified
  // Baggage key/value pair, and sets it as the current instance for this Span Shim.
  const std::lock_guard<decltype(context_lock_)> guard(context_lock_);
  context_ = context_.newWithKeyValue(restricted_key.data(), value.data());;
}

std::string SpanShim::BaggageItem(opentracing::string_view restricted_key) const noexcept
{
  // Returns the value for the specified key in the OpenTelemetry Baggage
  // of the current SpanContext Shim, or null if none exists.
  const std::lock_guard<decltype(context_lock_)> guard(context_lock_);
  std::string value;
  return context_.BaggageItem(restricted_key.data(), value) ? value : "";
}

void SpanShim::Log(std::initializer_list<EventEntry> fields) noexcept
{
  // If an explicit timestamp is specified, a conversion MUST
  // be done to match the OpenTracing and OpenTelemetry units.
  logImpl(opentracing::SystemTime::min(), fields);
}

void SpanShim::Log(opentracing::SystemTime timestamp, std::initializer_list<EventEntry> fields) noexcept
{
  // If an explicit timestamp is specified, a conversion MUST
  // be done to match the OpenTracing and OpenTelemetry units.
  logImpl(timestamp, fields);
}

void SpanShim::Log(opentracing::SystemTime timestamp, const std::vector<EventEntry>& fields) noexcept
{
  // If an explicit timestamp is specified, a conversion MUST
  // be done to match the OpenTracing and OpenTelemetry units.
  logImpl(timestamp, fields);
}

void SpanShim::logImpl(opentracing::SystemTime timestamp, nostd::span<const EventEntry> fields) noexcept
{
  // The Add Event’s name parameter MUST be the value with the event key
  // in the pair set, or else fallback to use the log literal string.
  const auto event = std::find_if(fields.begin(), fields.end(), [](EventEntry item){ return item.first == "event"; });
  auto name = (event != fields.end()) ? shimutils::stringFromValue(event->second) : std::string{"log"};
  // If pair set contains an event=error entry, the values MUST be mapped to an Event
  // with the conventions outlined in the Exception semantic conventions document:
  bool is_error = (name == opentracing::ext::error);
  // A call to AddEvent is performed with name being set to exception
  if (is_error) name = "exception";
  // Along the specified key/value pair set as additional event attributes...
  std::vector<std::pair<std::string, common::AttributeValue>> attributes;
  attributes.reserve(fields.size());

  for (const auto& entry : fields)
  {
    auto key = entry.first;
    const auto& value = shimutils::attributeFromValue(entry.second);
    // ... including mapping of the following key/value pairs:
    // - error.kind maps to exception.type.
    // - message maps to exception.message.
    // - stack maps to exception.stacktrace.
    if (is_error)
    {
      if (key == "error.kind")
      {
        key = opentelemetry::trace::SemanticConventions::kExceptionType;
      }
      else if (key == "message")
      {
        key = opentelemetry::trace::SemanticConventions::kExceptionMessage;
      }
      else if (key == "stack")
      {
        key = opentelemetry::trace::SemanticConventions::kExceptionStacktrace;
      }
    }

    attributes.emplace_back(key, value);
  }
  // Calls Add Events on the underlying OpenTelemetry Span with the specified key/value pair set.
  if (timestamp != opentracing::SystemTime::min())
  {
    span_->AddEvent(name, timestamp, attributes);
  }
  else
  {
    span_->AddEvent(name, attributes);
  }
}

} // namespace opentracingshim
OPENTELEMETRY_END_NAMESPACE