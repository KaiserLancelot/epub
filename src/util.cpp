#include "util.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <clocale>
#include <cstddef>
#include <cstdlib>
#include <cuchar>
#include <regex>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <unicode/calendar.h>
#include <unicode/timezone.h>
#include <unicode/uchar.h>
#include <unicode/umachine.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>
#include <boost/algorithm/string.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/variables_map.hpp>

#include "encoding.h"
#include "error.h"
#include "trans.h"
#include "version.h"

namespace {

std::u32string utf8_to_utf32(const std::string &str) {
  std::setlocale(LC_ALL, "en_US.utf8");

  std::u32string result;

  std::size_t rc;
  char32_t out;
  auto begin = str.c_str();
  mbstate_t state = {};

  while ((rc = mbrtoc32(&out, begin, std::size(str), &state))) {
    assert(rc != static_cast<std::size_t>(-3));

    if (rc > static_cast<std::size_t>(-1) / 2) {
      break;
    } else {
      begin += rc;
      result.push_back(out);
    }
  }

  return result;
}

// https://stackoverflow.com/questions/62531882/is-there-a-way-to-detect-chinese-characters-in-c-using-boost
template <char32_t a, char32_t b>
class UnicodeRange {
  static_assert(a <= b, "proper range");

 public:
  constexpr bool operator()(char32_t x) const noexcept {
    return x >= a && x <= b;
  }
};

using UnifiedIdeographs = UnicodeRange<0x4E00, 0x9FFF>;
using UnifiedIdeographsA = UnicodeRange<0x3400, 0x4DBF>;
using UnifiedIdeographsB = UnicodeRange<0x20000, 0x2A6DF>;
using UnifiedIdeographsC = UnicodeRange<0x2A700, 0x2B73F>;
using UnifiedIdeographsD = UnicodeRange<0x2B740, 0x2B81F>;
using UnifiedIdeographsE = UnicodeRange<0x2B820, 0x2CEAF>;
using CompatibilityIdeographs = UnicodeRange<0xF900, 0xFAFF>;
using CompatibilityIdeographsSupplement = UnicodeRange<0x2F800, 0x2FA1F>;

constexpr bool is_chinese(char32_t x) noexcept {
  return UnifiedIdeographs{}(x) || UnifiedIdeographsA{}(x) ||
         UnifiedIdeographsB{}(x) || UnifiedIdeographsC{}(x) ||
         UnifiedIdeographsD{}(x) || UnifiedIdeographsE{}(x) ||
         CompatibilityIdeographs{}(x) || CompatibilityIdeographsSupplement{}(x);
}

bool start_with_chinese(const std::string &str) {
  return is_chinese(utf8_to_utf32(str).front());
}

bool end_with_chinese(const std::string &str) {
  return is_chinese(utf8_to_utf32(str).back());
}

char32_t to_unicode(const std::string &str) {
  auto utf32 = utf8_to_utf32(str);
  assert(std::size(utf32) == 1);

  return utf32.front();
}

bool is_punct(char32_t c) {
  return u_ispunct(c) || c == to_unicode("+") || c == to_unicode("-") ||
         c == to_unicode("*") || c == to_unicode("/") || c == to_unicode("=") ||
         c == to_unicode("～") || c == to_unicode("ー") || c == to_unicode("→");
}

}  // namespace

namespace kepub {

void create_dir(const std::filesystem::path &path) {
  if (std::filesystem::exists(path)) {
    if (std::filesystem::is_directory(path)) {
      if (std::filesystem::remove_all(path) == 0) {
        error("can not remove directory: {}", path);
      }
    } else {
      error("{} already exists", path);
    }
  }

  if (!std::filesystem::create_directory(path)) {
    error("can not create directory: {}", path.string());
  }
}

void check_is_txt_file(const std::string &file_name) {
  check_file_exist(file_name);

  if (std::filesystem::path(file_name).extension() != ".txt") {
    error("need a txt file: {}", file_name);
  }
}

void check_file_exist(const std::string &file_name) {
  if (!(std::filesystem::exists(file_name) &&
        std::filesystem::is_regular_file(file_name))) {
    error("the file not exist: {}", file_name);
  }
}

// https://stackoverflow.com/questions/38608116/how-to-check-a-specified-string-is-a-valid-url-or-not-using-c-code/38608262
void check_is_url(const std::string &url) {
  if (!std::regex_match(
          url,
          std::regex(
              R"(^(https?:\/\/)?([\da-z\.-]+)\.([a-z\.]{2,6})([\/\w \.-]*)*\/?$)"))) {
    error("not a url: {}", url);
  }
}

std::string read_file_to_str(const std::string &file_name) {
  std::ifstream ifs(file_name);
  std::string data;

  data.resize(ifs.seekg(0, std::ifstream::end).tellg());
  ifs.seekg(0, std::ifstream::beg).read(data.data(), std::size(data));

  if (auto encoding = detect_encoding(data); encoding != "UTF-8") {
    error("file {} encoding is not UTF-8 ({})", file_name, encoding);
  }

  return data;
}

std::vector<std::string> read_file_to_vec(const std::string &file_name) {
  auto data = read_file_to_str(file_name);
  std::vector<std::string> result;
  boost::split(result, data, boost::is_any_of("\n"), boost::token_compress_on);

  for (auto &item : result) {
    item = trans_str(item);
  }

  std::erase_if(result,
                [](const std::string &line) { return std::empty(line); });

  return result;
}

void check_and_write_file(std::ofstream &ofs, std::string_view str) {
  if (!ofs) {
    error("file is not open");
  }

  ofs << str << std::flush;
}

std::string num_to_str(std::int32_t i) {
  assert(i > 0);

  if (i < 10) {
    return "00" + std::to_string(i);
  } else if (i < 100) {
    return "0" + std::to_string(i);
  } else {
    return std::to_string(i);
  }
}

std::string num_to_chapter_name(std::int32_t i) {
  return "chapter" + num_to_str(i) + ".xhtml";
}

std::string num_to_illustration_name(std::int32_t i) {
  return "illustration" + num_to_str(i) + ".xhtml";
}

std::string processing_cmd(std::int32_t argc, char *argv[]) {
  std::string input_file;

  boost::program_options::options_description generic("Generic options");
  generic.add_options()("version,v", "print version string");
  generic.add_options()("help,h", "produce help message");

  boost::program_options::options_description config("Configuration");
  config.add_options()("connect,c", "connect chinese");
  config.add_options()("no-cover", "do not generate cover");
  config.add_options()("postscript,p", "generate postscript");
  config.add_options()("download-cover,d", "download cover");
  config.add_options()("old-style", "old style");
  config.add_options()("no-trans-hant", "no trans hant");
  config.add_options()(
      "illustration,i",
      boost::program_options::value<std::int32_t>(&illustration_num)
          ->default_value(0),
      "generate illustration");
  config.add_options()(
      "image",
      boost::program_options::value<std::int32_t>(&image_num)->default_value(0),
      "generate image");
  config.add_options()("max-chapter",
                       boost::program_options::value<std::int32_t>(&max_chapter)
                           ->default_value(0),
                       "maximum number of chapters");
  config.add_options()(
      "date",
      boost::program_options::value<std::string>(&date)->default_value(""),
      "specify the date");

  boost::program_options::options_description hidden("Hidden options");
  hidden.add_options()("input-file",
                       boost::program_options::value<std::string>(&input_file));

  boost::program_options::options_description cmdline_options;
  cmdline_options.add(generic).add(config).add(hidden);

  boost::program_options::options_description visible("Allowed options");
  visible.add(generic).add(config);

  boost::program_options::positional_options_description p;
  p.add("input-file", 1);

  boost::program_options::variables_map vm;
  store(boost::program_options::command_line_parser(argc, argv)
            .options(cmdline_options)
            .positional(p)
            .run(),
        vm);
  notify(vm);

  if (vm.contains("help")) {
    fmt::print("Usage: {} [options] file...\n\n{}\n", argv[0], visible);
    std::exit(EXIT_SUCCESS);
  }

  if (vm.contains("version")) {
    fmt::print("{} version: {}.{}.{}", argv[0], KEPUB_VER_MAJOR,
               KEPUB_VER_MINOR, KEPUB_VER_PATCH);
    std::exit(EXIT_SUCCESS);
  }

  if (!vm.contains("input-file")) {
    error("need a text file name or a url");
  }

  if (vm.contains("connect")) {
    connect_chinese = true;
  }
  if (vm.contains("no-cover")) {
    no_cover = true;
  }
  if (vm.contains("download-cover")) {
    download_cover = true;
  }
  if (vm.contains("postscript")) {
    postscript = true;
  }
  if (vm.contains("old-style")) {
    old_style = true;
  }
  if (vm.contains("no-trans-hant")) {
    no_trans_hant = true;
  }

  return input_file;
}

void push_back(std::vector<std::string> &texts, const std::string &str) {
  if (std::empty(str)) {
    return;
  }

  if (std::empty(texts)) {
    texts.push_back(str);
    return;
  }

  if (texts.back().ends_with("，") || str.starts_with("，") ||
      str.starts_with("。") || str.starts_with("！") || str.starts_with("？") ||
      str.starts_with("”") || str.starts_with("、") || str.starts_with("』") ||
      str.starts_with("》") || str.starts_with("】") || str.starts_with("）")) {
    texts.back().append(str);
  } else if (std::isalpha(texts.back().back()) && std::isalpha(str.front())) {
    texts.back().append(" " + str);
  } else if (end_with_chinese(texts.back()) && std::isalpha(str.front())) {
    texts.back().append(" " + str);
  } else if (std::isalpha(texts.back().back()) && start_with_chinese(str)) {
    texts.back().append(" " + str);
  } else if (connect_chinese && end_with_chinese(texts.back()) &&
             start_with_chinese(str)) {
    texts.back().append(str);
  } else {
    texts.push_back(str);
  }
}

std::int32_t str_size(const std::string &str) {
  auto copy = str;
  boost::replace_all(copy, "&amp;", "&");
  boost::replace_all(copy, "&lt;", "<");
  boost::replace_all(copy, "&gt;", ">");
  boost::replace_all(copy, "&quot;", "\"");
  boost::replace_all(copy, "&apos;", "'");

  auto result = std::erase_if(copy, [](auto c) { return std::isalnum(c); });

  for (auto c : utf8_to_utf32(copy)) {
    if (is_chinese(c)) {
      ++result;
    }
  }

  return result;
}

void str_check(const std::string &str) {
  auto copy = str;
  std::erase_if(copy, [](auto c) { return std::isalnum(c); });

  for (auto c : utf8_to_utf32(copy)) {
    if (!u_isblank(c) && !is_chinese(c) && !is_punct(c)) {
      std::string temp;
      UChar32 ch = c;
      warning("unknown character: {} in {}",
              icu::UnicodeString::fromUTF32(&ch, 1).toUTF8String(temp), str);
    }
  }
}

std::string get_date(std::string_view time_zone) {
  UErrorCode status = U_ZERO_ERROR;

  auto calendar = icu::Calendar::createInstance(
      icu::TimeZone::createTimeZone(time_zone.data()), status);
  if (U_FAILURE(status)) {
    error("error: {}", u_errorName(status));
  }

  auto result = fmt::format("{}-{:02d}-{}", calendar->get(UCAL_YEAR, status),
                            calendar->get(UCAL_MONTH, status) + 1,
                            calendar->get(UCAL_DATE, status));
  if (U_FAILURE(status)) {
    error("error: {}", u_errorName(status));
  }

  delete calendar;
  return result;
}

}  // namespace kepub
