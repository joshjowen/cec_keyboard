#ifndef INPUTDEVICE_H
#define INPUTDEVICE_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include <iostream>
#include <cstring>
#include <string>
#include <exception>

#include <linux/uinput.h>

namespace UserInputDevice
{
  class InputDevice
  {
    public:
      InputDevice(std::string uinput);
      ~InputDevice();

      void sendKeyInput(int key);

    private:
      int device_fd_;

      void emit(int type, int code, int val);
  };


  class InputDeviceException: public std::exception
  {
    private:
      std::string message_;

    public:
      InputDeviceException(const std::string& message) : message_(message)
      {
      }

      virtual const char* what() const throw()
      {
        return message_.c_str();
      }
  };
};
#endif
