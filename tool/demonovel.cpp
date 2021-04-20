#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "epub.h"
#include "trans.h"

int main(int argc, char *argv[]) {
  auto [input_file, xhtml]{processing_cmd(argc, argv)};

  for (const auto &item : input_file) {
    auto [book_name, texts]{read_file(item)};

    if (xhtml) {
      generate_xhtml(book_name, texts);
    } else {
      create_epub_directory(book_name);

      std::vector<std::string> titles;
      auto size{std::size(texts)};
      std::int32_t count{1};
      for (std::size_t index{}; index < size; ++index) {
        if (texts[index].starts_with("－－－－－－－－－－－－－－－BEGIN")) {
          index += 2;
          auto title{texts[index]};
          titles.push_back(title);
          index += 2;

          auto filename{get_chapter_filename(book_name, count)};
          ++count;

          std::ofstream ofs{filename};
          check_file_is_open(ofs, filename);

          ofs << chapter_file_begin(title);

          for (; index < size &&
                 !texts[index].starts_with("－－－－－－－－－－－－－－－END");
               ++index) {
            ofs << chapter_file_text(texts[index]);
          }

          ofs << chapter_file_end() << std::flush;
        }
      }

      generate_content_opf(book_name, texts[1], count);
      generate_toc_ncx(book_name, titles);
    }
  }

  clean_up();
}
