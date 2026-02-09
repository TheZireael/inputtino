#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <endian.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <inputtino/input.hpp>
#include <sstream>
#include <uhid/protected_types.hpp>
#include <uhid/uhid.hpp>
#include <uhid/ultimate2.hpp>

namespace inputtino {

static void send_report(Ultimate2JoypadState &state) {
  struct uhid_event ev {};
  ev.type = UHID_INPUT2;
  ev.u.input2.data[0] = uhid::ULTIMATE2_REPORT_ID_INPUT;
  auto data = reinterpret_cast<unsigned char *>(&state.current_state);
  std::copy(data, data + sizeof(state.current_state), &ev.u.input2.data[1]);
  ev.u.input2.size = sizeof(state.current_state) + 1;
  state.dev->send(ev);
}

static void on_uhid_event(std::shared_ptr<Ultimate2JoypadState> state, uhid_event ev, int fd) {
  (void)fd;
  switch (ev.type) {
  case UHID_OUTPUT: {
    if (ev.u.output.size < 2) {
      break;
    }
    if (ev.u.output.data[0] != uhid::ULTIMATE2_REPORT_ID_OUTPUT) {
      break;
    }
    if (state->on_rumble && ev.u.output.size >= 3) {
      auto left = static_cast<int>(ev.u.output.data[1] * 0xFFFF / 100.0f);
      auto right = static_cast<int>(ev.u.output.data[2] * 0xFFFF / 100.0f);
      (*state->on_rumble)(left, right);
    }
    break;
  }
  default:
    break;
  }
}

Ultimate2Joypad::Ultimate2Joypad(uint16_t vendor_id, uint16_t product_id, std::string uniq)
    : _state(std::make_shared<Ultimate2JoypadState>()) {
  _state->vendor_id = vendor_id;
  _state->product_id = product_id;
  _state->uniq = std::move(uniq);
  _state->current_state.hat = uhid::ULTIMATE2_HAT_NEUTRAL;
  _state->current_state.x = uhid::ULTIMATE2_AXIS_NEUTRAL;
  _state->current_state.y = uhid::ULTIMATE2_AXIS_NEUTRAL;
  _state->current_state.z = uhid::ULTIMATE2_AXIS_NEUTRAL;
  _state->current_state.rz = uhid::ULTIMATE2_AXIS_NEUTRAL;
  _state->current_state.rt = 0;
  _state->current_state.lt = 0;
  _state->current_state.vendor[3] = 100;
}

Ultimate2Joypad::~Ultimate2Joypad() {
  if (this->_state && this->_state->dev) {
    this->_state->dev->stop_thread();
    this->_state->dev.reset();
  }
}

Result<Ultimate2Joypad> Ultimate2Joypad::create(const DeviceDefinition &device) {
  auto def = uhid::DeviceDefinition{
      .name = device.name,
      .phys = device.device_phys,
      .uniq = device.device_uniq,
      .bus = BUS_USB,
      .vendor = static_cast<uint32_t>(device.vendor_id),
      .product = static_cast<uint32_t>(device.product_id),
      .version = static_cast<uint32_t>(device.version),
      .country = 0,
      .report_description = {&uhid::ultimate2_rdesc[0], &uhid::ultimate2_rdesc[0] + sizeof(uhid::ultimate2_rdesc)}};

  auto joypad = Ultimate2Joypad(device.vendor_id, device.product_id, def.uniq);

  auto dev = uhid::Device::create(def, [state = joypad._state](uhid_event ev, int fd) { on_uhid_event(state, ev, fd); });
  if (dev) {
    joypad._state->dev = std::make_shared<uhid::Device>(std::move(*dev));
    send_report(*joypad._state);
    return joypad;
  }
  return Error(dev.getErrorMessage());
}

static int scale_value(int input, int input_start, int input_end, int output_start, int output_end) {
  auto slope = 1.0 * (output_end - output_start) / (input_end - input_start);
  return output_start + std::round(slope * (input - input_start));
}

static std::string to_hex(uint16_t value) {
  std::stringstream stream;
  stream << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << value;
  return stream.str();
}

std::vector<std::string> Ultimate2Joypad::get_sys_nodes() const {
  std::vector<std::string> nodes;
  auto base_path = "/sys/devices/virtual/misc/uhid/";
  if (std::filesystem::exists(base_path)) {
    auto uhid_entries = std::filesystem::directory_iterator{base_path};
    auto vendor_id = to_hex(this->_state->vendor_id);
    auto product_id = to_hex(this->_state->product_id);
    for (auto uhid_entry : uhid_entries) {
      auto uhid_candidate_path = uhid_entry.path().filename().string();
      if (uhid_entry.is_directory() && uhid_candidate_path.find(vendor_id) != std::string::npos &&
          uhid_candidate_path.find(product_id) != std::string::npos) {
        if (std::filesystem::exists(uhid_entry.path() / "input")) {
          auto dev_entries = std::filesystem::directory_iterator{uhid_entry.path() / "input"};
          for (auto dev_entry : dev_entries) {
            if (dev_entry.is_directory()) {
              if (!_state->uniq.empty()) {
                auto dev_uniq_path = dev_entry.path() / "uniq";
                if (std::filesystem::exists(dev_uniq_path)) {
                  std::ifstream dev_uniq_file{dev_uniq_path};
                  std::string line;
                  std::getline(dev_uniq_file, line);
                  if (line != _state->uniq) {
                    continue;
                  }
                }
              }
              nodes.push_back(dev_entry.path().string());
            }
          }
        }
      }
    }
  }
  return nodes;
}

std::vector<std::string> Ultimate2Joypad::get_nodes() const {
  std::vector<std::string> nodes;
  auto sys_nodes = get_sys_nodes();
  for (const auto &dev_entry : sys_nodes) {
    auto dev_nodes = std::filesystem::directory_iterator{dev_entry};
    for (auto dev_node : dev_nodes) {
      if (dev_node.is_directory() && (dev_node.path().filename().string().rfind("event", 0) == 0 ||
                                      dev_node.path().filename().string().rfind("js", 0) == 0)) {
        nodes.push_back(("/dev/input/" / dev_node.path().filename()).string());
      }
    }
  }
  return nodes;
}

void Ultimate2Joypad::set_pressed_buttons(unsigned int pressed) {
  uint8_t trigger_bits = this->_state->current_state.buttons[1] & 0x03;
  this->_state->current_state.buttons[0] = 0;
  this->_state->current_state.buttons[1] = trigger_bits;
  this->_state->current_state.buttons[2] = 0;

  if (DPAD_UP & pressed) {
    if (DPAD_LEFT & pressed) {
      this->_state->current_state.hat = uhid::ULTIMATE2_HAT_NW;
    } else if (DPAD_RIGHT & pressed) {
      this->_state->current_state.hat = uhid::ULTIMATE2_HAT_NE;
    } else {
      this->_state->current_state.hat = uhid::ULTIMATE2_HAT_N;
    }
  } else if (DPAD_DOWN & pressed) {
    if (DPAD_LEFT & pressed) {
      this->_state->current_state.hat = uhid::ULTIMATE2_HAT_SW;
    } else if (DPAD_RIGHT & pressed) {
      this->_state->current_state.hat = uhid::ULTIMATE2_HAT_SE;
    } else {
      this->_state->current_state.hat = uhid::ULTIMATE2_HAT_S;
    }
  } else if (DPAD_LEFT & pressed) {
    this->_state->current_state.hat = uhid::ULTIMATE2_HAT_W;
  } else if (DPAD_RIGHT & pressed) {
    this->_state->current_state.hat = uhid::ULTIMATE2_HAT_E;
  } else {
    this->_state->current_state.hat = uhid::ULTIMATE2_HAT_NEUTRAL;
  }

  if (A & pressed)
    this->_state->current_state.buttons[0] |= 0x01;
  if (B & pressed)
    this->_state->current_state.buttons[0] |= 0x02;
  if (PADDLE1_FLAG & pressed)
    this->_state->current_state.buttons[0] |= 0x04;
  if (X & pressed)
    this->_state->current_state.buttons[0] |= 0x08;
  if (Y & pressed)
    this->_state->current_state.buttons[0] |= 0x10;
  if (PADDLE2_FLAG & pressed)
    this->_state->current_state.buttons[0] |= 0x20;
  if (LEFT_BUTTON & pressed)
    this->_state->current_state.buttons[0] |= 0x40;
  if (RIGHT_BUTTON & pressed)
    this->_state->current_state.buttons[0] |= 0x80;

  if (BACK & pressed)
    this->_state->current_state.buttons[1] |= 0x04;
  if (START & pressed)
    this->_state->current_state.buttons[1] |= 0x08;
  if (HOME & pressed)
    this->_state->current_state.buttons[1] |= 0x10;
  if (LEFT_STICK & pressed)
    this->_state->current_state.buttons[1] |= 0x20;
  if (RIGHT_STICK & pressed)
    this->_state->current_state.buttons[1] |= 0x40;

  if (PADDLE3_FLAG & pressed)
    this->_state->current_state.buttons[2] |= 0x01;
  if (PADDLE4_FLAG & pressed)
    this->_state->current_state.buttons[2] |= 0x02;

  send_report(*this->_state);
}

void Ultimate2Joypad::set_triggers(int16_t left, int16_t right) {
  this->_state->current_state.lt =
      scale_value(left, 0, 255, uhid::ULTIMATE2_AXIS_MIN, uhid::ULTIMATE2_AXIS_MAX);
  this->_state->current_state.rt =
      scale_value(right, 0, 255, uhid::ULTIMATE2_AXIS_MIN, uhid::ULTIMATE2_AXIS_MAX);

  if (left == 0)
    this->_state->current_state.buttons[1] &= ~0x01;
  else
    this->_state->current_state.buttons[1] |= 0x01;

  if (right == 0)
    this->_state->current_state.buttons[1] &= ~0x02;
  else
    this->_state->current_state.buttons[1] |= 0x02;

  send_report(*this->_state);
}static inline uint8_t stick_u8_from_s16(int v) {
  // v: -32768..32767
  int32_t x = int32_t(v) + 32768;      // 0..65535
  return uint8_t((x * 255) / 65535);   // floor -> 0 maps to 127 (0x7F)
}



void Ultimate2Joypad::set_stick(Joypad::STICK_POSITION stick_type, short x, short y) {
  if (!_state) return;

  const uint8_t xu = stick_u8_from_s16(x);
  const uint8_t yu = stick_u8_from_s16(-y); // keep your existing inverted Y

  switch (stick_type) {
    case LS:
      _state->current_state.x = xu;
      _state->current_state.y = yu;
      break;
    case RS:
      _state->current_state.z  = xu;
      _state->current_state.rz = yu;
      break;
  }

  send_report(*_state);
}


void Ultimate2Joypad::set_on_rumble(const std::function<void(int, int)> &callback) {
  this->_state->on_rumble = callback;
}

void Ultimate2Joypad::set_battery(uint8_t level) {
  this->_state->current_state.vendor[3] = std::clamp<int>(level, 0, 100);
  send_report(*this->_state);
}

static uint16_t to_le_signed(int value) {
  value = std::clamp(value, static_cast<int>(SHRT_MIN), static_cast<int>(SHRT_MAX));
  return htole16(value);
}

static inline int16_t clamp_i16(long v) {
  if (v < SHRT_MIN) return SHRT_MIN;
  if (v > SHRT_MAX) return SHRT_MAX;
  return (int16_t)v;
}

void Ultimate2Joypad::set_motion(MOTION_TYPE type, float x, float y, float z) {
  int16_t v[3];

  if (type == GYROSCOPE) {
    // x,y,z are deg/s from Moonlight
    v[0] = clamp_i16(lroundf(x * 16.0f));
    v[1] = clamp_i16(lroundf(y * 16.0f));
    v[2] = clamp_i16(lroundf(z * 16.0f));
    std::memcpy(&_state->current_state.vendor[4], v, sizeof(v));
  } else {
    // x,y,z are m/s^2 from Moonlight
    constexpr float g = 9.80665f;
    v[0] = clamp_i16(lroundf((x / g) * 4096.0f));
    v[1] = clamp_i16(lroundf((y / g) * 4096.0f));
    v[2] = clamp_i16(lroundf((z / g) * 4096.0f));
    std::memcpy(&_state->current_state.vendor[10], v, sizeof(v));
  }

  send_report(*_state);
}


} // namespace inputtino
