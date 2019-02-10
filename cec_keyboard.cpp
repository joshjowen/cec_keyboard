#include <iostream>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>
#include <mutex>
#include <atomic>
#include <queue>

#include <libcec/cec.h>
#include <libcec/cecloader.h>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <json/json.h>

#include <yaml-cpp/yaml.h>

#include "ceckeymap.h"
#include "inputdevice/inputdevice.h"

// build deps: libcec4-dev cmake libyaml-cpp-dev libwebsocketpp-dev libboost-system-dev libjsoncpp-dev
// deps: libcec4 libyaml-cpp0.5v5 MAYBE boost-system libjsoncpp1

uint32_t cecRepeatRateMs       = 250;
uint32_t cecReleaseDelayMs     = 0;
uint32_t cecDoubleTapTimeoutMs = 650;
std::string cecDeviceName      = "cec_keyboard";
int ws_port = -1;

volatile std::atomic<bool> kill_main;
std::mutex key_mutex;
std::queue<int> key_queue;

CEC::ICECAdapter* cec_adapter;
websocketpp::server<websocketpp::config::asio> ws_server;


void* ws_loop(void*);

void read_config_yaml(std::string config_file);

void cecKeyPressCB(void*, const CEC::cec_keypress* msg);

bool execCECCommand(std::string cmd, std::string args, std::string response);

void wsMessageCB(websocketpp::server<websocketpp::config::asio>* s,
                 websocketpp::connection_hdl hdl,
                 websocketpp::server<websocketpp::config::asio>::message_ptr msg);

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
  kill_main = false;
  long int raw_port;

  if( signal(SIGINT, sigintHandler) == SIG_ERR)
  {
    std::cerr << "Could not install signal handler" << std::endl;
    return -1;
  }

  std::string cec_device_name, ui_device_name;
  int opt_return;
  bool dump_and_exit = false;
  while ((opt_return = getopt(argc, argv, "c:d:u:p:n:mh?")) != -1)
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
      case 'p':
        char *remain;
        errno = 0;
        raw_port = strtol(optarg, &remain, 10);

        if ((errno != 0) || (*remain != '\0') || (raw_port < 0)
                                            || (raw_port > INT_MAX))
        {
          std::cout << "invalid websocket port provided:" << raw_port
                                                          << std::endl;
          return -1;

        }
        ws_port = raw_port;
        break;
      case 'n':
        if (strlen(optarg) <= 13)
        {
          cecDeviceName = optarg;
        }
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
  UserInputDevice::InputDevice* id;

  try
  {
    id = new UserInputDevice::InputDevice(ui_device_name);
  }
  catch(UserInputDevice::InputDeviceException& e)
  {
    std::cerr << "Can't open user input device: " << e.what() << std::endl;
    return -1;
  }

  CEC::ICECCallbacks cec_callbacks;
  CEC::libcec_configuration cec_config;
  cec_config.Clear();
  cec_callbacks.Clear();

  strcpy(cec_config.strDeviceName, cecDeviceName.c_str());
  cec_config.clientVersion         = CEC::LIBCEC_VERSION_CURRENT;
  cec_config.bActivateSource       = 0;
  cec_config.iButtonRepeatRateMs   = cecRepeatRateMs;
  cec_config.iButtonReleaseDelayMs = cecReleaseDelayMs;
  cec_config.iDoubleTapTimeoutMs   = cecDoubleTapTimeoutMs;
  cec_callbacks.keyPress           = &cecKeyPressCB;
  cec_config.callbacks             = &cec_callbacks;
  cec_config.deviceTypes.Add(CEC::CEC_DEVICE_TYPE_RECORDING_DEVICE);

  cec_adapter = LibCecInitialise(&cec_config);
  if(!cec_adapter)
  {
    std::cerr << "Cannot load libcec.so" << std::endl;
    delete id;
    return -1;
  }

  if (cec_device_name.empty())
  {

    std::cout << "Attempting cec device autodetect..."
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


  pthread_t ws_thread;

  if (ws_port > 0)
  {
    if (pthread_create(&ws_thread, NULL, ws_loop, NULL))
    {
      std::cout << "Unable to start websocket thread" << std::endl;
      kill_main = true;
    }
  }

  while (!kill_main)
  {
    {
      std::lock_guard<std::mutex> lock(key_mutex);

      if (!key_queue.empty())
      {
        int input_key = key_queue.front();
        key_queue.pop();
        id->sendKeyInput(input_key);
      }
    }

    usleep(5000);
  }

  ws_server.stop();
  cec_adapter->Close();
  delete id;
  UnloadLibCec(cec_adapter);
  pthread_join(ws_thread, NULL);
  return 0;
}


void* ws_loop(void*)
{
  try
  {
    ws_server.set_access_channels(websocketpp::log::alevel::fail);
    ws_server.clear_access_channels(websocketpp::log::alevel::fail);
    ws_server.init_asio();
    ws_server.set_message_handler(
      websocketpp::lib::bind(&wsMessageCB, &ws_server,
                             websocketpp::lib::placeholders::_1,
                             websocketpp::lib::placeholders::_2));
    ws_server.listen(ws_port);
    ws_server.start_accept();

    std::cout << "Websocket available on port " << ws_port << std::endl;
    ws_server.run();
  }
  catch (websocketpp::exception const & e)
  {
    std::cout << e.what() << std::endl;
    kill_main = true;
  }

  pthread_exit(NULL);
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
    cecRepeatRateMs = config["RepeatRateMs"].as<int>();
  }

  if (config["ReleaseDelayMs"])
  {
    cecReleaseDelayMs = config["ReleaseDelayMs"].as<int>();
  }

  if (config["DoubleTapTimeoutMs"])
  {
    cecDoubleTapTimeoutMs = config["DoubleTapTimeoutMs"].as<int>();
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


void cecKeyPressCB(void*, const CEC::cec_keypress* msg)
{
  int input_key;
  if (translateCECToKeyCode(msg->keycode, &input_key))
  {
    std::lock_guard<std::mutex> lock(key_mutex);
    key_queue.push(input_key);
  }
  else
  {
    std::cout << "Unmapped CEC code received: "
              << getCECControlStr(msg->keycode) << std::endl;
  }
}


bool execCECCommand(std::string cmd, std::string args, std::string* response)
{
  if (cmd.compare("transmit") == 0)
  {
    CEC::cec_command bytes = cec_adapter->CommandFromString(args.c_str());
    bytes.transmit_timeout = 0;
    if (cec_adapter->Transmit(bytes))
    {
      *response = "Bytes sent (Warning: This function has not been tested)";
    }
    else
    {
      *response = "Byte transmission failed";
    }
  }
  else if (cmd.compare("on") == 0)
  {
    int addr = -1;
    if (sscanf(args.c_str(), "%x", &addr) == 1)
    {
      if ((addr >= 0) && (addr < 256))
      {
        if(cec_adapter->PowerOnDevices((CEC::cec_logical_address) addr))
        {
          *response = "Device powered on";
          return true;
        }
      }
    }

    *response = "Failed to power device";
    return false;
  }
  else if (cmd.compare("standby") == 0)
  {
    int addr = -1;
    if (sscanf(args.c_str(), "%x", &addr) == 1)
    {
      if ((addr >= 0) && (addr < 256))
      {
        if(cec_adapter->StandbyDevices((CEC::cec_logical_address) addr))
        {
          *response = "Device set to standby";
          return true;
        }
      }
    }

    *response = "Failed to put device in standby";
    return false;
  }
  else if (cmd.compare("set_addr_active") == 0)
  {
    int addr = -1;
    if (sscanf(args.c_str(), "%x", &addr) == 1)
    {
      std::cout << "addr: " << addr << std::endl;
      if ((addr >= 0) && (addr < CEC_INVALID_PHYSICAL_ADDRESS))
      {
        cec_adapter->SetStreamPath((uint16_t) addr);
        *response = "Active path set";
        return true;
      }
    }

    *response = "Failed to set active path";
    return false;
  }
  else if (cmd.compare("activate") == 0)
  {
    if (cec_adapter->SetActiveSource())
    {
      *response = "Device set as active source";
    }
    else
    {
      *response = "Failed to set device as active source";
    }
  }
  else if (cmd.compare("deactivate") == 0)
  {
    if (cec_adapter->SetInactiveView())
    {
      *response = "Device set as inactive";
    }
    else
    {
      *response = "Failed to set device inactive view";
    }
  }
  else if (cmd.compare("volup") == 0)
  {
    if (cec_adapter->VolumeUp())
    {
      *response = "Volume increased";
    }
    else
    {
      *response = "Failed change volume";
    }
  }
  else if (cmd.compare("voldown") == 0)
  {
    if (cec_adapter->VolumeDown())
    {
      *response = "Volume decreased";
    }
    else
    {
      *response = "Failed change volume";
    }
  }
  else if (cmd.compare("mute") == 0)
  {
    if (cec_adapter->AudioToggleMute())
    {
      *response = "Mute toggled";
    }
    else
    {
      *response = "Failed to toggle mute";
    }
  }
  else
  {
    *response = "The CEC command given was invalid";
    return false;
  }

  return true;
}


void wsMessageCB(websocketpp::server<websocketpp::config::asio>* serv,
                 websocketpp::connection_hdl hdl,
                 websocketpp::server<websocketpp::config::asio>::message_ptr msg)
{
  hdl.lock().get();
  std::string response;
  Json::Value recievedJson;
  Json::Reader reader;
  Json::Value responseJson;

  if (reader.parse(msg->get_payload().c_str(), recievedJson))
  {
    std::string target = recievedJson.get("target", "").asString();
    std::string command = recievedJson.get("command", "").asString();
    std::string arguments = recievedJson.get("args", "").asString();
    if (!(target.empty() || command.empty()))
    {
      if (target.compare("cec") == 0)
      {
        std::string exec_response;
        responseJson["success"] = execCECCommand(command, arguments,
                                                 &exec_response);
        responseJson["message"] = exec_response;

      }
      else if (target.compare("key") == 0)
      {
        int kCode;
        if (getInputKeyCode(command, &kCode))
        {
          responseJson["success"] = true;
          responseJson["message"] = "key code received";
          std::lock_guard<std::mutex> lock(key_mutex);
          key_queue.push(kCode);
        }
        else
        {
          responseJson["success"] = false;
          responseJson["message"] = "Unrecognised key command";
        }
      }
      else
      {
        responseJson["success"] = false;
        responseJson["message"] = "Unrecognised command type";
      }
    }
    else
    {
      responseJson["success"] = false;
      responseJson["message"] = "target and command are both required parameters";
    }
  }
  else
  {
    responseJson["success"] = false;
    responseJson["message"] = reader.getFormattedErrorMessages();
  }

  Json::FastWriter fastWriter;
  response = fastWriter.write(responseJson);

  try
  {
      serv->send(hdl, response, msg->get_opcode());
  }
  catch (websocketpp::exception const & e)
  {
    std::cerr << "Failed to respond to websocket client." << std::endl
              << e.what() << std::endl;
  }

  return;
}


void print_usage(std::string prog_name)
{
    std::cout << std::endl << "usage: " << prog_name << " [options]"
      << std::endl << std::endl << "options:"
      << std::endl << "\t-c {file}   - configuration yaml location"
      << std::endl << "\t-d {device} - cec device port (default: autodetect)"
      << std::endl << "\t-u {device} - uinput device port (default: /dev/uinput)"
      << std::endl << "\t-p {port}   - websocket server port (default: websocket disabled)"
      << std::endl << "\t-m          - dump config yaml and exit"
      << std::endl << "\t-n {name}   - CEC device name, max length=13 {default: cec_keyboard}"
      << std::endl << std::endl;
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
