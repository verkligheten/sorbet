#include "Context.h"
#include <regex>
#include <vector>

template class std::vector<std::shared_ptr<ruby_typer::core::File>>;
template class std::shared_ptr<ruby_typer::core::File>;
using namespace std;

namespace ruby_typer {
namespace core {

vector<int> findLineBreaks(const std::string &s) {
    vector<int> res;
    int i = -1;
    res.push_back(-1);
    for (auto c : s) {
        i++;
        if (c == '\n') {
            res.push_back(i);
        }
    }
    res.push_back(i);
    return res;
}

StrictLevel fileSigil(absl::string_view source) {
    /*
     * StrictLevel::Stripe: <none> | # typed: false
     * StrictLevel::Typed: # typed: true
     * StrictLevel::Strict: # typed: strict
     * StrictLevel::String: # typed: strong
     */
    static regex sigil(R"(^\s*#\s*typed:\s*(\w+)?\s*$)");
    size_t off = 0;
    // std::regex is shockingly slow.
    //
    // Help it out by manually scanning for the word `typed:`, and then running
    // the regex only over the single line.
    while (true) {
        off = source.find("typed:", off);
        if (off == absl::string_view::npos) {
            return StrictLevel::Stripe;
        }

        size_t line_start = source.rfind('\n', off);
        if (line_start == absl::string_view::npos) {
            line_start = 0;
        }
        size_t line_end = source.find('\n', off);
        if (line_end == absl::string_view::npos) {
            line_end = source.size();
        }

        match_results<const char *> match;
        if (regex_search(source.data() + line_start, source.data() + line_end, match, sigil)) {
            absl::string_view suffix(match[1].first, match[1].second - match[1].first);
            if (suffix == "true") {
                return StrictLevel::Typed;
            } else if (suffix == "strict") {
                return StrictLevel::Strict;
            } else if (suffix == "strong") {
                return StrictLevel::Strong;
            } else if (suffix == "false") {
                return StrictLevel::Stripe;
            } else {
                // TODO(nelhage): We should report an error here to help catch
                // typos. This would require refactoring so this function has
                // access to GlobalState or can return errors to someone who
                // does.
            }
        }
        off = line_end;
    }
}

File::File(std::string &&path_, std::string &&source_, Type source_type)
    : source_type(source_type), path_(path_), source_(source_),
      hashKey_(this->path_ + "-" + to_string(_hash(this->source_))), sigil(fileSigil(this->source())), strict(sigil) {}

FileRef::FileRef(unsigned int id) : _id(id) {}

const File &FileRef::data(const GlobalState &gs) const {
    ENFORCE(_id < gs.filesUsed());
    ENFORCE(gs.files[_id]->source_type != File::TombStone);
    return *(gs.files[_id]);
}

File &FileRef::data(GlobalState &gs) const {
    ENFORCE(_id < gs.filesUsed());
    ENFORCE(gs.files[_id]->source_type != File::TombStone);
    return *(gs.files[_id]);
}

absl::string_view File::path() const {
    return this->path_;
}

absl::string_view File::source() const {
    return this->source_;
}

absl::string_view File::hashKey() const {
    return this->hashKey_;
}

bool File::hadErrors() const {
    return hadErrors_;
}

bool File::isPayload() const {
    return source_type == Type::PayloadGeneration || source_type == Type::Payload;
}

std::vector<int> &File::line_breaks() const {
    auto ptr = std::atomic_load(&line_breaks_);
    if (ptr) {
        return *ptr;
    } else {
        auto my = make_shared<vector<int>>(findLineBreaks(this->source_));
        atomic_compare_exchange_weak(&line_breaks_, &ptr, my);
        return line_breaks();
    }
}

int File::lineCount() const {
    return line_breaks().size() - 1;
}

} // namespace core
} // namespace ruby_typer
