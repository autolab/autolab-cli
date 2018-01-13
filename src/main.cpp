#include <iostream>
#include <vector>
#include <algorithm>
#include <utility>

#include "autolab_client.h"
#include "client_helpers.h"
#include "config_manager.h"
#include "file_utils.h"
#include "logger.h"

const std::string client_id = "c021c9b12dc597b5b42d783ee285a2bc9a8afcce4a60db5b4b97a1cda551f48d";
const std::string client_secret = "5cce1a3f5968308defe8a15d4cb1e47250fc88976ebf9c084155fc86da8050f1";
const std::string redirect_uri = "http://localhost:3000/device_flow_auth_cb";

AutolabClient ac = AutolabClient(client_id, client_secret, redirect_uri, store_tokens);

void init_autolab_client(AutolabClient &ac) {
  std::string at, rt;
  load_tokens(at, rt);
  ac.set_tokens(at, rt);
}

void print_download_help() {
  Logger::info << "Usage: autolab download [course_name]:[assessment_name]" << Logger::endl;
}

void print_assessments_help() {
  Logger::info << "Usage: autolab assessments [course_name]" << Logger::endl;
}

int perform_device_flow(AutolabClient &ac) {
  Logger::info << "Initiating authorization..." << Logger::endl << Logger::endl;
  std::string user_code, verification_uri;
  ac.device_flow_init(user_code, verification_uri);
  Logger::info << "Please visit " << verification_uri << " and enter the code: " << user_code << Logger::endl;
  Logger::info << "Waiting for user authorization ..." << Logger::endl;

  int res = ac.device_flow_authorize(300); // wait for 5 minutes max
  if (res == 1) {
    Logger::info << "User denied authorization." << Logger::endl;
    return 1;
  } else if (res < 0) {
    Logger::info << "Error occurred during authorization." << Logger::endl;
    return -1;
  }

  // res == 0
  Logger::info << "Received authorization!" << Logger::endl;

  return 0;
}

/* return false if failed to parse */
bool parse_course_and_asmt(std::string raw_input, std::string &course, std::string &asmt) {
  std::string::size_type split_pos = raw_input.find(":");
  if (split_pos == std::string::npos) {
    // the entire string is the assessment name.
    // attempt to load course name from config if exists,
    // otherwise report error.
    return false;
  }
  course = raw_input.substr(0, split_pos);
  asmt = raw_input.substr(split_pos + 1, std::string::npos);
  return true;
}

struct namepair {
  std::string name;
  std::string display_name;
  namepair(std::string n, std::string dn) :
    name(n), display_name(dn) {}
};
bool namepair_comparator(const namepair &a, const namepair &b) {
  if (a.name == b.name) return a.display_name < b.display_name;
  return a.name < b.name;
}

/* download assessment into a new directory
 */
int download_asmt(int argc, char *argv[]) {
  if (argc < 3) {
    print_download_help();
    return 0;
  }

  // set up logger
  Logger::fatal.set_prefix("Cannot download assessment");

  // parse course and assessment name
  std::string course_name, asmt_name;
  if (!parse_course_and_asmt(argv[2], course_name, asmt_name)) {
    Logger::fatal << "Please specify both course and assessment name." << Logger::endl;
    return 0;
  }

  Logger::info << "Querying assessment '" << asmt_name << "' of course '" << 
    course_name << "' ..." << Logger::endl;

  // make sure assessment exists
  rapidjson::Document asmt;
  ac.get_assessment_details(asmt, course_name, asmt_name);
  check_error_and_exit(asmt);

  // setup directory
  bool dir_exists = dir_find(get_curr_dir(), asmt_name.c_str(), true);
  if (dir_exists) {
    Logger::fatal << "Directory named '" << asmt_name << "' already exists. Please delete or rename before proceeding." << Logger::endl;
    return 0;
  }

  std::string new_dir(get_curr_dir());
  new_dir.append("/" + asmt_name);
  Logger::info << "Creating directory " << new_dir << Logger::endl;

  bool created = create_dir(new_dir.c_str());
  err_assert(created);

  // download files into directory
  rapidjson::Document response;
  ac.download_handout(response, new_dir, course_name, asmt_name);
  check_error_and_exit(response);
  if (response.IsObject()) {
    if (response.HasMember("url"))
      Logger::info << "Handout URL: " << response["url"].GetString() << Logger::endl;
    else
      Logger::info << "Assessment has no handout" << Logger::endl;
  } else {
    Logger::info << "Handout downloaded into assessment directory" << Logger::endl;
  }

  ac.download_writeup(response, new_dir, course_name, asmt_name);
  check_error_and_exit(response);
  if (response.IsObject()) {
    if (response.HasMember("url"))
      Logger::info << "Writeup URL: " << response["url"].GetString() << Logger::endl;
    else
      Logger::info << "Assessment has no writeup" << Logger::endl;
  } else {
    Logger::info << "Writeup downloaded into assessment directory" << Logger::endl;
  }

  // write assessment file
  write_asmt_file(new_dir, course_name, asmt_name);

  return 0;
}

int submit_asmt(int argc, char *argv[]) {
  std::string course_name, asmt_name, course_name_config, asmt_name_config;
  bool user_specified_names = false;

  // set up logger
  Logger::fatal.set_prefix("Cannot submit assessment");

  if (argc == 3) {
    // user specified course and assessment name
    user_specified_names = true;
    if (!parse_course_and_asmt(argv[2], course_name, asmt_name)) {
      Logger::fatal << "Please specify both course and assessment name." << Logger::endl;
      return 0;
    }
  }

  // attempt to load names from asmt-file
  bool found_asmt_file = read_asmt_file(course_name_config, asmt_name_config);
  if (!found_asmt_file && !user_specified_names) {
    Logger::fatal << "Not inside an autolab assessment directory: .autolab-asmt not found" << Logger::endl << Logger::endl
      << "Please change directory or specify the course and assessment names" << Logger::endl;
    return 0;
  }

  if (found_asmt_file && user_specified_names) {
    if ((course_name != course_name_config) || (asmt_name != asmt_name_config)) {
      Logger::fatal << "The provided names and the configured names for this autolab assessment directory do not match:" << Logger::endl
        << "Provided names:   " << course_name  << ":" << asmt_name << Logger::endl
        << "Configured names: " << course_name_config << ":" << asmt_name_config << Logger::endl << Logger::endl
        << "Please resolve this conflict, or use the '-f' option to force the use of the provided names." << Logger::endl;
      return 0;
    }
  }

  // conflicts resolved, use course_name and asmt_name from now on
  rapidjson::Document response;
  ac.submit_assessment(response, course_name, asmt_name);

  check_error_and_exit(response);
  return 0;
}

int show_courses(int argc, char *argv[]) {
  // set up logger
  Logger::fatal.set_prefix("Cannot get courses");

  rapidjson::Document courses;
  ac.get_courses(courses);
  check_error_and_exit(courses);

  Logger::debug << "Found " << courses.Size() << " current courses." << Logger::endl;

  std::vector<namepair> course_list;
  for (auto &c : courses.GetArray()) {
    std::string course_name = c.GetObject()["name"].GetString();
    std::string course_display_name = c.GetObject()["display_name"].GetString();
    course_list.emplace_back(course_name, course_display_name);
  }
  std::sort(course_list.begin(), course_list.end(), namepair_comparator);

  for (auto &c : course_list) {
    Logger::info << c.name << " (" << c.display_name << ")" << Logger::endl;
  }

  return 0;
}

int show_assessments(int argc, char *argv[]) {
  if (argc < 3) {
    print_assessments_help();
    return 0;
  }

  // set up logger
  Logger::fatal.set_prefix("Cannot get assessments");

  std::string course_name(argv[2]);

  rapidjson::Document asmts;
  ac.get_assessments(asmts, course_name);
  check_error_and_exit(asmts);

  Logger::debug << "Found " << asmts.Size() << " assessments." << Logger::endl;

  std::vector<namepair> asmt_list;
  for (auto &a : asmts.GetArray()) {
    std::string asmt_name = a.GetObject()["name"].GetString();
    std::string asmt_display_name = a.GetObject()["display_name"].GetString();
    asmt_list.emplace_back(asmt_name, asmt_display_name);
  }
  std::sort(asmt_list.begin(), asmt_list.end(), namepair_comparator);

  for (auto &c : asmt_list) {
    Logger::info << c.name << " (" << c.display_name << ")" << Logger::endl;
  }

  return 0;
}

int user_setup(int argc, char *argv[]) {
  bool force_setup = false;

  std::string at, rt;
  bool user_exists = load_tokens(at, rt);
  ac.set_tokens(at, rt);

  if (!force_setup) {
    // perform a check if not a forced setup
    if (user_exists) {
      bool token_valid = true;
      rapidjson::Document user_info;
      try {
        ac.get_user_info(user_info);
      } catch (AutolabClient::InvalidTokenException &e) {
        token_valid = false;
      }
      if (token_valid && user_info.HasMember("first_name")) {
        Logger::info << "User '" << user_info["first_name"].GetString() << "' is currently set up on this client." << Logger::endl <<
          "To force reset of user info, use the '-f' option." << Logger::endl;
        return 0;
      }
    }
  }

  // user non-existant, or existing user's credentials no longer work
  int result = perform_device_flow(ac);
  if (result == 0) {
    Logger::info << Logger::endl << "User setup complete." << Logger::endl;
    return 0;
  }
  Logger::info << Logger::endl << "User setup failed." << Logger::endl;
  return -1;
}

void print_help() {
  Logger::info << "HELP TEXT" << Logger::endl;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_help();
    return 0;
  }

  init_autolab_client(ac);

  // determine what command it is
  std::string command(argv[1]);
  if ("setup" == command) {
    return user_setup(argc, argv);
  } else if ("download" == command) {
    return download_asmt(argc, argv);
  } else if ("submit" == command) {
    return submit_asmt(argc, argv);
  } else if ("courses" == command) {
    return show_courses(argc, argv);
  } else if ("assessments" == command ||
             "asmts" == command) {
    return show_assessments(argc, argv);
  } else {
    Logger::fatal << "Unrecognized command: " << command << Logger::endl;
  }

  return 0;
}