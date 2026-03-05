#include <iostream>
#include <fstream>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <chrono>
#include <cstring>

static bool is_all_digits(const std::string &s) {
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

static void tokenize_text(const std::string &text, std::ofstream &out,
                           long long &token_count, long long &total_chars) {
    int n = (int)text.size();
    int i = 0;
    while (i < n) {
        while (i < n && !isalnum((unsigned char)text[i])) i++;
        if (i >= n) break;

        std::string tok;
        while (i < n) {
            char c = text[i];
            if (isalnum((unsigned char)c)) {
                tok += (char)tolower((unsigned char)c);
                i++;
            } else if ((c == '-' || c == '\'') && i + 1 < n && isalnum((unsigned char)text[i + 1])) {
                tok += c;
                i++;
            } else {
                break;
            }
        }

        while (!tok.empty() && (tok.back() == '-' || tok.back() == '\''))
            tok.pop_back();
        while (!tok.empty() && (tok.front() == '-' || tok.front() == '\''))
            tok.erase(tok.begin());

        if (tok.size() < 2) continue;
        if (is_all_digits(tok)) continue;

        out << tok << '\n';
        token_count++;
        total_chars += (long long)tok.size();
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <corpus_dir> <output_file>\n";
        return 1;
    }

    const char *corpus_dir = argv[1];
    const char *out_path   = argv[2];

    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "Cannot open output file: " << out_path << "\n";
        return 1;
    }

    DIR *dir = opendir(corpus_dir);
    if (!dir) {
        std::cerr << "Cannot open corpus dir: " << corpus_dir << "\n";
        return 1;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    long long token_count = 0;
    long long total_chars = 0;
    long long input_bytes = 0;
    int file_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char *name = entry->d_name;
        int nlen = (int)strlen(name);
        if (nlen < 5 || strcmp(name + nlen - 4, ".txt") != 0) continue;

        std::string fpath = std::string(corpus_dir) + "/" + name;
        std::ifstream fin(fpath);
        if (!fin) continue;

        std::string title, url, line, body;
        std::getline(fin, title);
        std::getline(fin, url);
        while (std::getline(fin, line)) {
            if (!body.empty()) body += ' ';
            body += line;
        }
        fin.close();

        input_bytes += (long long)body.size();
        tokenize_text(body, out, token_count, total_chars);
        file_count++;
    }
    closedir(dir);
    out.close();

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    double speed_kb = (input_bytes / 1024.0) / elapsed;
    double avg_len = token_count > 0 ? (double)total_chars / token_count : 0;

    std::cerr << "Files processed:    " << file_count << "\n";
    std::cerr << "Total tokens:       " << token_count << "\n";
    std::cerr << "Average token len:  " << avg_len << " chars\n";
    std::cerr << "Input size:         " << input_bytes / (1024 * 1024) << " MB\n";
    std::cerr << "Time:               " << elapsed << " s\n";
    std::cerr << "Speed:              " << (long long)speed_kb << " KB/s\n";

    return 0;
}
