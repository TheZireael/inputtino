#include "helpers.hpp"
#include <inputtino/input.h>

InputtinoUltimate2Joypad *inputtino_joypad_ultimate2_create(const InputtinoDeviceDefinition *device,
                                                            const InputtinoErrorHandler *eh) {
  auto joypad_ = inputtino::Ultimate2Joypad::create({
      .name = device->name ? device->name : "Inputtino virtual device",
      .vendor_id = device->vendor_id,
      .product_id = device->product_id,
      .version = device->version,
      .device_phys = device->device_phys ? device->device_phys : "",
      .device_uniq = device->device_uniq ? device->device_uniq : "",
  });
  if (joypad_) {
    return reinterpret_cast<InputtinoUltimate2Joypad *>(new inputtino::Ultimate2Joypad(std::move(*joypad_)));
  } else {
    eh->eh(joypad_.getErrorMessage().c_str(), eh->user_data);
    return nullptr;
  }
}

char **inputtino_joypad_ultimate2_get_nodes(InputtinoUltimate2Joypad *joypad, int *num_nodes) {
  return c_get_nodes(joypad, num_nodes);
}

void inputtino_joypad_ultimate2_set_pressed_buttons(InputtinoUltimate2Joypad *joypad, int newly_pressed) {
  if (joypad) {
    reinterpret_cast<inputtino::Ultimate2Joypad *>(joypad)->set_pressed_buttons(newly_pressed);
  }
}

void inputtino_joypad_ultimate2_set_triggers(InputtinoUltimate2Joypad *joypad, short left_trigger, short right_trigger) {
  if (joypad) {
    reinterpret_cast<inputtino::Ultimate2Joypad *>(joypad)->set_triggers(left_trigger, right_trigger);
  }
}

void inputtino_joypad_ultimate2_set_stick(InputtinoUltimate2Joypad *joypad,
                                          enum INPUTTINO_JOYPAD_STICK_POSITION stick_type,
                                          short x,
                                          short y) {
  if (joypad) {
    reinterpret_cast<inputtino::Ultimate2Joypad *>(joypad)->set_stick(inputtino::Joypad::STICK_POSITION(stick_type),
                                                                      x,
                                                                      y);
  }
}

void inputtino_joypad_ultimate2_set_on_rumble(InputtinoUltimate2Joypad *joypad,
                                              InputtinoJoypadRumbleFn rumble_fn,
                                              void *user_data) {
  if (joypad) {
    reinterpret_cast<inputtino::Ultimate2Joypad *>(joypad)->set_on_rumble(
        [user_data, rumble_fn](short left, short right) { rumble_fn(left, right, user_data); });
  }
}

void inputtino_joypad_ultimate2_set_motion(
    InputtinoUltimate2Joypad *joypad, enum INPUTTINO_JOYPAD_MOTION_TYPE motion_type, float x, float y, float z) {
  if (joypad) {
    reinterpret_cast<inputtino::Ultimate2Joypad *>(joypad)->set_motion(
        inputtino::Ultimate2Joypad::MOTION_TYPE(motion_type), x, y, z);
  }
}

void inputtino_joypad_ultimate2_destroy(InputtinoUltimate2Joypad *joypad) {
  if (joypad) {
    auto joypad_ptr = reinterpret_cast<inputtino::Ultimate2Joypad *>(joypad);
    delete joypad_ptr;
  }
}
