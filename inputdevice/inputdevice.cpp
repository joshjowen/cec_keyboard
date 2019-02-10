#include "inputdevice.h"

namespace UserInputDevice
{
  InputDevice::InputDevice(std::string uinput)
  {
    struct input_id uid;
    memset(&uid, 0, sizeof(uid));

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));

    device_fd_ = open(uinput.c_str(), O_WRONLY | O_NONBLOCK);

    if (device_fd_ < 0)
    {
      throw InputDeviceException(strerror(errno));
    }
    ioctl(device_fd_, UI_SET_EVBIT, EV_KEY);

    for (int i = 0; i < 256; i++)
    {
      ioctl(device_fd_, UI_SET_KEYBIT, i);
    }

    usetup.id = uid;
    strcpy(usetup.name, "ui_device");

    ioctl(device_fd_, UI_DEV_SETUP, &usetup);
    ioctl(device_fd_, UI_DEV_CREATE);
  }


  InputDevice::~InputDevice(void)
  {
    if (device_fd_ > 0)
    {
      close(device_fd_);
    }
  }


  void InputDevice::emit(int type, int code, int val)
  {
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = val;

    if (write(device_fd_, &ie, sizeof(ie)) < 0)
    {
      throw InputDeviceException(strerror(errno));
    }
  }


  void InputDevice::sendKeyInput(int key)
  {
    emit(EV_KEY, key, 1);
    emit(EV_SYN, SYN_REPORT, 0);
    emit(EV_KEY, key, 0);
    emit(EV_SYN, SYN_REPORT, 0);
  }
};
