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
    auto linesCache = this->linesCache.lock();

    /* Try the origin's line cache */
    const auto * linesForInput = linesCache->getOrNullptr(origin->offset);

    auto fillCacheForOrigin = [](std::string_view content) {
        auto contentLines = Lines();

        const char * begin = content.data();
        for (Pos::LinesIterator it(content), end; it != end; it++)
            contentLines.push_back(it->data() - begin);
        if (contentLines.empty())
            contentLines.push_back(0);

        return contentLines;
    };

    /* Calculate line offsets and fill the cache */
    if (!linesForInput) {
        auto originContent = result.getSource().value_or("");
        linesCache->upsert(origin->offset, fillCacheForOrigin(originContent));
        linesForInput = linesCache->getOrNullptr(origin->offset);
    }

    assert(linesForInput);

    // as above: the first line starts at byte 0 and is always present
    auto lineStartOffset = std::prev(std::upper_bound(linesForInput->begin(), linesForInput->end(), offset));
    result.line = 1 + (lineStartOffset - linesForInput->begin());
    result.column = 1 + (offset - *lineStartOffset);
    return result;
}

} // namespace nix
