#include "ESPEasy_checks.h"

#include "../../ESPEasy_common.h"
#include "../../ESPEasy-Globals.h"

#include "../DataStructs/NodeStruct.h"
#include "../DataStructs/CRCStruct.h"
#include "../DataStructs/SettingsStruct.h"

#include "../DataStructs/SecurityStruct.h"
#include "../DataStructs/ExtraTaskSettingsStruct.h"
#include "../DataStructs/ControllerSettingsStruct.h"
#include "../DataStructs/ESPEasy_EventStruct.h"
#include "../DataStructs/LogStruct.h"
#include "../DataStructs/DeviceStruct.h"
#include "../DataStructs/ProtocolStruct.h"
#include "../DataStructs/ESPEasy_EventStruct.h"
#include "../DataStructs/NodeStruct.h"
#include "../DataStructs/RTCStruct.h"
#include "../DataStructs/SystemTimerStruct.h"
#include "../DataStructs/PortStatusStruct.h"
#include "../DataStructs/FactoryDefaultPref.h"

#include "../Globals/ExtraTaskSettings.h"

#include "../Helpers/ESPEasy_Storage.h"



#ifdef USES_NOTIFIER
#include "../DataStructs/NotificationStruct.h"
#endif


// ********************************************************************************
// Check struct sizes at compile time
// Usage:
//   struct foo
//   {
//     char bla[16];
//   };
//
//   check_size<foo, 8>();
// ********************************************************************************
template <typename ToCheck, std::size_t ExpectedSize, std::size_t RealSize = sizeof(ToCheck)>
void check_size() {
  static_assert(ExpectedSize == RealSize, "");
}



// ********************************************************************************
// Check struct sizes at compile time
// Usage:
//   struct X { int a, b, c, d; }
//   static_assert(ExpectedSize == offsetOf(&X::c), "");
// ********************************************************************************
template<typename T, typename U> constexpr size_t offsetOf(U T::*member)
{
    return (char*)&((T*)nullptr->*member) - (char*)nullptr;
}

void run_compiletime_checks() {
  check_size<CRCStruct,                             204u>();
  check_size<SecurityStruct,                        593u>();
  const unsigned int SettingsStructSize = (276 + 82 * TASKS_MAX);
  check_size<SettingsStruct,                        SettingsStructSize>();
  check_size<ControllerSettingsStruct,              820u>();
  #ifdef USES_NOTIFIER
  check_size<NotificationSettingsStruct,            996u>();
  #endif
  check_size<ExtraTaskSettingsStruct,               472u>();
  check_size<EventStruct,                           96u>(); // Is not stored

  // LogStruct is mainly dependent on the number of lines.
  // Has to be round up to multiple of 4.
  const unsigned int LogStructSize = ((12u + 17 * LOG_STRUCT_MESSAGE_LINES) + 3) & ~3;
  check_size<LogStruct,                             LogStructSize>(); // Is not stored
  check_size<DeviceStruct,                          7u>();
  check_size<ProtocolStruct,                        6u>();
  #ifdef USES_NOTIFIER
  check_size<NotificationStruct,                    3u>();
  #endif
  check_size<NodeStruct,                            28u>();
  check_size<systemTimerStruct,                     28u>();
  check_size<RTCStruct,                             32u>();
  check_size<rulesTimerStatus,                      12u>();
  check_size<portStatusStruct,                      4u>();
  check_size<ResetFactoryDefaultPreference_struct,  4u>();
  check_size<GpioFactorySettingsStruct,             18u>();
  #if defined(USE_NON_STANDARD_24_TASKS) && defined(ESP8266)
    static_assert(TASKS_MAX == 24, "TASKS_MAX invalid size");
  #endif

  // Check for alignment issues at compile time
  static_assert(256u == offsetOf(&SecurityStruct::ControllerUser), "");
  static_assert((256 + (CONTROLLER_MAX * 26))  == offsetOf(&SecurityStruct::ControllerPassword), "");
  static_assert(192u == offsetOf(&SettingsStruct::Protocol), "");
  static_assert(195u == offsetOf(&SettingsStruct::Notification), "CONTROLLER_MAX has changed?");
  static_assert(198u == offsetOf(&SettingsStruct::TaskDeviceNumber), "NOTIFICATION_MAX has changed?");

  // All settings related to N_TASKS
  static_assert((200 + TASKS_MAX) == offsetOf(&SettingsStruct::OLD_TaskDeviceID), ""); // 32-bit alignment, so offset of 2 bytes.
  static_assert((200 + (67 * TASKS_MAX)) == offsetOf(&SettingsStruct::ControllerEnabled), ""); 

  // Used to compute true offset.
  //const size_t offset = offsetOf(&SettingsStruct::ControllerEnabled);
  //check_size<SettingsStruct, offset>();

}

String ReportOffsetErrorInStruct(const String& structname, size_t offset) {
  String error;

  error.reserve(48 + structname.length());
  error  = F("Error: Incorrect offset in struct: ");
  error += structname;
  error += '(';
  error += String(offset);
  error += ')';
  return error;
}

/*********************************************************************************************\
*  Analyze SettingsStruct and report inconsistencies
*  Not a member function to be able to use the F-macro
\*********************************************************************************************/
bool SettingsCheck(String& error) {
  error = "";
#ifdef esp8266
  size_t offset = offsetof(SettingsStruct, ResetFactoryDefaultPreference);

  if (offset != 1224) {
    error = ReportOffsetErrorInStruct(F("SettingsStruct"), offset);
  }
#endif // ifdef esp8266

  if (!Settings.networkSettingsEmpty()) {
    if ((Settings.IP[0] == 0) || (Settings.Gateway[0] == 0) || (Settings.Subnet[0] == 0) || (Settings.DNS[0] == 0)) {
      error += F("Error: Either fill all IP settings fields or leave all empty");
    }
  }

  return error.length() == 0;
}


String checkTaskSettings(taskIndex_t taskIndex) {
  String err = LoadTaskSettings(taskIndex);
  if (err.length() > 0) return err;
  if (!ExtraTaskSettings.checkUniqueValueNames()) {
    return F("Use unique value names");
  }
  if (!ExtraTaskSettings.checkInvalidCharInNames()) {
    return F("Invalid character in names. Do not use ',#[]' or space.");
  }
  String deviceName = ExtraTaskSettings.TaskDeviceName;
  if (deviceName.length() == 0) {
    if (Settings.TaskDeviceEnabled[taskIndex]) {
      // Decide what to do here, for now give a warning when task is enabled.
      return F("Warning: Task Device Name is empty. It is adviced to give tasks an unique name");
    }
  }
  // Do not use the cached function findTaskIndexByName since that one does rely on the fact names should be unique.
  for (taskIndex_t i = 0; i < TASKS_MAX; ++i) {
    if (i != taskIndex && Settings.TaskDeviceEnabled[i]) {
      LoadTaskSettings(i);
      if (ExtraTaskSettings.TaskDeviceName[0] != 0) {
        if (strcasecmp(ExtraTaskSettings.TaskDeviceName, deviceName.c_str()) == 0) {
          err = F("Task Device Name is not unique, conflicts with task ID #");
          err += (i+1);
//          return err;
        }
      }
    }
  }

  err += LoadTaskSettings(taskIndex);
  return err;
}