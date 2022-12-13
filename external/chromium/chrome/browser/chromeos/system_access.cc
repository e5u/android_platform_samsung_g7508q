// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/system_access.h"

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/observer_list.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/name_value_pairs_parser.h"

namespace chromeos {  // NOLINT

namespace { // NOLINT

// The filepath to the timezone file that symlinks to the actual timezone file.
const char kTimezoneSymlink[] = "/var/lib/timezone/localtime";
const char kTimezoneSymlink2[] = "/var/lib/timezone/localtime2";

// The directory that contains all the timezone files. So for timezone
// "US/Pacific", the actual timezone file is: "/usr/share/zoneinfo/US/Pacific"
const char kTimezoneFilesDir[] = "/usr/share/zoneinfo/";

// The system command that returns the hardware class.
const char kHardwareClassKey[] = "hardware_class";
const char* kHardwareClassTool[] = { "/usr/bin/hardware_class" };
const char kUnknownHardwareClass[] = "unknown";

// Command to get machine hardware info and key/value delimiters.
// /tmp/machine-info is generated by platform/init/chromeos_startup.
const char* kMachineHardwareInfoTool[] = { "cat", "/tmp/machine-info" };
const char kMachineHardwareInfoEq[] = "=";
const char kMachineHardwareInfoDelim[] = " \n";

// Command to get machine OS info and key/value delimiters.
const char* kMachineOSInfoTool[] = { "cat", "/etc/lsb-release" };
const char kMachineOSInfoEq[] = "=";
const char kMachineOSInfoDelim[] = "\n";

// Command to get  HWID and key.
const char kHwidKey[] = "hwid";
const char* kHwidTool[] = { "cat", "/sys/devices/platform/chromeos_acpi/HWID" };

// Command to get VPD info and key/value delimiters.
const char* kVpdTool[] = { "cat", "/var/log/vpd_2.0.txt" };
const char kVpdEq[] = "=";
const char kVpdDelim[] = "\n";

// Fallback time zone ID used in case of an unexpected error.
const char kFallbackTimeZoneId[] = "America/Los_Angeles";

class SystemAccessImpl : public SystemAccess {
 public:
  // SystemAccess.implementation:
  virtual const icu::TimeZone& GetTimezone();
  virtual void SetTimezone(const icu::TimeZone& timezone);
  virtual bool GetMachineStatistic(const std::string& name,
                                   std::string* result);
  virtual void AddObserver(Observer* observer);
  virtual void RemoveObserver(Observer* observer);

  static SystemAccessImpl* GetInstance();

 private:
  friend struct DefaultSingletonTraits<SystemAccessImpl>;

  SystemAccessImpl();

  // Updates the machine statistcs by examining the system.
  void UpdateMachineStatistics();

  scoped_ptr<icu::TimeZone> timezone_;
  ObserverList<Observer> observers_;
  NameValuePairsParser::NameValueMap machine_info_;

  DISALLOW_COPY_AND_ASSIGN(SystemAccessImpl);
};

std::string GetTimezoneIDAsString() {
  // Look at kTimezoneSymlink, see which timezone we are symlinked to.
  char buf[256];
  const ssize_t len = readlink(kTimezoneSymlink, buf,
                               sizeof(buf)-1);
  if (len == -1) {
    LOG(ERROR) << "GetTimezoneID: Cannot read timezone symlink "
               << kTimezoneSymlink;
    return std::string();
  }

  std::string timezone(buf, len);
  // Remove kTimezoneFilesDir from the beginning.
  if (timezone.find(kTimezoneFilesDir) != 0) {
    LOG(ERROR) << "GetTimezoneID: Timezone symlink is wrong "
               << timezone;
    return std::string();
  }

  return timezone.substr(strlen(kTimezoneFilesDir));
}

void SetTimezoneIDFromString(const std::string& id) {
  // Change the kTimezoneSymlink symlink to the path for this timezone.
  // We want to do this in an atomic way. So we are going to create the symlink
  // at kTimezoneSymlink2 and then move it to kTimezoneSymlink

  FilePath timezone_symlink(kTimezoneSymlink);
  FilePath timezone_symlink2(kTimezoneSymlink2);
  FilePath timezone_file(kTimezoneFilesDir + id);

  // Make sure timezone_file exists.
  if (!file_util::PathExists(timezone_file)) {
    LOG(ERROR) << "SetTimezoneID: Cannot find timezone file "
               << timezone_file.value();
    return;
  }

  // Delete old symlink2 if it exists.
  file_util::Delete(timezone_symlink2, false);

  // Create new symlink2.
  if (symlink(timezone_file.value().c_str(),
              timezone_symlink2.value().c_str()) == -1) {
    LOG(ERROR) << "SetTimezoneID: Unable to create symlink "
               << timezone_symlink2.value() << " to " << timezone_file.value();
    return;
  }

  // Move symlink2 to symlink.
  if (!file_util::ReplaceFile(timezone_symlink2, timezone_symlink)) {
    LOG(ERROR) << "SetTimezoneID: Unable to move symlink "
               << timezone_symlink2.value() << " to "
               << timezone_symlink.value();
  }
}

const icu::TimeZone& SystemAccessImpl::GetTimezone() {
  return *timezone_.get();
}

void SystemAccessImpl::SetTimezone(const icu::TimeZone& timezone) {
  timezone_.reset(timezone.clone());
  icu::UnicodeString unicode;
  timezone.getID(unicode);
  std::string id;
  UTF16ToUTF8(unicode.getBuffer(), unicode.length(), &id);
  VLOG(1) << "Setting timezone to " << id;
  chromeos::SetTimezoneIDFromString(id);
  icu::TimeZone::setDefault(timezone);
  FOR_EACH_OBSERVER(Observer, observers_, TimezoneChanged(timezone));
}

bool SystemAccessImpl::GetMachineStatistic(
    const std::string& name, std::string* result) {
  NameValuePairsParser::NameValueMap::iterator iter = machine_info_.find(name);
  if (iter != machine_info_.end()) {
    *result = iter->second;
    return true;
  }
  return false;
}

void SystemAccessImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SystemAccessImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

SystemAccessImpl::SystemAccessImpl() {
  // Get Statistics
  UpdateMachineStatistics();
  // Get Timezone
  std::string id = GetTimezoneIDAsString();
  if (id.empty()) {
    id = kFallbackTimeZoneId;
    LOG(ERROR) << "Got an empty string for timezone, default to " << id;
  }
  icu::TimeZone* timezone =
      icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8(id));
  timezone_.reset(timezone);
  icu::TimeZone::setDefault(*timezone);
  VLOG(1) << "Timezone is " << id;
}

void SystemAccessImpl::UpdateMachineStatistics() {
  NameValuePairsParser parser(&machine_info_);
  if (!parser.GetSingleValueFromTool(arraysize(kHardwareClassTool),
                                     kHardwareClassTool,
                                     kHardwareClassKey)) {
    // Use kUnknownHardwareClass if the hardware class command fails.
    parser.AddNameValuePair(kHardwareClassKey, kUnknownHardwareClass);
  }
  parser.ParseNameValuePairsFromTool(arraysize(kMachineHardwareInfoTool),
                                     kMachineHardwareInfoTool,
                                     kMachineHardwareInfoEq,
                                     kMachineHardwareInfoDelim);
  parser.ParseNameValuePairsFromTool(arraysize(kMachineOSInfoTool),
                                     kMachineOSInfoTool,
                                     kMachineOSInfoEq,
                                     kMachineOSInfoDelim);
  parser.GetSingleValueFromTool(arraysize(kHwidTool), kHwidTool, kHwidKey);
  parser.ParseNameValuePairsFromTool(
      arraysize(kVpdTool), kVpdTool, kVpdEq, kVpdDelim);
}

SystemAccessImpl* SystemAccessImpl::GetInstance() {
  return Singleton<SystemAccessImpl,
                   DefaultSingletonTraits<SystemAccessImpl> >::get();
}

}  // namespace

SystemAccess* SystemAccess::GetInstance() {
  return SystemAccessImpl::GetInstance();
}

}  // namespace chromeos
