// A file order people expect: "img2" before "img10", letters without caring about the case. Included once by a program, inside its anonymous namespace.
#pragma once

// "img2" before "img10".
bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && std::isdigit(static_cast<unsigned char>(a[i2]))) i2++;
            while (j2 < b.size() && std::isdigit(static_cast<unsigned char>(b[j2]))) j2++;
            std::string x = a.substr(i, i2 - i), y = b.substr(j, j2 - j);
            x.erase(0, std::min(x.find_first_not_of('0'), x.size()));
            y.erase(0, std::min(y.find_first_not_of('0'), y.size()));
            if (x.size() != y.size()) return x.size() < y.size();
            if (x != y) return x < y;
            i = i2;
            j = j2;
        } else {
            const int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[j]));
            if (ca != cb) return ca < cb;
            i++;
            j++;
        }
    }
    return a.size() - i < b.size() - j;
}
