#include "cpplibostree.h"

// C++
#include <cstdlib>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
// C
#include <cstdio>
#include <fcntl.h>
#include <ostree.h>
#include <glib-2.0/glib.h>

namespace cpplibostree {

OSTreeRepo::OSTreeRepo(std::string path):
        repo_path(std::move(path)), 
        branches({}) {
    updateData();
}

auto OSTreeRepo::updateData() -> bool {

    std::string branchString = getBranchesAsString();
    std::stringstream bss(branchString);
    std::string word;
    while (bss >> word) {
        branches.push_back(word);
    }

    commit_list = parseCommitsAllBranches();

    return true;
}

// METHODS

auto OSTreeRepo::_c() -> OstreeRepo* {
    // open repo
    GError *error = nullptr;
    OstreeRepo *repo = ostree_repo_open_at(AT_FDCWD, repo_path.c_str(), nullptr, &error);
    return repo;
}

auto OSTreeRepo::getRepo() -> std::string* {
    return &repo_path;
}

auto OSTreeRepo::getCommitList() -> std::unordered_map<std::string,Commit> {
    return commit_list;
}

auto OSTreeRepo::getBranches() -> std::vector<std::string> {
    return branches;
}

auto OSTreeRepo::isCommitSigned(const Commit& commit) -> bool {
    return commit.signatures.size() > 0;
}

// source: https://github.com/ostreedev/ostree/blob/main/src/ostree/ot-dump.c#L52
static auto formatTimestamp(guint64 timestamp, gboolean local_tz) -> gchar* {
    GDateTime *dt;
    gchar *str;

    dt = g_date_time_new_from_unix_utc (timestamp);
    if (dt == nullptr) {
        return nullptr;
    }

    if (local_tz) {
        // convert to local time and display in the locale's preferred representation.
        g_autoptr (GDateTime) dt_local = g_date_time_to_local (dt);
        str = g_date_time_format (dt_local, "%c");
    } else {
        str = g_date_time_format (dt, "%Y-%m-%d %H:%M:%S +0000");
    }
    
    g_date_time_unref (dt);
    return str;
}

auto OSTreeRepo::parseCommit(GVariant *variant, std::string branch, std::string hash) -> Commit {
    Commit commit = {"error", "", 0, "", "", "", "", {}, {}};

    const gchar *subject;
    const gchar *body;
    guint64 timestamp;
    g_autofree char *parent = nullptr;
    g_autofree char *date = nullptr;
    g_autofree char *version = nullptr;
    //g_autoptr (GError) local_error = nullptr;

    // see OSTREE_COMMIT_GVARIANT_FORMAT
    g_variant_get(variant, "(a{sv}aya(say)&s&stayay)", nullptr, nullptr, nullptr, &subject, &body, &timestamp, nullptr, nullptr);

    timestamp = GUINT64_FROM_BE(timestamp);
    date = formatTimestamp(timestamp, FALSE);

    if ((parent = ostree_commit_get_parent(variant))) {
        commit.parent = parent;
    } else {
        commit.parent = "(no parent)";
    }

    g_autofree char *contents = ostree_commit_get_content_checksum(variant);
    commit.contentChecksum = contents;
    commit.timestamp = timestamp;
    commit.date = date;

    if (subject[0]) {
        std::string val = subject;
        commit.subject = val;
    } else {
        commit.subject = "(no subject)";
    }

    if (body[0]) {
        commit.body = body;
    }

    commit.branches.push_back(branch);
    commit.hash = hash;

    // Signatures ___ refactor into own method
    // open repo
    GError *error = nullptr;
    OstreeRepo *repo = ostree_repo_open_at(AT_FDCWD, repo_path.c_str(), nullptr, &error);
    if (repo == nullptr) {
        g_printerr("Error opening repository: %s\n", error->message);
        g_error_free(error);
    }
    // see ostree print_object for reference
    g_autoptr (OstreeGpgVerifyResult) result = nullptr;
    g_autoptr (GError) local_error = nullptr;
    result = ostree_repo_verify_commit_ext (repo, commit.parent.c_str(), nullptr, nullptr, nullptr,
                                                  &local_error);
    if (g_error_matches (local_error, OSTREE_GPG_ERROR, OSTREE_GPG_ERROR_NO_SIGNATURE)) {
        /* Ignore */
    } else if (local_error != nullptr) {
        /* Ignore */
    } else {
        guint n_sigs = ostree_gpg_verify_result_count_all (result);
        // parse all found signatures
        for (guint ii = 0; ii < n_sigs; ii++) {
            g_autoptr (GVariant) variant = nullptr;
            variant = ostree_gpg_verify_result_get_all (result, ii);
            // see ostree_gpg_verify_result_describe_variant for reference
            gint64 timestamp;
            gint64 exp_timestamp;
            const char *fingerprint;
            const char *pubkey_algo;
            const char *user_name;
            const char *user_email;

            g_variant_get_child (variant, OSTREE_GPG_SIGNATURE_ATTR_PUBKEY_ALGO_NAME, "&s", &pubkey_algo);
            g_variant_get_child (variant, OSTREE_GPG_SIGNATURE_ATTR_FINGERPRINT, "&s", &fingerprint);
            g_variant_get_child (variant, OSTREE_GPG_SIGNATURE_ATTR_TIMESTAMP, "x", &timestamp);
            g_variant_get_child (variant, OSTREE_GPG_SIGNATURE_ATTR_EXP_TIMESTAMP, "x", &exp_timestamp);
            g_variant_get_child (variant, OSTREE_GPG_SIGNATURE_ATTR_USER_NAME, "&s", &user_name);
            g_variant_get_child (variant, OSTREE_GPG_SIGNATURE_ATTR_USER_EMAIL, "&s", &user_email);
            
            // create signature struct
            Signature sig = {"error","","","","",""};
            sig.pubkey_algorithm = pubkey_algo;
            sig.fingerprint = fingerprint;
            sig.timestamp = std::to_string(timestamp);
            sig.expire_timestamp = std::to_string(exp_timestamp);
            sig.username = user_name;
            sig.usermail = user_email;

            commit.signatures.push_back(std::move(sig));
        }
    }
    

    return commit;
}

// modified log_commit() from https://github.com/ostreedev/ostree/blob/main/src/ostree/ot-builtin-log.c#L40
auto OSTreeRepo::parseCommitsRecursive (OstreeRepo *repo, const gchar *checksum, GError **error,
                std::unordered_map<std::string,Commit> *commit_list, std::string branch, gboolean is_recurse) -> gboolean {
    GError *local_error = nullptr;

    g_autoptr (GVariant) variant = nullptr;
    if (!ostree_repo_load_variant(repo, OSTREE_OBJECT_TYPE_COMMIT, checksum, &variant, &local_error)) {
        return is_recurse && g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    }

    commit_list->insert({
        static_cast<std::string>(checksum),
        parseCommit(variant, branch, static_cast<std::string>(checksum))
    });

    // parent recursion
    g_autofree char *parent = ostree_commit_get_parent(variant);
    if (parent && !parseCommitsRecursive(repo, parent, error, commit_list, branch, true)) {
        return false;
    }
    
    return true;
}

auto OSTreeRepo::parseCommitsOfBranch(std::string branch) -> std::unordered_map<std::string,Commit> {
    auto ret = std::unordered_map<std::string,Commit>();

    // open repo
    GError *error = nullptr;
    OstreeRepo *repo = ostree_repo_open_at(AT_FDCWD, repo_path.c_str(), nullptr, &error);
    if (repo == nullptr) {
        g_printerr("Error opening repository: %s\n", error->message);
        g_error_free(error);
        return ret;
    }

    // recursive commit log
    g_autofree char *checksum = nullptr;
    if (!ostree_repo_resolve_rev(repo, branch.c_str(), false, &checksum, &error)) {
        return ret;
    }

    parseCommitsRecursive(repo, checksum, &error, &ret, branch);

    return ret;
}

auto OSTreeRepo::parseCommitsAllBranches() -> std::unordered_map<std::string,Commit> {
    
    std::istringstream branches_string(getBranchesAsString());
    std::string branch;

    std::unordered_map<std::string,Commit> commits_all_branches;

    while (branches_string >> branch) {
        auto commits = parseCommitsOfBranch(branch);
        commits_all_branches.insert(commits.begin(), commits.end());
    }

    return commits_all_branches;
}

auto OSTreeRepo::getBranchesAsString() -> std::string {
    std::string branches_str = "";

    // open repo
    GError *error = nullptr;
    OstreeRepo *repo = ostree_repo_open_at(AT_FDCWD, repo_path.c_str(), nullptr, &error);

    if (repo == nullptr) {
        g_printerr("Error opening repository: %s\n", error->message);
        g_error_free(error);
        return "";
    }

    // get a list of refs
    GHashTable *refs_hash = nullptr;
    gboolean result = ostree_repo_list_refs_ext(repo, nullptr, &refs_hash, OSTREE_REPO_LIST_REFS_EXT_NONE, nullptr, &error);
    if (!result) {
        g_printerr("Error listing refs: %s\n", error->message);
        g_error_free(error);
        g_object_unref(repo);
        // TODO exit with error
        return "";
    }

    // iterate through the refs
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, refs_hash);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const gchar *ref_name = (const gchar *)key;
        branches_str += " " + std::string(ref_name);
    }

    // free
    g_hash_table_unref(refs_hash);

    return branches_str;
}

} // namespace cpplibostree