#pragma once

#include <cstdint>
#include <limits>

namespace hcq::ot_sim {

class OpenThermStartSequenceDiagnostics {
 public:
  static constexpr uint8_t STATUS_ID = 0;
  static constexpr uint8_t TSET_ID = 1;

  enum class RequestType : uint8_t {
    OTHER,
    READ_DATA,
    WRITE_DATA,
  };

  void observe_valid_request(uint8_t request_id, RequestType request_type,
                             uint16_t data, uint64_t frame_end_us,
                             bool ch_enable_rising) {
    if (ignore_through_frame_end_us_ != 0 &&
        frame_end_us <= ignore_through_frame_end_us_) {
      return;
    }

    if (ch_enable_rising && request_id == STATUS_ID &&
        request_type == RequestType::READ_DATA) {
      ch_enable_rising_count_++;
      last_start_present_ = true;
      last_start_previous_request_id_ =
          previous_request_present_ ? static_cast<int16_t>(previous_request_id_) : -1;
      last_start_preceded_by_tset_ =
          previous_request_present_ && previous_request_id_ == TSET_ID &&
          previous_request_type_ == RequestType::WRITE_DATA;

      if (last_start_preceded_by_tset_) {
        ch_enable_rising_after_tset_count_++;
        const uint64_t interval_us =
            frame_end_us >= previous_request_end_us_
                ? frame_end_us - previous_request_end_us_
                : 0;
        last_tset_to_ch_enable_interval_us_ =
            interval_us > std::numeric_limits<uint32_t>::max()
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(interval_us);
        last_start_tset_data_ = previous_request_data_;
      } else {
        last_tset_to_ch_enable_interval_us_ = 0;
        last_start_tset_data_ = 0;
      }
    }

    previous_request_present_ = true;
    previous_request_id_ = request_id;
    previous_request_type_ = request_type;
    previous_request_data_ = data;
    previous_request_end_us_ = frame_end_us;
  }

  void reset(uint64_t ignore_through_frame_end_us = 0) {
    previous_request_present_ = false;
    previous_request_id_ = 0;
    previous_request_type_ = RequestType::OTHER;
    previous_request_data_ = 0;
    previous_request_end_us_ = 0;
    ignore_through_frame_end_us_ = ignore_through_frame_end_us;
    ch_enable_rising_count_ = 0;
    ch_enable_rising_after_tset_count_ = 0;
    last_start_present_ = false;
    last_start_preceded_by_tset_ = false;
    last_start_previous_request_id_ = -1;
    last_tset_to_ch_enable_interval_us_ = 0;
    last_start_tset_data_ = 0;
  }

  uint32_t ch_enable_rising_count() const { return ch_enable_rising_count_; }
  uint32_t ch_enable_rising_after_tset_count() const {
    return ch_enable_rising_after_tset_count_;
  }
  bool has_last_start() const { return last_start_present_; }
  bool last_start_preceded_by_tset() const {
    return last_start_preceded_by_tset_;
  }
  int last_start_previous_request_id() const {
    return last_start_previous_request_id_;
  }
  uint32_t last_tset_to_ch_enable_interval_us() const {
    return last_tset_to_ch_enable_interval_us_;
  }
  uint16_t last_start_tset_data() const { return last_start_tset_data_; }

 private:
  bool previous_request_present_ = false;
  uint8_t previous_request_id_ = 0;
  RequestType previous_request_type_ = RequestType::OTHER;
  uint16_t previous_request_data_ = 0;
  uint64_t previous_request_end_us_ = 0;
  uint64_t ignore_through_frame_end_us_ = 0;

  uint32_t ch_enable_rising_count_ = 0;
  uint32_t ch_enable_rising_after_tset_count_ = 0;
  bool last_start_present_ = false;
  bool last_start_preceded_by_tset_ = false;
  int16_t last_start_previous_request_id_ = -1;
  uint32_t last_tset_to_ch_enable_interval_us_ = 0;
  uint16_t last_start_tset_data_ = 0;
};

}  // namespace hcq::ot_sim
