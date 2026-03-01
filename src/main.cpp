#include <cxxopts.hpp>
#include <fmt/core.h>
#include <filesystem>
#include <git2.h>
#include <random>
#include <chrono>
#include <vector>
#include <string>
#include <iostream>
#include <cstdio>
#include <array>
#include <unistd.h>  // close, unlink, mkstemp
#include <sys/wait.h> // WIFEXITED, WEXITSTATUS

namespace fs = std::filesystem;

struct Config {
    bool preview = false;
    int frequency = 80;
    std::string distribution = "uniform";
    std::string startDate = "";
    std::string endDate = "";
    std::string commitsPerDay = "0,4";
    bool newRepo = false;
    std::string dir = "";
    std::string name = "Fake Author";
    std::string email = "fake@example.com";
    bool gpgSign = false;
};

bool is_git_repo(const std::string& path = ".") {
    return fs::exists(path + "/.git") && fs::is_directory(path + "/.git");
}

void create_git_repo() {
    if (std::system("git init") != 0) {  // NOLINT
        fmt::print("Warning: git init returned non-zero\n");
    }
}

// Utility to parse date strings (YYYY/MM/DD)
std::tm parse_date(const std::string& date) {
    std::tm tm = {};
    sscanf(date.c_str(), "%d/%d/%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return tm;
}

// Utility to format date as string
std::string format_date(const std::tm& tm) {
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

// Generate a vector of dates between start and end
std::vector<std::tm> get_date_range(const std::string& start, const std::string& end) {
    std::vector<std::tm> dates;
    std::tm tm_start = parse_date(start);
    std::tm tm_end = parse_date(end);
    std::time_t t_start = std::mktime(&tm_start);
    std::time_t t_end = std::mktime(&tm_end);
    for (std::time_t t = t_start; t <= t_end; t += 86400) {
        std::tm tm = *std::localtime(&t);
        dates.push_back(tm);
    }
    return dates;
}

// Parse commitsPerDay string (e.g. "0,4")
std::pair<int, int> parse_commits_per_day(const std::string& s) {
    int min = 0, max = 4;
    sscanf(s.c_str(), "%d,%d", &min, &max);
    return {min, max};
}

// Distribution logic
int get_commit_count(const Config& cfg, const std::tm& date, std::mt19937& rng) {
    auto [min, max] = parse_commits_per_day(cfg.commitsPerDay);
    std::uniform_int_distribution<int> dist(min, max);
    int count = dist(rng);
    if (cfg.distribution == "workHours") {
        if (date.tm_wday >= 2 && date.tm_wday <= 4) {
            count += dist(rng) / 2;
        } else if (date.tm_wday == 0 || date.tm_wday == 6) {
            count = 0;
        }
    } else if (cfg.distribution == "afterWork") {
        if (date.tm_wday == 0 || date.tm_wday == 6) {
            count += dist(rng) / 2;
        } else if (date.tm_wday >= 2 && date.tm_wday <= 4) {
            count = min;
        }
    }
    return std::max(min, std::min(max, count));
}

void preview_activity(const std::vector<std::tm>& dates, const std::vector<int>& commits) {
    fmt::print("\nActivity Preview:\n");
    int totalCommits = 0, activeDays = 0;
    for (size_t i = 0; i < dates.size(); ++i) {
        fmt::print("Day {} / {} | {}: {} commits\n", i+1, dates.size(), format_date(dates[i]), commits[i]);
        if (commits[i] > 0) {
            totalCommits += commits[i];
            activeDays++;
        }
    }
    fmt::print("\nPreview Summary: {} total commits over {} days ({} days with commits)\n",
               totalCommits, dates.size(), activeDays);
}

// GPG-sign a commit buffer; returns the ASCII-armored signature or empty on failure
std::string gpg_sign_buffer(const std::string& buffer) {
    // Write commit buffer to a temp file (gpg needs a seekable input)
    char tmpIn[] = "/tmp/fgh_commit_XXXXXX";
    int fdIn = mkstemp(tmpIn);
    if (fdIn < 0) {
        fmt::print("Error: could not create temp file for GPG signing\n");
        return "";
    }
    FILE* f = fdopen(fdIn, "w");
    if (!f) {
        fmt::print("Error: could not open temp file for writing\n");
        close(fdIn);
        unlink(tmpIn);
        return "";
    }
    fwrite(buffer.data(), 1, buffer.size(), f);
    fclose(f);

    // Resolve configured signing key (may be empty → use gpg default key)
    std::string signingKey;
    {
        FILE* kp = popen("git config user.signingkey 2>/dev/null", "r");
        if (kp) {
            char kbuf[256] = {};
            if (fgets(kbuf, sizeof(kbuf), kp)) signingKey = kbuf;
            pclose(kp);
            // trim trailing whitespace / newline
            while (!signingKey.empty() &&
                   (signingKey.back() == '\n' || signingKey.back() == '\r' || signingKey.back() == ' '))
                signingKey.pop_back();
        }
    }

    // Build gpg command:
    //   --output -   forces the signature onto stdout (without this, gpg writes
    //                a side-car file next to tmpIn and stdout is empty)
    //   -bsa         detached, sign, ascii-armor
    //   -u <key>     only when a key is actually configured
    std::string cmd;
    if (!signingKey.empty()) {
        cmd = fmt::format("gpg -bsa --output - -u \"{}\" \"{}\" 2>/dev/null", signingKey, tmpIn);
    } else {
        cmd = fmt::format("gpg -bsa --output - \"{}\" 2>/dev/null", tmpIn);
    }

    // Read the armored signature from gpg's stdout
    std::string sig;
    FILE* gpgOut = popen(cmd.c_str(), "r");
    if (gpgOut) {
        char buf[256];
        while (fgets(buf, sizeof(buf), gpgOut))
            sig += buf;
        int status = pclose(gpgOut);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fmt::print("Warning: GPG signing failed (exit code {})\n",
                       WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            sig.clear();
        }
    } else {
        fmt::print("Warning: could not launch gpg process\n");
    }

    unlink(tmpIn);
    return sig;
}

void make_fake_commits(const std::vector<std::tm>& dates, const std::vector<int>& commits, const Config& cfg) {
    int totalCommits = 0, activeDays = 0, commitGoal = 0;
    for (int c : commits) commitGoal += c;

    git_libgit2_init();
    git_repository* repo = nullptr;

    if (git_repository_open(&repo, ".") != 0) {
        const git_error* e = git_error_last();
        fmt::print("Error: Could not open git repository: {}\n", e ? e->message : "unknown");
        git_libgit2_shutdown();
        return;
    }

    // Build an empty tree for all commits
    git_oid tree_oid;
    git_tree* tree = nullptr;
    {
        git_treebuilder* builder = nullptr;
        git_treebuilder_new(&builder, repo, nullptr);
        git_treebuilder_write(&tree_oid, builder);
        git_treebuilder_free(builder);
        if (git_tree_lookup(&tree, repo, &tree_oid) != 0) {
            const git_error* e = git_error_last();
            fmt::print("Error: Could not look up empty tree: {}\n", e ? e->message : "unknown");
            git_repository_free(repo);
            git_libgit2_shutdown();
            return;
        }
    }

    // Resolve existing HEAD / parent commit (may not exist on a fresh repo)
    git_oid parent_oid;
    git_commit* parent_commit = nullptr;
    bool has_parent = (git_reference_name_to_id(&parent_oid, repo, "HEAD") == 0 &&
                       git_commit_lookup(&parent_commit, repo, &parent_oid) == 0);

    int progress = 0;
    for (size_t i = 0; i < dates.size(); ++i) {
        if (commits[i] > 0) activeDays++;

        for (int j = 0; j < commits[i]; ++j) {
            std::tm tm = dates[i];
            std::time_t t = std::mktime(&tm);

            git_signature* author = nullptr;
            if (git_signature_new(&author, cfg.name.c_str(), cfg.email.c_str(), (git_time_t)t, 0) != 0) {
                const git_error* e = git_error_last();
                fmt::print("Error creating signature: {}\n", e ? e->message : "unknown");
                continue;
            }

            git_oid commit_oid;
            int err = 0;

            if (cfg.gpgSign) {
                // ---- GPG-signed path ----
                // Step 1: produce the raw commit buffer
                git_buf commit_buf = GIT_BUF_INIT;
                if (has_parent) {
                    const git_commit* parents[1] = { parent_commit };
                    err = git_commit_create_buffer(&commit_buf, repo, author, author,
                                                   nullptr, "fake commit", tree, 1, parents);
                } else {
                    err = git_commit_create_buffer(&commit_buf, repo, author, author,
                                                   nullptr, "fake commit", tree, 0, nullptr);
                }

                if (err != 0) {
                    const git_error* e = git_error_last();
                    fmt::print("Error creating commit buffer: {}\n", e ? e->message : "unknown");
                    git_signature_free(author);
                    git_buf_dispose(&commit_buf);
                    continue;
                }

                // Step 2: sign it
                std::string sig = gpg_sign_buffer(std::string(commit_buf.ptr, commit_buf.size));
                if (sig.empty()) {
                    fmt::print("Error: empty GPG signature — skipping commit\n");
                    git_signature_free(author);
                    git_buf_dispose(&commit_buf);
                    continue;
                }

                // Step 3: create commit with embedded signature
                err = git_commit_create_with_signature(
                    &commit_oid, repo,
                    commit_buf.ptr,
                    sig.c_str(),
                    "gpgsig"
                );
                git_buf_dispose(&commit_buf);

                if (err == 0) {
                    // git_commit_create_with_signature does NOT update refs — do it manually
                    git_reference* head_ref = nullptr;
                    if (git_repository_head(&head_ref, repo) == 0) {
                        git_reference* new_ref = nullptr;
                        git_reference_set_target(&new_ref, head_ref, &commit_oid, "fake commit");
                        if (new_ref) git_reference_free(new_ref);
                        git_reference_free(head_ref);
                    } else {
                        // Unborn HEAD — create the branch and point HEAD at it
                        git_reference* new_ref = nullptr;
                        git_reference_create(&new_ref, repo, "refs/heads/main", &commit_oid, 0, "fake commit");
                        if (new_ref) git_reference_free(new_ref);
                        git_repository_set_head(repo, "refs/heads/main");
                    }
                }
            } else {
                // ---- Normal (unsigned) path ----
                // FIX: pass parent_commit directly as the variadic arg, not &parent_commit
                if (has_parent) {
                    err = git_commit_create_v(
                        &commit_oid, repo, "HEAD",
                        author, author,
                        nullptr, "fake commit",
                        tree,
                        1, parent_commit   // correct: git_commit*, not git_commit**
                    );
                } else {
                    err = git_commit_create_v(
                        &commit_oid, repo, "HEAD",
                        author, author,
                        nullptr, "fake commit",
                        tree,
                        0
                    );
                }
            }

            git_signature_free(author);

            if (err != 0) {
                const git_error* e = git_error_last();
                fmt::print("Error creating commit: {}\n", e ? e->message : "unknown");
                continue;
            }

            has_parent = true;
            if (parent_commit) {
                git_commit_free(parent_commit);
                parent_commit = nullptr;
            }

            // Resolve the new commit as the parent for the next iteration
            if (git_commit_lookup(&parent_commit, repo, &commit_oid) != 0) {
                const git_error* e = git_error_last();
                fmt::print("Error looking up new commit as parent: {}\n", e ? e->message : "unknown");
                // Reset so the next commit doesn't try to use a null parent pointer
                has_parent = false;
                parent_commit = nullptr;
                continue;
            }

            totalCommits++;
            progress++;

            // Progress bar
            int barWidth = 40;
            float ratio = commitGoal > 0 ? (float)progress / commitGoal : 1.0f;
            int pos = (int)(barWidth * ratio);
            fmt::print("\r[");
            for (int k = 0; k < barWidth; ++k) fmt::print(k < pos ? "=" : " ");
            fmt::print("] {}% ({}/{})", (int)(ratio * 100), progress, commitGoal);
            std::cout.flush();
        }
    }

    fmt::print("\nProgress Summary: {} total commits over {} days ({} days with commits)\n",
               totalCommits, dates.size(), activeDays);

    if (parent_commit) git_commit_free(parent_commit);
    if (tree) git_tree_free(tree);
    git_repository_free(repo);
    git_libgit2_shutdown();
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("fake-it", "Fake GitHub commit history generator");
    options.add_options()
        ("preview",       "Preview activity graph",
                          cxxopts::value<bool>()->default_value("false"))
        ("frequency",     "Chance (0-100%) of generating commits per day",
                          cxxopts::value<int>()->default_value("80"))
        ("distribution",  "Distribution pattern (uniform, workHours, afterWork)",
                          cxxopts::value<std::string>()->default_value("uniform"))
        ("startDate",     "Start date (YYYY/MM/DD)",
                          cxxopts::value<std::string>())
        ("endDate",       "End date (YYYY/MM/DD)",
                          cxxopts::value<std::string>())
        ("commitsPerDay", "Commits per day range (min,max)",
                          cxxopts::value<std::string>()->default_value("0,4"))
        ("new",           "Force-create a new git repo",
                          cxxopts::value<bool>()->default_value("false"))
        ("dir",           "Directory to create/use for the repo (defaults to 'my-history' when not already in one)",
                          cxxopts::value<std::string>())
        ("name",          "Author/committer name",
                          cxxopts::value<std::string>()->default_value("Fake Author"))
        ("email",         "Author/committer email",
                          cxxopts::value<std::string>()->default_value("fake@example.com"))
        ("gpg-sign",      "GPG-sign every commit",
                          cxxopts::value<bool>()->default_value("false"))
        ("h,help",        "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        fmt::print("{}\n", options.help());
        return 0;
    }

    Config cfg;
    cfg.preview       = result["preview"].as<bool>();
    cfg.frequency     = result["frequency"].as<int>();
    cfg.distribution  = result["distribution"].as<std::string>();
    cfg.commitsPerDay = result["commitsPerDay"].as<std::string>();
    cfg.newRepo       = result["new"].as<bool>();
    cfg.name          = result["name"].as<std::string>();
    cfg.email         = result["email"].as<std::string>();
    cfg.gpgSign       = result["gpg-sign"].as<bool>();

    if (result.count("startDate")) cfg.startDate = result["startDate"].as<std::string>();
    if (result.count("endDate"))   cfg.endDate   = result["endDate"].as<std::string>();
    if (result.count("dir"))       cfg.dir       = result["dir"].as<std::string>();

    // Default date range: last year → today
    auto now_tp = std::chrono::system_clock::now();
    std::time_t t_now = std::chrono::system_clock::to_time_t(now_tp);
    std::tm tm_now = *std::localtime(&t_now);
    std::tm tm_last_year = tm_now;
    tm_last_year.tm_year -= 1;
    if (cfg.startDate.empty()) cfg.startDate = format_date(tm_last_year);
    if (cfg.endDate.empty())   cfg.endDate   = format_date(tm_now);

    // ---- Directory / repo setup ----
    std::string targetDir = cfg.dir;
    if (targetDir.empty() && !is_git_repo(".")) {
        // Not already in a repo and no --dir specified → use "my-history"
        targetDir = "my-history";
    }

    if (!targetDir.empty()) {
        if (!fs::exists(targetDir)) {
            fs::create_directories(targetDir);
            fmt::print("Created directory: {}\n", targetDir);
        }
        fs::current_path(targetDir);
        fmt::print("Working in directory: {}\n", fs::current_path().string());
    }

    if (cfg.newRepo || !is_git_repo(".")) {
        fmt::print("Initializing new git repo...\n");
        create_git_repo();
    } else {
        fmt::print("Using existing git repo at: {}\n", fs::current_path().string());
    }

    // Persist author identity in the local repo config so libgit2 and git CLI agree
    (void)std::system(fmt::format("git config user.name \"{}\"", cfg.name).c_str());
    (void)std::system(fmt::format("git config user.email \"{}\"", cfg.email).c_str());
    if (cfg.gpgSign) {
        (void)std::system("git config commit.gpgsign true");
    }

    // ---- Generate activity schedule ----
    auto dates = get_date_range(cfg.startDate, cfg.endDate);
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> freq_dist(0, 99);
    std::vector<int> commits;
    for (const auto& date : dates) {
        if (freq_dist(rng) < cfg.frequency) {
            commits.push_back(get_commit_count(cfg, date, rng));
        } else {
            commits.push_back(0);
        }
    }

    if (cfg.preview) {
        preview_activity(dates, commits);
    } else {
        make_fake_commits(dates, commits, cfg);
        fmt::print("Done! Fake commits created in: {}\n", fs::current_path().string());
    }

    return 0;
}
