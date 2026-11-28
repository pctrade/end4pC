// Vid3os: the small pure pieces (no window, no mpv), so they can be tested alone.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vid {

// 65 -> "1:05", 3723 -> "1:02:03"; a negative or unknown time is "0:00".
inline std::string formatTime(double seconds) {
    if (!(seconds > 0) || std::isinf(seconds)) seconds = 0;
    const long total = static_cast<long>(seconds);
    const long h = total / 3600, m = total / 60 % 60, s = total % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", h, m, s);
    else std::snprintf(buf, sizeof buf, "%ld:%02ld", m, s);
    return buf;
}

// The speeds the button goes through.
inline const std::vector<double>& speeds() {
    static const std::vector<double> v = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    return v;
}
// The next speed after `now` (wraps to the first); a speed that is not in the list goes to 1.0.
inline double nextSpeed(double now) {
    const auto& v = speeds();
    for (size_t i = 0; i < v.size(); i++)
        if (std::fabs(v[i] - now) < 1e-6) return v[(i + 1) % v.size()];
    return 1.0;
}
inline std::string speedLabel(double s) {
    char buf[16];
    if (std::fabs(s - std::round(s)) < 1e-6) std::snprintf(buf, sizeof buf, "%d×", static_cast<int>(std::lround(s)));
    else if (std::fabs(s * 10 - std::round(s * 10)) < 1e-6) std::snprintf(buf, sizeof buf, "%.1f×", s);
    else std::snprintf(buf, sizeof buf, "%.2f×", s);
    return buf;
}

// Where a click at `x` on a bar from `left` to `left + width` lands, as 0..1.
inline double barFraction(double x, double left, double width) {
    if (width <= 0) return 0;
    return std::clamp((x - left) / width, 0.0, 1.0);
}

}  // namespace vid
