#include "nix/util/pos-table.hh"

#include <algorithm>

namespace nix {

/* Position table. */

Pos PosTable::operator[](PosIdx p) const
{
    auto origin = resolve(p);
    if (!origin)
        return {};

    const auto offset = origin->offsetOf(p);

    Pos result{0, 0, origin->origin};
    auto lines = this->lines.lock();
    auto linesForInput = (*lines)[origin->offset];

    if (linesForInput.empty()) {
        auto source = result.getSource().value_or("");
        const char * begin = source.data();
        for (Pos::LinesIterator it(source), end; it != end; it++)
            linesForInput.push_back(it->data() - begin);
        if (linesForInput.empty())
            linesForInput.push_back(0);
    }
    // as above: the first line starts at byte 0 and is always present
    auto lineStartOffset = std::prev(std::upper_bound(linesForInput.begin(), linesForInput.end(), offset));

    result.line = 1 + (lineStartOffset - linesForInput.begin());
    result.column = 1 + (offset - *lineStartOffset);
    return result;
}

} // namespace nix
