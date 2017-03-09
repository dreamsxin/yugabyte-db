// Copyright (c) YugaByte, Inc.
//
// This file contains the YQLValue class that represents YQL values.

#include "yb/common/yql_value.h"

#include <cfloat>

#include <glog/logging.h>

#include "yb/common/wire_protocol.h"
#include "yb/util/date_time.h"
#include "yb/util/bytes_formatter.h"

// The list of unsupported datypes to use in switch statements
#define YQL_UNSUPPORTED_TYPES_IN_SWITCH \
  case UINT8:  FALLTHROUGH_INTENDED;    \
  case UINT16: FALLTHROUGH_INTENDED;    \
  case UINT32: FALLTHROUGH_INTENDED;    \
  case UINT64: FALLTHROUGH_INTENDED;    \
  case BINARY: FALLTHROUGH_INTENDED;    \
  case UNKNOWN_DATA

namespace yb {

using std::string;
using std::to_string;
using util::FormatBytesAsStr;

//------------------------- instance methods for abstract YQLValue class -----------------------

int YQLValue::CompareTo(const YQLValue& other) const {
  CHECK_EQ(type(), other.type());
  CHECK(!IsNull());
  CHECK(!other.IsNull());
  switch (type()) {
    case INT8:   return GenericCompare(int8_value(), other.int8_value());
    case INT16:  return GenericCompare(int16_value(), other.int16_value());
    case INT32:  return GenericCompare(int32_value(), other.int32_value());
    case INT64:  return GenericCompare(int64_value(), other.int64_value());
    case FLOAT:  return GenericCompare(float_value(), other.float_value());
    case DOUBLE: return GenericCompare(double_value(), other.double_value());
    case STRING: return string_value().compare(other.string_value());
    case BOOL:
      LOG(FATAL) << "Internal error: bool type not comparable";
      return 0;
    case TIMESTAMP:
      return GenericCompare(timestamp_value().ToInt64(), other.timestamp_value().ToInt64());

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type();
  return 0;
}

void YQLValue::Serialize(const YQLClient client, faststring* buffer) const {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  if (IsNull()) {
    CQLEncodeLength(-1, buffer);
    return;
  }

  switch (type()) {
    case INT8:
      CQLEncodeNum(Store8, int8_value(), buffer);
      return;
    case INT16:
      CQLEncodeNum(NetworkByteOrder::Store16, int16_value(), buffer);
      return;
    case INT32:
      CQLEncodeNum(NetworkByteOrder::Store32, int32_value(), buffer);
      return;
    case INT64:
      CQLEncodeNum(NetworkByteOrder::Store64, int64_value(), buffer);
      return;
    case FLOAT:
      CQLEncodeFloat(NetworkByteOrder::Store32, float_value(), buffer);
      return;
    case DOUBLE:
      CQLEncodeFloat(NetworkByteOrder::Store64, double_value(), buffer);
      return;
    case STRING:
      CQLEncodeBytes(string_value(), buffer);
      return;
    case BOOL:
      CQLEncodeNum(Store8, static_cast<uint8>(bool_value() ? 1 : 0), buffer);
      return;
    case TIMESTAMP: {
      int64_t val = DateTime::AdjustPrecision(timestamp_value().ToInt64(),
          DateTime::internal_precision,
          DateTime::CqlDateTimeInputFormat.input_precision());
      CQLEncodeNum(NetworkByteOrder::Store64, val, buffer);
      return;
    }

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type();
}

Status YQLValue::Deserialize(const DataType type, const YQLClient client, Slice* data) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  int32_t len = 0;
  RETURN_NOT_OK(CQLDecodeNum(sizeof(len), NetworkByteOrder::Load32, data, &len));
  if (len == -1) {
    SetNull();
    return Status::OK();
  }

  switch (type) {
    case INT8:
      return CQLDeserializeNum(
          len, Load8, static_cast<void (YQLValue::*)(int8_t)>(&YQLValue::set_int8_value), data);
    case INT16:
      return CQLDeserializeNum(
          len, NetworkByteOrder::Load16,
          static_cast<void (YQLValue::*)(int16_t)>(&YQLValue::set_int16_value), data);
    case INT32:
      return CQLDeserializeNum(
          len, NetworkByteOrder::Load32,
          static_cast<void (YQLValue::*)(int32_t)>(&YQLValue::set_int32_value), data);
    case INT64:
      return CQLDeserializeNum(
          len, NetworkByteOrder::Load64,
          static_cast<void (YQLValue::*)(int64_t)>(&YQLValue::set_int64_value), data);
    case FLOAT:
      return CQLDeserializeFloat(
          len, NetworkByteOrder::Load32,
          static_cast<void (YQLValue::*)(float)>(&YQLValue::set_float_value), data);
    case DOUBLE:
      return CQLDeserializeFloat(
          len, NetworkByteOrder::Load64,
          static_cast<void (YQLValue::*)(double)>(&YQLValue::set_double_value), data);
    case STRING: {
      string value;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &value));
      set_string_value(value);
      return Status::OK();
    }
    case BOOL: {
      uint8_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, Load8, data, &value));
      set_bool_value(value != 0);
      return Status::OK();
    }
    case TIMESTAMP: {
      int64_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, NetworkByteOrder::Load64, data, &value));
      value = DateTime::AdjustPrecision(value,
          DateTime::CqlDateTimeInputFormat.input_precision(), DateTime::internal_precision);
      set_timestamp_value(value);
      return Status::OK();
    }

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type;
  return STATUS(RuntimeError, "unsupported type");
}

string YQLValue::ToString() const {
  string s = DataType_Name(type()) + ":";
  if (IsNull()) {
    return s + "null";
  }

  switch (type()) {
    case INT8: return s + to_string(int8_value());
    case INT16: return s + to_string(int16_value());
    case INT32: return s + to_string(int32_value());
    case INT64: return s + to_string(int64_value());
    case FLOAT: return s + to_string(float_value());
    case DOUBLE: return s + to_string(double_value());
    case STRING: return s + FormatBytesAsStr(string_value());
    case TIMESTAMP: return s + timestamp_value().ToFormattedString();
    case BOOL: return s + (bool_value() ? "true" : "false");

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;
    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type();
  return s;
}

//------------------------- static functions for existing YQLValuePB --------------------------

DataType YQLValue::type(const YQLValuePB& v) {
  switch(v.value_case()) {
    case YQLValuePB::kInt8Value: return INT8;
    case YQLValuePB::kInt16Value: return INT16;
    case YQLValuePB::kInt32Value: return INT32;
    case YQLValuePB::kInt64Value: return INT64;
    case YQLValuePB::kFloatValue: return FLOAT;
    case YQLValuePB::kDoubleValue: return DOUBLE;
    case YQLValuePB::kStringValue: return STRING;
    case YQLValuePB::kBoolValue: return BOOL;
    case YQLValuePB::kTimestampValue: return TIMESTAMP;
    case YQLValuePB::VALUE_NOT_SET:
      break;
  }
  return UNKNOWN_DATA;
}

void YQLValue::SetNull(YQLValuePB* v) {
  switch(v->value_case()) {
    case YQLValuePB::kInt8Value:   v->clear_int8_value(); return;
    case YQLValuePB::kInt16Value:  v->clear_int16_value(); return;
    case YQLValuePB::kInt32Value:  v->clear_int32_value(); return;
    case YQLValuePB::kInt64Value:  v->clear_int64_value(); return;
    case YQLValuePB::kFloatValue:  v->clear_float_value(); return;
    case YQLValuePB::kDoubleValue: v->clear_double_value(); return;
    case YQLValuePB::kStringValue: v->clear_string_value(); return;
    case YQLValuePB::kBoolValue:   v->clear_bool_value(); return;
    case YQLValuePB::kTimestampValue: v->clear_timestamp_value(); return;
    case YQLValuePB::VALUE_NOT_SET: return;
  }
  LOG(FATAL) << "Internal error: unknown or unsupported type " << v->value_case();
}

int YQLValue::CompareTo(const YQLValuePB& lhs, const YQLValuePB& rhs) {
  CHECK(Comparable(lhs, rhs));
  CHECK(BothNotNull(lhs, rhs));
  switch (lhs.value_case()) {
    case YQLValuePB::kInt8Value:   return GenericCompare(lhs.int8_value(), rhs.int8_value());
    case YQLValuePB::kInt16Value:  return GenericCompare(lhs.int16_value(), rhs.int16_value());
    case YQLValuePB::kInt32Value:  return GenericCompare(lhs.int32_value(), rhs.int32_value());
    case YQLValuePB::kInt64Value:  return GenericCompare(lhs.int64_value(), rhs.int64_value());
    case YQLValuePB::kFloatValue:  return GenericCompare(lhs.float_value(), rhs.float_value());
    case YQLValuePB::kDoubleValue: return GenericCompare(lhs.double_value(), rhs.double_value());
    case YQLValuePB::kStringValue: return lhs.string_value().compare(rhs.string_value());
    case YQLValuePB::kBoolValue:
      LOG(FATAL) << "Internal error: bool type not comparable";
      return 0;
    case YQLValuePB::kTimestampValue:
      return GenericCompare(lhs.timestamp_value(), rhs.timestamp_value());
    case YQLValuePB::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unknown or unsupported type " << lhs.value_case();
  return 0;
}

void YQLValue::Serialize(const YQLValuePB& v, const YQLClient client, faststring* buffer) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  if (IsNull(v)) {
    CQLEncodeLength(-1, buffer);
    return;
  }

  switch (v.value_case()) {
    case YQLValuePB::kInt8Value:
      CQLEncodeNum(Store8, static_cast<int8_t>(v.int8_value()), buffer);
      return;
    case YQLValuePB::kInt16Value:
      CQLEncodeNum(NetworkByteOrder::Store16, static_cast<int16_t>(v.int16_value()), buffer);
      return;
    case YQLValuePB::kInt32Value:
      CQLEncodeNum(NetworkByteOrder::Store32, v.int32_value(), buffer);
      return;
    case YQLValuePB::kInt64Value:
      CQLEncodeNum(NetworkByteOrder::Store64, v.int64_value(), buffer);
      return;
    case YQLValuePB::kFloatValue:
      CQLEncodeFloat(NetworkByteOrder::Store32, v.float_value(), buffer);
      return;
    case YQLValuePB::kDoubleValue:
      CQLEncodeFloat(NetworkByteOrder::Store64, v.double_value(), buffer);
      return;
    case YQLValuePB::kStringValue:
      CQLEncodeBytes(v.string_value(), buffer);
      return;
    case YQLValuePB::kBoolValue:
      CQLEncodeNum(Store8, static_cast<uint8>(v.bool_value() ? 1 : 0), buffer);
      return;
    case YQLValuePB::kTimestampValue: {
      int64_t val = DateTime::AdjustPrecision(v.timestamp_value(),
          DateTime::internal_precision,
          DateTime::CqlDateTimeInputFormat.input_precision());
      CQLEncodeNum(NetworkByteOrder::Store64, val, buffer);
      return;
    }

    case YQLValuePB::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      return;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unknown or unsupported type " << v.value_case();
}

Status YQLValue::Deserialize(
    YQLValuePB* v, const DataType type, const YQLClient client, Slice* data) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  int32_t len = 0;
  RETURN_NOT_OK(CQLDecodeNum(sizeof(len), NetworkByteOrder::Load32, data, &len));
  if (len == -1) {
    SetNull(v);
    return Status::OK();
  }

  switch (type) {
    case INT8:
      return CQLDeserializeNum(v, len, Load8, YQLValue::set_int8_value, data);
    case INT16:
      return CQLDeserializeNum(v, len, NetworkByteOrder::Load16, YQLValue::set_int16_value, data);
    case INT32:
      return CQLDeserializeNum(v, len, NetworkByteOrder::Load32, YQLValue::set_int32_value, data);
    case INT64:
      return CQLDeserializeNum(v, len, NetworkByteOrder::Load64, YQLValue::set_int64_value, data);
    case FLOAT:
      return CQLDeserializeFloat(v, len, NetworkByteOrder::Load32, YQLValue::set_float_value, data);
    case DOUBLE:
      return CQLDeserializeFloat(
          v, len, NetworkByteOrder::Load64, YQLValue::set_double_value, data);
    case STRING: {
      string value;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &value));
      set_string_value(value, v);
      return Status::OK();
    }
    case BOOL: {
      uint8_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, Load8, data, &value));
      set_bool_value(value != 0, v);
      return Status::OK();
    }
    case TIMESTAMP: {
      int64_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, NetworkByteOrder::Load64, data, &value));
      value = DateTime::AdjustPrecision(value,
          DateTime::CqlDateTimeInputFormat.input_precision(), DateTime::internal_precision);
      v->set_timestamp_value(value);
      return Status::OK();
    }

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type;
  return STATUS(RuntimeError, "unsupported type");
}

string YQLValue::ToString(const YQLValuePB& v) {
  string s = DataType_Name(type(v)) + ":";
  if (IsNull(v)) {
    return s + "null";
  }

  switch (v.value_case()) {
    case YQLValuePB::kInt8Value: return s + to_string(v.int8_value());
    case YQLValuePB::kInt16Value: return s + to_string(v.int16_value());
    case YQLValuePB::kInt32Value: return s + to_string(v.int32_value());
    case YQLValuePB::kInt64Value: return s + to_string(v.int64_value());
    case YQLValuePB::kFloatValue: return s + to_string(v.float_value());
    case YQLValuePB::kDoubleValue: return s + to_string(v.double_value());
    case YQLValuePB::kStringValue: return s + FormatBytesAsStr(v.string_value());
    case YQLValuePB::kTimestampValue: return s + timestamp_value(v).ToFormattedString();
    case YQLValuePB::kBoolValue: return s + (v.bool_value() ? "true" : "false");
    case YQLValuePB::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      return s;
    // default: fall through
  }

  LOG(FATAL) << "Internal error: unknown or unsupported type " << v.value_case();
  return s;
}

//----------------------------------- YQLValuePB operators --------------------------------

#define YQL_COMPARE(lhs, rhs, op)                                       \
  do { return YQLValue::BothNotNull(lhs, rhs) && YQLValue::CompareTo(lhs, rhs) op 0; } while (0)

bool operator <(const YQLValuePB& lhs, const YQLValuePB& rhs) { YQL_COMPARE(lhs, rhs, <); }
bool operator >(const YQLValuePB& lhs, const YQLValuePB& rhs) { YQL_COMPARE(lhs, rhs, >); }
bool operator <=(const YQLValuePB& lhs, const YQLValuePB& rhs) { YQL_COMPARE(lhs, rhs, <=); }
bool operator >=(const YQLValuePB& lhs, const YQLValuePB& rhs) { YQL_COMPARE(lhs, rhs, >=); }
bool operator ==(const YQLValuePB& lhs, const YQLValuePB& rhs) { YQL_COMPARE(lhs, rhs, ==); }
bool operator !=(const YQLValuePB& lhs, const YQLValuePB& rhs) { YQL_COMPARE(lhs, rhs, !=); }

#undef YQL_COMPARE

//--------------------------------------- YQLValueWithPB -----------------------------------
YQLValueWithPB::YQLValueWithPB() {
}

YQLValueWithPB::~YQLValueWithPB() {
}

DataType YQLValueWithPB::type() const {
  return YQLValue::type(*static_cast<const YQLValuePB*>(this));
}

bool YQLValueWithPB::IsNull() const {
  return YQLValue::IsNull(*static_cast<const YQLValuePB*>(this));
}

void YQLValueWithPB::SetNull() {
  YQLValue::SetNull(static_cast<YQLValuePB*>(this));
}

int8_t YQLValueWithPB::int8_value() const {
  return YQLValue::int8_value(*static_cast<const YQLValuePB*>(this));
}

int16_t YQLValueWithPB::int16_value() const {
  return YQLValue::int16_value(*static_cast<const YQLValuePB*>(this));
}

int32_t YQLValueWithPB::int32_value() const {
  return YQLValue::int32_value(*static_cast<const YQLValuePB*>(this));
}

int64_t YQLValueWithPB::int64_value() const {
  return YQLValue::int64_value(*static_cast<const YQLValuePB*>(this));
}

float YQLValueWithPB::float_value() const {
  return YQLValue::float_value(*static_cast<const YQLValuePB*>(this));
}

double YQLValueWithPB::double_value() const {
  return YQLValue::double_value(*static_cast<const YQLValuePB*>(this));
}

bool YQLValueWithPB::bool_value() const {
  return YQLValue::bool_value(*static_cast<const YQLValuePB*>(this));
}

const string& YQLValueWithPB::string_value() const {
  return YQLValue::string_value(*static_cast<const YQLValuePB*>(this));
}

Timestamp YQLValueWithPB::timestamp_value() const {
  return YQLValue::timestamp_value(*static_cast<const YQLValuePB*>(this));
}

void YQLValueWithPB::set_int8_value(const int8_t val) {
  YQLValue::set_int8_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_int16_value(const int16_t val) {
  YQLValue::set_int16_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_int32_value(const int32_t val) {
  YQLValue::set_int32_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_int64_value(const int64_t val) {
  YQLValue::set_int64_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_float_value(const float val) {
  YQLValue::set_float_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_double_value(const double val) {
  YQLValue::set_double_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_bool_value(const bool val) {
  YQLValue::set_bool_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_string_value(const string& val) {
  YQLValue::set_string_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_string_value(const char* val) {
  YQLValue::set_string_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_string_value(const char* val, const size_t size) {
  YQLValue::set_string_value(val, size, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_timestamp_value(const Timestamp& val) {
  YQLValue::set_timestamp_value(val, static_cast<YQLValuePB*>(this));
}

void YQLValueWithPB::set_timestamp_value(const int64_t val) {
  YQLValue::set_timestamp_value(val, static_cast<YQLValuePB*>(this));
}

YQLValue& YQLValueWithPB::operator=(const YQLValuePB& other) {
  static_cast<YQLValuePB&>(*this) = other;
  return *this;
}

YQLValue& YQLValueWithPB::operator=(YQLValuePB&& other) {
  static_cast<YQLValuePB&>(*this) = std::move(other);
  return *this;
}

} // namespace yb