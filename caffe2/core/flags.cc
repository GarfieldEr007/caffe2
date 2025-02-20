#include <cstdlib>

#include "caffe2/core/flags.h"
#include "caffe2/core/logging.h"

namespace caffe2 {
DEFINE_REGISTRY(Caffe2FlagsRegistry, Caffe2FlagParser, const string&);

static bool gCommandLineFlagsParsed = false;

// The function is defined in init.cc.
extern std::stringstream& GlobalInitStream();

bool ParseCaffeCommandLineFlags(int* pargc, char** argv) {
  bool success = true;
  GlobalInitStream() << "Parsing commandline arguments for caffe2."
                     << std::endl;
  // write_head is the location we write the unused arguments to.
  int write_head = 1;
  for (int i = 1; i < *pargc; ++i) {
    string arg(argv[i]);
    // If the arg does not start with "--", and we will ignore it.
    if (arg[0] != '-' || arg[1] != '-') {
      GlobalInitStream()
          << "Caffe2 flag: commandline argument does not match --name=var "
             "or --name format: "
          << arg << ". Ignoring this argument." << std::endl;
      argv[write_head++] = argv[i];
      continue;
    }

    string key;
    string value;
    int prefix_idx = arg.find('=');
    if (prefix_idx == string::npos) {
      // If there is no equality char in the arg, it means that the
      // arg is specified in the next argument.
      key = arg.substr(2, arg.size() - 2);
      ++i;
      if (i == *pargc) {
        GlobalInitStream()
            << "Caffe2 flag: reached the last commandline argument, but "
               "I am expecting a value for it.";
        success = false;
        break;
      }
      value = string(argv[i]);
    } else {
      // If there is an equality character, we will basically use the value
      // after the "=".
      key = arg.substr(2, prefix_idx - 2);
      value = arg.substr(prefix_idx + 1, string::npos);
    }
    // If the flag is not registered, we will ignore it.
    if (!Caffe2FlagsRegistry()->Has(key)) {
      GlobalInitStream() << "Caffe2 flag: unrecognized commandline argument: "
                         << arg << std::endl;
      success = false;
      break;
    }
    std::unique_ptr<Caffe2FlagParser> parser(
        Caffe2FlagsRegistry()->Create(key, value));
    if (!parser->success()) {
      GlobalInitStream() << "Caffe2 flag: illegal argument: "
                         << arg << std::endl;
      success = false;
      break;
    }
  }
  *pargc = write_head;
  gCommandLineFlagsParsed = true;
  // TODO: when we fail commandline flag parsing, shall we continue, or
  // shall we just quit loudly? Right now we carry on the computation, but
  // since there are failures in parsing, it is very likely that some
  // downstream things will break, in which case it makes sense to quit loud
  // and early.
  return success;
}

bool CommandLineFlagsHasBeenParsed() {
  return gCommandLineFlagsParsed;
}

template <>
bool Caffe2FlagParser::Parse<string>(const string& content, string* value) {
  *value = content;
  return true;
}

template <>
bool Caffe2FlagParser::Parse<int>(const string& content, int* value) {
  try {
    *value = std::atoi(content.c_str());
    return true;
  } catch(...) {
    GlobalInitStream() << "Caffe2 flag error: Cannot convert argument to int: "
                       << content << std::endl;
    return false;
  }
}

template <>
bool Caffe2FlagParser::Parse<double>(const string& content, double* value) {
  try {
    *value = std::atof(content.c_str());
    return true;
  } catch(...) {
    GlobalInitStream()
        << "Caffe2 flag error: Cannot convert argument to double: "
        << content << std::endl;
    return false;
  }
}

template <>
bool Caffe2FlagParser::Parse<bool>(const string& content, bool* value) {
  if (content == "false" || content == "False" || content == "FALSE" ||
      content == "0") {
    *value = false;
    return true;
  } else if (content == "true" || content == "True" || content == "TRUE" ||
      content == "1") {
    *value = true;
    return true;
  } else {
    GlobalInitStream()
        << "Caffe2 flag error: Cannot convert argument to bool: "
        << content << std::endl
        << "Note that if you are passing in a bool flag, you need to "
           "explicitly specify it, like --arg=True or --arg True. Otherwise, "
           "the next argument may be inadvertently used as the argument, "
           "causing the above error."
        << std::endl;
    return false;
  }
}


}  // namespace caffe2
