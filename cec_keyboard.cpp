#include <iostream>
#include <signal.h>
#include <getopt.h>

#include <libcec/cec.h>
#include <libcec/cecloader.h>

#include "yaml-cpp/yaml.h"

#include "ceckeymap.h"
#include "inputdevice/inputdevice.h"

// build deps: libcec4-dev cmake libyaml-cpp-dev
// deps: libcec4 libyaml-cpp0.5v5

uint32_t RepeatRateMs       = 250;
uint32_t ReleaseDelayMs     = 0;
uint32_t DoubleTapTimeoutMs = 650;

volatile bool kill_main = false;
UserInputDevice::InputDevice* id;

void read_config_yaml(std::string config_file);

void cecKeyPress(void*, const CEC::cec_keypress* msg);

void print_usage(std::string prog_name);

void sigintHandler(int signal);

bool getCECControlCode(std::string control_code_str,
                       CEC::cec_user_control_code* cec_control_code);

bool getInputKeyCode(std::string input_key_str, int* input_key);

bool translateCECToKeyCode(CEC::cec_user_control_code cec_control_code,
                           int* input_key);

std::string getCECControlStr(CEC::cec_user_control_code cec_control_code);

void dump_keymap(void);

int main(int argc, char* argv[])
{
  if( signal(SIGINT, sigintHandler) == SIG_ERR)
  {
    std::cerr << "Could not install signal handler" << std::endl;
    return -1;
  }

  std::string cec_device_name, ui_device_name;
  int opt_return;
  bool dump_and_exit = false;
  while ((opt_return = getopt(argc, argv, "c:d:u:mh?")) != -1)
  {
    switch (opt_return)
    {
      case 'c':
        read_config_yaml(optarg);
        break;

      case 'd':
        cec_device_name = optarg;
        break;

      case 'u':
        ui_device_name = optarg;
        break;
      case 'm':
        dump_and_exit = true;
        break;
      case 'h':
      case '?':
      default:
        print_usage(argv[0]);
        return -1;
    }
  }

  if (dump_and_exit)
  {
    dump_keymap();
    return 0;
  }

  if (ui_device_name.empty())
  {
    ui_device_name = "/dev/uinput";
  }

  //create input device
  try
  {
    id = new UserInputDevice::InputDevice(ui_device_name);
  }
  catch(UserInputDevice::InputDeviceException& e)
  {
    std::cerr << "Can't open user input device: " << e.what() << std::endl;
    delete id;
    return -1;
  }

  CEC::ICECCallbacks cec_callbacks;
  CEC::libcec_configuration cec_config;
  cec_config.Clear();
  cec_callbacks.Clear();

  strcpy(cec_config.strDeviceName, "cecui_device");
  cec_config.clientVersion         = CEC::LIBCEC_VERSION_CURRENT;
  cec_config.bActivateSource       = 0;
  cec_config.iButtonRepeatRateMs   = RepeatRateMs;
  cec_config.iButtonReleaseDelayMs = ReleaseDelayMs;
  cec_config.iDoubleTapTimeoutMs   = DoubleTapTimeoutMs;
  cec_callbacks.keyPress           = &cecKeyPress;
  cec_config.callbacks             = &cec_callbacks;

  cec_config.deviceTypes.Add(CEC::CEC_DEVICE_TYPE_RECORDING_DEVICE);

  CEC::ICECAdapter* cec_adapter = LibCecInitialise(&cec_config);
  if(!cec_adapter)
  {
    std::cerr << "Cannot load libcec.so" << std::endl;
    delete id;
    return -1;
  }

  if (cec_device_name.empty())
  {

    std::cout << "No cec device port provided, attempting autodetect..."
              << std::endl;
    std::array<CEC::cec_adapter_descriptor,10> cec_devices;
    int8_t devices_found =
      cec_adapter->DetectAdapters(cec_devices.data(), 10, NULL, true);

    if( devices_found < 1)
    {
      std::cerr << "CEC device autodetection failed" << std::endl;
      UnloadLibCec(cec_adapter);
      delete id;
      return -1;
    }

    cec_device_name = cec_devices[0].strComName;
  }

  if(!cec_adapter->Open(cec_device_name.c_str()))
  {
    std::cerr << "Unable to open CEC device on port: " << cec_device_name
              << std::endl;
    delete id;
    UnloadLibCec(cec_adapter);
    return -1;
  }

  std::cout << "CEC device connected" << std::endl;
  while(!kill_main)
  {
    sleep(1);
  }

  cec_adapter->Close();
  delete id;
  UnloadLibCec(cec_adapter);
  return 0;
}


void read_config_yaml(std::string config_file)
{
  YAML::Node config;
  try
  {
    config = YAML::LoadFile(config_file);
  }
  catch (YAML::BadFile)
  {
    std::cerr << "'" << config_file << "'" << " was not found." << std::endl
              << "exiting." << std::endl;
    exit(1);
  }

  if (config["RepeatRateMs"])
  {
    RepeatRateMs = config["RepeatRateMs"].as<int>();
  }

  if (config["ReleaseDelayMs"])
  {
    ReleaseDelayMs = config["ReleaseDelayMs"].as<int>();
  }

  if (config["DoubleTapTimeoutMs"])
  {
    DoubleTapTimeoutMs = config["DoubleTapTimeoutMs"].as<int>();
  }

  if (config["keymap"])
  {
    cec_to_key.clear();
    const YAML::Node keymap = config["keymap"];

    for (YAML::const_iterator it = keymap.begin(); it != keymap.end(); it++)
    {
      std::string key = it->first.as<std::string>();
      std::string value = it->second.as<std::string>();

      CEC::cec_user_control_code control_code;
      int input_key;

      if (! (getCECControlCode(key, &control_code) &&
             getInputKeyCode(value, &input_key)) )
      {
        std::cerr << "'" << config_file
                  << "' contains the following invalid keymap pair:"
                  << std::endl << "\t\"" << key << ": " << value << "\""
                  << std::endl << "exiting." << std::endl;
        exit(1);
      }

      cec_to_key[control_code] = input_key;
    }
  }
  else
  {
    std::cerr << "keymap was not found in '" << config_file << ". "
              << "using defaults instead." << std::endl;
  }
}


void cecKeyPress(void*, const CEC::cec_keypress* msg)
{
  int input_key;
  if (translateCECToKeyCode(msg->keycode, &input_key))
  {
    if(id)
    {
      id->sendKeyInput(input_key);
    }
  }
  else
  {
    std::cout << "Unmapped key pressed! CEC code: "
              << getCECControlStr(msg->keycode) << std::endl;
  }
}


void print_usage(std::string prog_name)
{
    std::cout << std:: endl << "usage: " << prog_name << " [options]"
      << std:: endl << std:: endl << "options:"
      << std:: endl << "\t-c {file}\t- configuration yaml location"
      << std:: endl << "\t-d {device}\t- cec device port (default: autodetect)"
      << std:: endl << "\t-u {device}\t- uinput device port (default: /dev/uinput)"
      << std:: endl << "\t-m\t\t- dump config yaml and exit"
      << std:: endl << std:: endl;
}


void sigintHandler(int signal)
{
  kill_main = true;
}


bool getCECControlCode(std::string control_code_str,
                       CEC::cec_user_control_code* cec_control_code)
{
  std::map<std::string, CEC::cec_user_control_code>::iterator it =
    cec_code_map.find(control_code_str);

  if (it == cec_code_map.end())
  {
    *cec_control_code = CEC::cec_user_control_code::CEC_USER_CONTROL_CODE_UNKNOWN;
    return false;
  }

  *cec_control_code = it->second;
  return true;
}


bool getInputKeyCode(std::string input_key_str, int* input_key)
{
  std::map<std::string, int>::iterator it = input_key_map.find(input_key_str);

  if (it == input_key_map.end())
  {
    *input_key = -1;
    return false;
  }

  *input_key = it->second;
  return true;
}


bool translateCECToKeyCode(CEC::cec_user_control_code cec_control_code,
                           int* input_key)
{
  std::map<CEC::cec_user_control_code, int>::iterator it =
    cec_to_key.find(cec_control_code);

  if (it == cec_to_key.end())
  {
    *input_key = -1;
    return false;
  }

  *input_key = it->second;
  return true;
}


std::string getCECControlStr(CEC::cec_user_control_code cec_control_code)
{
  for (std::map<std::string, CEC::cec_user_control_code>::iterator it =
       cec_code_map.begin(); it != cec_code_map.end(); it++)
  {
    if (it->second == cec_control_code)
    {
      return it->first;
    }
  }

  return std::string("");
}


std::string getKeyStr(int input_key)
{
  for (std::map<std::string, int>::iterator it =
       input_key_map.begin(); it != input_key_map.end(); it++)
  {
    if (it->second == input_key)
    {
      return it->first;
    }
  }

  return std::string("");
}


void dump_keymap(void)
{
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "keymap";
  out << YAML::BeginMap;

  for (std::map<CEC::cec_user_control_code, int>::iterator it =
       cec_to_key.begin(); it != cec_to_key.end(); it++)
  {
    out << YAML::Key << getCECControlStr(it->first);
    out << YAML::Value << getKeyStr(it->second);
  }

  out << YAML::EndMap;
  out << YAML::EndMap;
  std::cout << out.c_str() << std::endl;
  return;
}
